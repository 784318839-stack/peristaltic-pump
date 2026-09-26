/******************************************************************************
 * command_protocol.cpp — 远程命令协议实现
 *
 * 线程模型 :
 *   - HTTP / USB 串口 / 硬件 UART 都在 loop() 上下文里直接调用 parseAndExecute()
 *   - 所有泵状态读写都发生在 loop() 单线程内 , 串行执行 , 无需互斥锁
 ******************************************************************************/
#include "command_protocol.h"
#include "pump_shared.h"
#include "pump_state.h"
#include "wifi_manager.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// ============================================================================
//                            JSON 解析 & 命令路由
// ============================================================================

// PSRAM 响应缓冲区
static char* responseBuf = nullptr;
#define RESPONSE_BUF_SIZE 1536

void initResponseBuffer() {
  responseBuf = (char*)heap_caps_malloc(RESPONSE_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!responseBuf) responseBuf = (char*)malloc(RESPONSE_BUF_SIZE);
  if (responseBuf) responseBuf[0] = '\0';
}

// 构造成功响应
static const char* okResponse( const char* cmd, const char* dataJson = nullptr ) {
  if ( dataJson ) {
    snprintf( responseBuf, RESPONSE_BUF_SIZE,
              "{\"type\":\"response\",\"id\":\"%s\",\"ok\":true,\"data\":%s}", cmd, dataJson );
  } else {
    snprintf( responseBuf, RESPONSE_BUF_SIZE,
              "{\"type\":\"response\",\"id\":\"%s\",\"ok\":true}", cmd );
  }
  return responseBuf;
}

// 构造错误响应
static const char* errResponse( const char* cmd, const char* error ) {
  snprintf( responseBuf, RESPONSE_BUF_SIZE,
            "{\"type\":\"response\",\"id\":\"%s\",\"ok\":false,\"error\":\"%s\"}", cmd, error );
  return responseBuf;
}

const char* parseAndExecute( const char* json ) {
  JsonDocument doc;  // ArduinoJson v7 默认栈分配

  DeserializationError err = deserializeJson( doc, json );
  if ( err ) {
    snprintf( responseBuf, RESPONSE_BUF_SIZE,
              "{\"type\":\"response\",\"id\":\"?\",\"ok\":false,\"error\":\"JSON parse: %s\"}",
              err.c_str() );
    return responseBuf;
  }

  const char* cmd = doc["cmd"] | "";
  if ( !cmd || strlen( cmd ) == 0 ) {
    return errResponse( "?", "Missing 'cmd' field" );
  }

  JsonObject params = doc["params"];

  // ===================================================================
  //  运行控制命令
  // ===================================================================

  if ( strcmp( cmd, "start" ) == 0 ) {
    if ( pump.state != STATE_IDLE && pump.state != DONE ) return errResponse( cmd, "Pump not idle" );
    // 校准向导里必须走 calib_start_run: start 用的是日常的 flowRate/targetVolume,
    // 而 calibStep 还停在 CALIB_RUN, 随后 calib_start_run 又会重新 moveTo, 校准步数就废了
    if ( pump.currentMenu == CALIBRATE )
      return errResponse( cmd, "Use calib_start_run during calibration" );
    if ( pump.mode == MODE_JET ) startJetCycle();
    else startPump();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "pause" ) == 0 ) {
    if ( pump.state != RUNNING ) return errResponse( cmd, "Pump not running" );
    pausePump();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "resume" ) == 0 ) {
    if ( pump.state != PAUSED ) return errResponse( cmd, "Pump not paused" );
    resumePump();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "stop" ) == 0 || strcmp( cmd, "reset" ) == 0 ) {
    // 校准运行中要走 calibStopRun(): 它才会清 calibRunning, 否则电机停了但
    // 遥测仍按 calibTargetVol 算进度, 向导也一直卡在「运行中」那一步
    if ( pump.calibRunning ) calibStopRun();
    else if ( pump.state == RUNNING || pump.state == PAUSED ) stopPump();
    // 校准运行时不碰喷射逻辑: 用户的 mode 可能就是 JET, 但这次运行与喷射无关
    if ( pump.mode == MODE_JET && !pump.calibRunning ) stopJetCycle();
    resetPump();
    pump.jetCount = 0;
    return okResponse( cmd );
  }

  // ===================================================================
  //  模式 & 液体选择
  // ===================================================================

  if ( strcmp( cmd, "set_mode" ) == 0 ) {
    if ( pump.state != STATE_IDLE && pump.state != DONE )
      return errResponse( cmd, "Cannot change mode while running" );
    // 校准向导期间不允许切模式: 下面那句 currentMenu=MAIN + resetPump() 会把向导打断,
    // 正在跑的校准电机也就失去 calibRunning 的看护 (tick_running 靠它选用哪套逻辑)。
    // 校准本身并不改 pump.mode —— 退出向导后模式仍是用户原来那个
    if ( pump.currentMenu == CALIBRATE )
      return errResponse( cmd, "Cannot change mode during calibration" );
    const char* modeStr = params["mode"] | "";
    if ( strcmp( modeStr, "VOLUME" ) == 0 || strcmp( modeStr, "volume" ) == 0 )
      pump.mode = MODE_VOLUME;
    else if ( strcmp( modeStr, "TIME" ) == 0 || strcmp( modeStr, "time" ) == 0 )
      pump.mode = MODE_TIME;
    else if ( strcmp( modeStr, "JET" ) == 0 || strcmp( modeStr, "jet" ) == 0 )
      pump.mode = MODE_JET;
    else
      return errResponse( cmd, "Invalid mode ( use VOLUME / TIME / JET )" );
    pump.currentMenu = MAIN;
    resetPump();   // clean state transition: reset stepper, volume, set state=IDLE
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "set_liquid" ) == 0 ) {
    // 校准向导自己管液体 (calibLiquid), 日常的液体选择在此期间冻结:
    // 换液体会同时改 stepsPerMl, 而校准运行正用 calibSPM() 算步数
    if ( pump.currentMenu == CALIBRATE )
      return errResponse( cmd, "Cannot change liquid during calibration" );
    int idx = params["index"] | -1;
    if ( idx < 0 || idx >= NUM_LIQUIDS ) return errResponse( cmd, "Invalid liquid index ( 0-3 )" );
    selectLiquid( idx );
    markDirty();
    beepConfirm();
    char data[64];
    snprintf( data, sizeof( data ), "{\"liquid\":\"%s\"}", LIQUID_NAMES[ idx ] );
    return okResponse( cmd, data );
  }

  // ===================================================================
  //  参数设置
  // ===================================================================

  if ( strcmp( cmd, "set_flow" ) == 0 ) {
    // 校准向导期间拒绝改日常参数 (set_flow / set_volume / set_time 同理):
    // 这三个命令结尾都会把 currentMenu 打回 MAIN, 等于悄悄退出向导,
    // 正在跑的校准电机也就没人管了 (calibRunning 还留着)。
    // 校准自己的体积和流量走 calib_set_vol (calibTargetVol / calibFlowRate),
    // 它们与 flowRate / targetVolume 是两套字段, 互不覆写
    if ( pump.currentMenu == CALIBRATE )
      return errResponse( cmd, "Cannot change flow during calibration" );
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 0.1 || val > 1600.0 )
      return errResponse( cmd, "Value out of range ( 0.1 - 1600 )" );
    pump.flowRate = constrain( val, 0.1f, 1600.0f );
    updateStepperSpeed();
    pump.currentMenu = MAIN;
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "set_volume" ) == 0 ) {
    if ( pump.currentMenu == CALIBRATE )   // 见 set_flow 处的说明
      return errResponse( cmd, "Cannot change volume during calibration" );
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 0.1 || val > 99999 )
      return errResponse( cmd, "Value out of range ( 0.1 - 99999 )" );
    pump.targetVolume = constrain( val, 0.1f, 99999.0f );
    pump.currentMenu = MAIN;
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "set_time" ) == 0 ) {
    if ( pump.currentMenu == CALIBRATE )   // 见 set_flow 处的说明
      return errResponse( cmd, "Cannot change time during calibration" );
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 1 || val > 86400 )
      return errResponse( cmd, "Value out of range ( 1 - 86400 )" );
    pump.targetTime = constrain( val, 1.0f, 86400.0f );
    pump.currentMenu = MAIN;
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  // ===================================================================
  //  喷射模式参数
  // ===================================================================

  if ( strcmp( cmd, "set_jet_vol" ) == 0 ) {
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 0.1 || val > 10.0 )
      return errResponse( cmd, "Value out of range ( 0.1 - 10.0 )" );
    pump.jetVolume = constrain( val, 0.1f, 10.0f );
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "set_jet_interval" ) == 0 ) {
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 1 || val > 60 )
      return errResponse( cmd, "Value out of range ( 1 - 60 )" );
    pump.jetInterval = constrain( val, 1.0f, 60.0f );
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "set_jet_flow" ) == 0 ) {
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 10 || val > 1600.0 )
      return errResponse( cmd, "Value out of range ( 10 - 1600 )" );
    pump.jetFlowRate = constrain( val, 10.0f, 1600.0f );
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "set_jet_pressure" ) == 0 ) {
    int val = params["value"] | -1;
    if ( val < 1 || val > 10 ) return errResponse( cmd, "Value out of range ( 1 - 10 )" );
    pump.jetPressure = constrain( ( float )val, 1.0f, 10.0f );
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "jet_start" ) == 0 ) {
    if ( pump.mode != MODE_JET ) return errResponse( cmd, "Not in jet mode" );
    if ( pump.state != STATE_IDLE ) return errResponse( cmd, "Pump not idle" );
    startJetCycle();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "jet_stop" ) == 0 ) {
    if ( pump.mode != MODE_JET ) return errResponse( cmd, "Not in jet mode" );
    if ( pump.state != RUNNING ) return errResponse( cmd, "Jet cycle not running" );
    stopJetCycle();
    return okResponse( cmd );
  }

  // ===================================================================
  //  校准向导 ( 远程命令 )
  // ===================================================================

  if ( strcmp( cmd, "calib_enter" ) == 0 ) {
    if ( pump.state != STATE_IDLE ) return errResponse( cmd, "Pump not idle" );
    pump.currentMenu = CALIBRATE;
    calibEnter();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "calib_select_liquid" ) == 0 ) {
    if ( pump.currentMenu != CALIBRATE || pump.calibStep != CALIB_SELECT_LIQUID )
      return errResponse( cmd, "Not at calib liquid selection step" );
    int idx = params["index"] | -1;
    if ( idx < 0 || idx >= NUM_LIQUIDS ) return errResponse( cmd, "Invalid liquid index ( 0-3 )" );
    // 只写 calibLiquid: 日常的 currentLiquid / stepsPerMl 要等 calib_save 才提交,
    // 中途 calib_abort 的话用户原来的液体选择一点不变
    pump.calibLiquid = idx;
    pump.calibStep = CALIB_SET_VOL;
    beepConfirm();
    char ldata[64];
    snprintf( ldata, sizeof( ldata ), "{\"calibLiquid\":%d,\"name\":\"%s\"}", idx, LIQUID_NAMES[ idx ] );
    return okResponse( cmd, ldata );
  }

  if ( strcmp( cmd, "calib_set_vol" ) == 0 ) {
    if ( pump.currentMenu != CALIBRATE )
      return errResponse( cmd, "Not in calibration menu" );
    if ( pump.calibStep != CALIB_SET_VOL && pump.calibStep != CALIB_SELECT_LIQUID )
      return errResponse( cmd, "Calib not ready for volume input" );
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 0.1 || val > 99999 )
      return errResponse( cmd, "Volume out of range ( 0.1 - 99999 )" );
    // 可选: 一并设定本次校准的流量。写入 calibFlowRate 而不是 flowRate,
    // 免得校准用的高速污染用户日常设定 (校准 1500 mL 时尤其需要单独提速)。
    // 两个值都校验通过再一起写 —— 否则流量非法时报了错, 体积却已经改了一半
    float f = params["flow"] | NAN;
    if ( !isnan( f ) && ( f < 0.1 || f > 1600.0 ) )
      return errResponse( cmd, "Calib flow out of range ( 0.1 - 1600 )" );
    pump.calibTargetVol = constrain( val, 0.1f, 99999.0f );
    if ( !isnan( f ) ) pump.calibFlowRate = constrain( f, 0.1f, 1600.0f );
    pump.calibStep = CALIB_RUN;
    beepConfirm();
    char cdata[128];
    snprintf( cdata, sizeof( cdata ), "{\"calibTargetVol\":%.1f,\"calibFlow\":%.1f}",
              pump.calibTargetVol, pump.calibFlowRate );
    return okResponse( cmd, cdata );
  }

  if ( strcmp( cmd, "calib_start_run" ) == 0 ) {
    if ( pump.currentMenu != CALIBRATE || pump.calibStep != CALIB_RUN )
      return errResponse( cmd, "Not at calib run step" );
    calibStartRun();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "calib_stop_run" ) == 0 ) {
    if ( !pump.calibRunning ) return errResponse( cmd, "Calib not running" );
    calibStopRun();
    pump.calibStep = CALIB_MEASURE;
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "calib_measure" ) == 0 ) {
    if ( pump.currentMenu != CALIBRATE || pump.calibStep != CALIB_MEASURE )
      return errResponse( cmd, "Not at calib measure step" );
    float val = params["value"] | NAN;
    if ( isnan( val ) || val <= 0 || val > 99999 )
      return errResponse( cmd, "Measured volume out of range" );
    pump.calibActualVol = val;
    // 算不出有效值时必须报错并停在 MEASURE 步, 否则 calibNewSPM 保持 0 却照样
    // 进入 RESULT, calib_save 会把 stepsPerMl=0 写进 EEPROM
    if ( !calibCalculate() )
      return errResponse( cmd, "No steps recorded - did the motor run?" );
    pump.calibStep = CALIB_RESULT;
    beepConfirm();
    char data[128];
    snprintf( data, sizeof( data ), "{\"oldSPM\":%.1f,\"newSPM\":%.1f}", calibSPM(), pump.calibNewSPM );
    return okResponse( cmd, data );
  }

  if ( strcmp( cmd, "calib_save" ) == 0 ) {
    if ( pump.currentMenu != CALIBRATE || pump.calibStep != CALIB_RESULT )
      return errResponse( cmd, "Not at calib result step" );
    if ( !( pump.calibNewSPM >= 10.0f ) )
      return errResponse( cmd, "Invalid stepsPerMl, please recalibrate" );
    calibSave();
    pump.calibStep = CALIB_SETTINGS;
    beepDone();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "calib_abort" ) == 0 ) {
    pump.calibRunning = false;
    if ( pump.state == RUNNING ) stopPump();
    calibLeave();          // 只清菜单和步骤 —— 日常设定从来没被校准改过
    beepCancel();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "calib_settings_done" ) == 0 ) {
    if ( pump.currentMenu != CALIBRATE || pump.calibStep != CALIB_SETTINGS )
      return errResponse( cmd, "Not at calib settings step" );
    calibLeave();
    beepConfirm();
    return okResponse( cmd );
  }

  // ===================================================================
  //  高级设置
  // ===================================================================

  if ( strcmp( cmd, "set_anti_drip" ) == 0 ) {
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 0 || val > 5.0 )
      return errResponse( cmd, "Value out of range ( 0 - 5.0 )" );
    pump.antiDripVol = constrain( val, 0.0f, 5.0f );
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "set_tube_life" ) == 0 ) {
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 0 || val > 200000 )
      return errResponse( cmd, "Value out of range ( 0 - 200000 )" );
    pump.tubeLifeML = constrain( val, 0.0f, 200000.0f );
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  // ===================================================================
  //  预灌 / 快排
  // ===================================================================

  if ( strcmp( cmd, "prime_start" ) == 0 ) {
    if ( pump.state != STATE_IDLE ) return errResponse( cmd, "Pump not idle" );
    if ( pump.currentMenu == CALIBRATE )   // 会把菜单改成 PRIME, 向导和强制的 VOLUME 模式就都回不来了
      return errResponse( cmd, "Cannot prime during calibration" );
    pump.currentMenu = PRIME;
    ensureStepperOn();
    applySpeed( flowRateToPPS( 1500.0 ), flowRateToPPS( 1500.0 ) );
    stepper->setCurrentPosition( 0 );
    stepper->moveTo( 999999999 );  /* 远超实际需要, MCPWM 硬件持续发脉冲直到 forceStop */
    pump.dispensedVolume = 0;
    pump.state = RUNNING;
    beepStart();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "prime_stop" ) == 0 ) {
    if ( pump.currentMenu != PRIME ) return errResponse( cmd, "Not in prime mode" );
    stopPump();
    pump.currentMenu = MAIN;
    beepPause();
    return okResponse( cmd );
  }


  // ===================================================================
  //  菜单 & 查询
  // ===================================================================

  if ( strcmp( cmd, "menu_main" ) == 0 ) {
    // 从校准菜单返回也统一走 calibLeave(), 并停掉可能还在跑的校准电机
    if ( pump.calibRunning ) calibStopRun();
    if ( pump.currentMenu == CALIBRATE ) calibLeave();
    else { pump.currentMenu = MAIN; pump.calibStep = CALIB_IDLE; }
    beepCancel();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "get_state" ) == 0 ) {
    const char* telemetry = buildTelemetryJson();
    snprintf( responseBuf, RESPONSE_BUF_SIZE,
              "{\"type\":\"response\",\"id\":\"get_state\",\"ok\":true,\"data\":%s}", telemetry );
    return responseBuf;
  }

  if ( strcmp( cmd, "wifi_restart" ) == 0 ) {
    restartWiFi();
    beepConfirm();
    return okResponse( cmd );
  }

  return errResponse( cmd, "Unknown command" );
}

// ============================================================================
//                                遥测
// ============================================================================

static char* telemetryBuf = nullptr;
#define TELEMETRY_BUF_SIZE 1024

// PSRAM 状态 (每次遥测刷新)
static size_t psramFree = 0, psramTotal = 0;

void initTelemetryBuffer() {
  // 优先分配到 PSRAM
  telemetryBuf = (char*)heap_caps_malloc(TELEMETRY_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!telemetryBuf) {
    telemetryBuf = (char*)malloc(TELEMETRY_BUF_SIZE);  // 回退到内部 RAM
  }
  if (telemetryBuf) telemetryBuf[0] = '\0';
}

const char* buildTelemetryJson() {
  if (!telemetryBuf) return "{}";

  // 刷新 PSRAM 统计
  psramTotal = ESP.getPsramSize();
  psramFree  = ESP.getFreePsram();

  // 内部 RAM 统计
  size_t heapFree = ESP.getFreeHeap();
  size_t heapTotal = ESP.getHeapSize();  // 实测值 (原先硬编码 327680 是假数据)
  const char* modeStr = ( pump.mode == MODE_TIME ) ? "TIME"
                      : ( pump.mode == MODE_JET )  ? "JET" : "VOLUME";

  const char* stateStr = "IDLE";
  switch ( pump.state ) {
    case RUNNING:     stateStr = "RUNNING";     break;
    case PAUSED:      stateStr = "PAUSED";      break;
    case DONE:        stateStr = "DONE";        break;
    case ANTI_DRIP:   stateStr = "ANTI_DRIP";   break;
    default: break;
  }

  const char* menuStr = "MAIN";
  switch ( pump.currentMenu ) {
    case CALIBRATE: menuStr = "CALIBRATE"; break;
    case PRIME:     menuStr = "PRIME";     break;
    default: break;
  }

  // 进度百分比
  int progress = 0;
  if ( pump.state == RUNNING || pump.state == PAUSED || pump.state == DONE ) {
    // 校准运行时分母用 calibTargetVol —— calibStartRun() 不再覆写 targetVolume
    // (那个字段会落盘, 覆写它会让 calibSave() 把校准体积误存成用户的目标体积)
    float denom = pump.calibRunning ? pump.calibTargetVol : pump.targetVolume;
    if ( denom > 0 ) {
      progress = ( int )( pump.dispensedVolume / denom * 100 );
      if ( progress > 100 ) progress = 100;
    }
  }

  // 已运行秒数。PAUSED 报冻结的 pumpElapsed 而不是 0 —— TIME 模式的倒计时靠它,
  // 报 0 会让剩余时间在一按暂停时跳回满值
  unsigned long elapsed = 0;
  if ( pump.state == RUNNING )
    elapsed = ( millis() - pump.pumpStartMs ) / 1000;
  else if ( pump.state == PAUSED )
    elapsed = pump.pumpElapsed;

  // 管路寿命百分比
  int tubePct = ( pump.tubeLifeML > 0 ) ? ( int )( pump.totalDispensed / pump.tubeLifeML * 100 ) : 0;

  // WiFi 状态
  const char* wifiMode = nullptr;
  const char* wifiIP = nullptr;
  int wifiClients = 0;
  getWiFiStatus( wifiMode, wifiIP, wifiClients );

  snprintf( telemetryBuf, TELEMETRY_BUF_SIZE,
    "{"
    "\"type\":\"telemetry\","
    "\"ts\":%lu,"
    "\"state\":\"%s\","
    "\"menu\":\"%s\","
    "\"mode\":\"%s\","
    "\"liquid\":\"%s\","
    "\"liquidIdx\":%d,"
    "\"flow\":%.1f,"
    "\"targetVol\":%.1f,"
    "\"calibTargetVol\":%.1f,"
    "\"calibFlow\":%.1f,"
    "\"calibLiquid\":%d,"
    "\"targetTime\":%.1f,"
    "\"dispensed\":%.2f,"
    "\"elapsed\":%lu,"
    "\"progress\":%d,"
    "\"totalDispensed\":%.1f,"
    "\"tubePct\":%d,"
	    "\"tubeLifeML\":%.0f,"
    "\"jetCount\":%d,"
    "\"jetVolume\":%.1f,"
    "\"jetInterval\":%.0f,"
    "\"jetFlowRate\":%.1f,"
    "\"jetPressure\":%.0f,"
    "\"stepsPerMl\":%.1f,"
    "\"antiDripVol\":%.2f,"
    "\"stepperEnabled\":%s,"
    "\"calibStep\":%d,"
    "\"wifiMode\":\"%s\","
    "\"wifiIP\":\"%s\","
    "\"wifiClients\":%d,"
    "\"psramFree\":%d,"
    "\"psramTotal\":%d,"
    "\"heapFree\":%d,"
    "\"heapTotal\":%d"
    "}",
    ( unsigned long )millis(),
    stateStr, menuStr, modeStr,
    LIQUID_NAMES[ pump.currentLiquid ], pump.currentLiquid,
    pump.flowRate, pump.targetVolume, pump.calibTargetVol, pump.calibFlowRate, pump.calibLiquid, pump.targetTime,
    pump.dispensedVolume, elapsed, progress,
    pump.totalDispensed, tubePct, pump.tubeLifeML,
    ( pump.mode == MODE_JET ) ? pump.jetCount : 0,
    pump.jetVolume, pump.jetInterval, pump.jetFlowRate, pump.jetPressure,
    pump.stepsPerMl, pump.antiDripVol,
    pump.stepperEnabled ? "true" : "false",
    ( int )pump.calibStep,
    wifiMode ? wifiMode : "ap",
    wifiIP ? wifiIP : "192.168.4.1",
    wifiClients,
    ( int )( psramFree / 1024 ),    // PSRAM free KB
    ( int )( psramTotal / 1024 ),   // PSRAM total KB
    ( int )( heapFree / 1024 ),     // internal heap free KB
    ( int )( heapTotal / 1024 )     // internal heap total KB
  );

  return telemetryBuf;
}
