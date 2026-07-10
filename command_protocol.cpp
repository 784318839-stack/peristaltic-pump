/******************************************************************************
 * command_protocol.cpp — 远程命令协议实现
 *
 * 线程安全设计 :
 *   - Web / BLE / 串口回调 : 只调用 enqueueCommand() 入队
 *   - loop()            : 调用 processCommandQueue() 出队并执行
 *   - 所有状态读写都在 loop() 线程上串行化 , 无需互斥锁
 ******************************************************************************/
#include "command_protocol.h"
#include "pump_shared.h"
#include "wifi_manager.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// ============================================================================
//                            FreeRTOS 命令队列
// ============================================================================
static QueueHandle_t cmdQueue = nullptr;
static CommandResponseCallback g_responseCb = nullptr;

void initCommandQueue() {
  cmdQueue = xQueueCreate( CMD_QUEUE_SIZE, sizeof( CommandMsg ) );
}

void setCommandResponseCallback( CommandResponseCallback cb ) {
  g_responseCb = cb;
}

bool enqueueCommand( const char* json ) {
  return enqueueCommandClient( json, 0 );  // clientId = 0 表示无客户端 ( 串口 )
}

bool enqueueCommandClient( const char* json, uint32_t clientId ) {
  if ( !cmdQueue ) return false;
  CommandMsg msg;
  msg.clientId = clientId;
  strncpy( msg.json, json, CMD_JSON_MAX - 1 );
  msg.json[CMD_JSON_MAX - 1] = '\0';
  BaseType_t ret = xQueueSend( cmdQueue, &msg, 0 );  // 非阻塞入队
  return ( ret == pdTRUE );
}

void processCommandQueue() {
  if ( !cmdQueue ) return;
  CommandMsg msg;
  while ( xQueueReceive( cmdQueue, &msg, 0 ) == pdTRUE ) {
    const char* response = parseAndExecute( msg.json );
    // 如果有 HTTP 客户端关联且注册了回调 , 将响应发回
    if ( msg.clientId != 0 && g_responseCb && response ) {
      g_responseCb( msg.clientId, response );
    }
  }
}

// ============================================================================
//                            JSON 解析 & 命令路由
// ============================================================================

// PSRAM 响应缓冲区
static char* responseBuf = nullptr;
#define RESPONSE_BUF_SIZE 512

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
    if ( pumpState == STALL_ERROR ) return errResponse( cmd, "Motor stalled! Reset first" );
    if ( pumpState != STATE_IDLE && pumpState != DONE ) return errResponse( cmd, "Pump not idle" );
    if ( pumpMode == MODE_JET ) startJetCycle();
    else startPump();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "pause" ) == 0 ) {
    if ( pumpState != RUNNING ) return errResponse( cmd, "Pump not running" );
    pausePump();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "resume" ) == 0 ) {
    if ( pumpState != PAUSED ) return errResponse( cmd, "Pump not paused" );
    resumePump();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "stop" ) == 0 || strcmp( cmd, "reset" ) == 0 ) {
    if ( pumpState == RUNNING || pumpState == PAUSED ) stopPump();
    if ( pumpMode == MODE_JET ) stopJetCycle();
    resetPump();
    jetCount = 0;
    return okResponse( cmd );
  }

  // ===================================================================
  //  模式 & 液体选择
  // ===================================================================

  if ( strcmp( cmd, "set_mode" ) == 0 ) {
    if ( pumpState != STATE_IDLE && pumpState != DONE )
      return errResponse( cmd, "Cannot change mode while running" );
    const char* modeStr = params["mode"] | "";
    if ( strcmp( modeStr, "VOLUME" ) == 0 || strcmp( modeStr, "volume" ) == 0 )
      pumpMode = MODE_VOLUME;
    else if ( strcmp( modeStr, "TIME" ) == 0 || strcmp( modeStr, "time" ) == 0 )
      pumpMode = MODE_TIME;
    else if ( strcmp( modeStr, "JET" ) == 0 || strcmp( modeStr, "jet" ) == 0 )
      pumpMode = MODE_JET;
    else
      return errResponse( cmd, "Invalid mode ( use VOLUME / TIME / JET )" );
    currentMenu = MAIN;
    resetPump();   // clean state transition: reset stepper, volume, set state=IDLE
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "set_liquid" ) == 0 ) {
    int idx = params["index"] | -1;
    if ( idx < 0 || idx >= NUM_LIQUIDS ) return errResponse( cmd, "Invalid liquid index ( 0-3 )" );
    selectLiquid( idx );
    markDirty();
    beepConfirm();
    char data[64];
    snprintf( data, sizeof( data ), "{\"liquid\":\"%s\"}", liquidNames[ idx ] );
    return okResponse( cmd, data );
  }

  // ===================================================================
  //  参数设置
  // ===================================================================

  if ( strcmp( cmd, "set_flow" ) == 0 ) {
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 0.1 || val > 2000.0 )
      return errResponse( cmd, "Value out of range ( 0.1 - 2000 )" );
    flowRate = constrain( val, 0.1f, 2000.0f );
    updateStepperSpeed();
    currentMenu = MAIN;
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "set_volume" ) == 0 ) {
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 0.1 || val > 99999 )
      return errResponse( cmd, "Value out of range ( 0.1 - 99999 )" );
    targetVolume = constrain( val, 0.1f, 99999.0f );
    currentMenu = MAIN;
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "set_time" ) == 0 ) {
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 1 || val > 86400 )
      return errResponse( cmd, "Value out of range ( 1 - 86400 )" );
    targetTime = constrain( val, 1.0f, 86400.0f );
    currentMenu = MAIN;
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
    jetVolume = constrain( val, 0.1f, 10.0f );
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "set_jet_interval" ) == 0 ) {
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 1 || val > 60 )
      return errResponse( cmd, "Value out of range ( 1 - 60 )" );
    jetInterval = constrain( val, 1.0f, 60.0f );
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "set_jet_flow" ) == 0 ) {
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 10 || val > 2000.0 )
      return errResponse( cmd, "Value out of range ( 10 - 2000 )" );
    jetFlowRate = constrain( val, 10.0f, 2000.0f );
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "set_jet_pressure" ) == 0 ) {
    int val = params["value"] | -1;
    if ( val < 1 || val > 10 ) return errResponse( cmd, "Value out of range ( 1 - 10 )" );
    jetPressure = constrain( ( float )val, 1.0f, 10.0f );
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "jet_start" ) == 0 ) {
    if ( pumpMode != MODE_JET ) return errResponse( cmd, "Not in jet mode" );
    if ( pumpState != STATE_IDLE ) return errResponse( cmd, "Pump not idle" );
    startJetCycle();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "jet_stop" ) == 0 ) {
    if ( pumpMode != MODE_JET ) return errResponse( cmd, "Not in jet mode" );
    if ( pumpState != RUNNING ) return errResponse( cmd, "Jet cycle not running" );
    stopJetCycle();
    return okResponse( cmd );
  }

  // ===================================================================
  //  校准向导 ( 6 步远程命令 )
  // ===================================================================

  if ( strcmp( cmd, "calib_enter" ) == 0 ) {
    if ( pumpState != STATE_IDLE ) return errResponse( cmd, "Pump not idle" );
    currentMenu = CALIBRATE;
    calibEnter();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "calib_select_liquid" ) == 0 ) {
    if ( currentMenu != CALIBRATE || calibStep != CALIB_SELECT_LIQUID )
      return errResponse( cmd, "Not at calib liquid selection step" );
    int idx = params["index"] | -1;
    if ( idx < 0 || idx >= NUM_LIQUIDS ) return errResponse( cmd, "Invalid liquid index ( 0-3 )" );
    currentLiquid = idx;
    calibStep = CALIB_SET_VOL;
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "calib_set_vol" ) == 0 ) {
    if ( currentMenu != CALIBRATE || calibStep != CALIB_SET_VOL )
      return errResponse( cmd, "Not at calib set volume step" );
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 0.1 || val > 99999 )
      return errResponse( cmd, "Volume out of range ( 0.1 - 99999 )" );
    calibTargetVol = constrain( val, 0.1f, 99999.0f );
    calibStep = CALIB_RUN;
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "calib_start_run" ) == 0 ) {
    if ( currentMenu != CALIBRATE || calibStep != CALIB_RUN )
      return errResponse( cmd, "Not at calib run step" );
    calibStartRun();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "calib_stop_run" ) == 0 ) {
    if ( !calibRunning ) return errResponse( cmd, "Calib not running" );
    calibStopRun();
    calibStep = CALIB_MEASURE;
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "calib_measure" ) == 0 ) {
    if ( currentMenu != CALIBRATE || calibStep != CALIB_MEASURE )
      return errResponse( cmd, "Not at calib measure step" );
    float val = params["value"] | NAN;
    if ( isnan( val ) || val <= 0 || val > 99999 )
      return errResponse( cmd, "Measured volume out of range" );
    calibActualVol = val;
    calibCalculate();
    calibStep = CALIB_RESULT;
    beepConfirm();
    char data[128];
    snprintf( data, sizeof( data ), "{\"oldSPM\":%.1f,\"newSPM\":%.1f}", stepsPerMl, calibNewSPM );
    return okResponse( cmd, data );
  }

  if ( strcmp( cmd, "calib_save" ) == 0 ) {
    if ( currentMenu != CALIBRATE || calibStep != CALIB_RESULT )
      return errResponse( cmd, "Not at calib result step" );
    calibSave();
    calibStep = CALIB_SETTINGS;
    beepDone();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "calib_abort" ) == 0 ) {
    currentMenu = MAIN;
    calibStep = CALIB_IDLE;
    calibRunning = false;
    if ( pumpState == RUNNING ) stopPump();
    beepCancel();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "calib_settings_done" ) == 0 ) {
    if ( currentMenu != CALIBRATE || calibStep != CALIB_SETTINGS )
      return errResponse( cmd, "Not at calib settings step" );
    currentMenu = MAIN;
    calibStep = CALIB_IDLE;
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
    antiDripVol = constrain( val, 0.0f, 5.0f );
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "set_tube_life" ) == 0 ) {
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 0 || val > 200000 )
      return errResponse( cmd, "Value out of range ( 0 - 200000 )" );
    tubeLifeML = constrain( val, 0.0f, 200000.0f );
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  // ===================================================================
  //  预灌 / 快排
  // ===================================================================

  if ( strcmp( cmd, "prime_start" ) == 0 ) {
    if ( pumpState != STATE_IDLE ) return errResponse( cmd, "Pump not idle" );
    currentMenu = PRIME;
    ensureStepperOn();
    stepper->setSpeedInHz( ( uint32_t )flowRateToPPS( 2000.0 ) );
    stepper->setAcceleration( ( int )flowRateToPPS( 2000.0 ) );
    stepper->setCurrentPosition( 0 );
    stepper->moveTo( 999999999 );  /* 远超实际, RMT 硬件持续运行直到 forceStop */
    dispensedVolume = 0;
    pumpState = RUNNING;
    beepStart();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "prime_stop" ) == 0 ) {
    if ( currentMenu != PRIME ) return errResponse( cmd, "Not in prime mode" );
    stopPump();
    currentMenu = MAIN;
    beepPause();
    return okResponse( cmd );
  }

  // ===================================================================
  //  方案预设
  // ===================================================================

  if ( strcmp( cmd, "preset_load" ) == 0 ) {
    int slot = params["slot"] | -1;
    if ( slot < 0 || slot > 3 ) return errResponse( cmd, "Invalid slot ( 0-3 )" );
    if ( !isPresetValid( slot ) ) return errResponse( cmd, "Preset slot empty" );
    loadPreset( slot );
    presetSlot = slot;
    currentMenu = MAIN;
    beepConfirm();
    char data[64];
    snprintf( data, sizeof( data ), "{\"slot\":%d}", slot );
    return okResponse( cmd, data );
  }

  if ( strcmp( cmd, "preset_save" ) == 0 ) {
    int slot = params["slot"] | -1;
    if ( slot < 0 || slot > 3 ) return errResponse( cmd, "Invalid slot ( 0-3 )" );
    savePreset( slot );
    presetSlot = slot;
    beepConfirm();
    char data[64];
    snprintf( data, sizeof( data ), "{\"slot\":%d}", slot );
    return okResponse( cmd, data );
  }

  // ===================================================================
  //  菜单 & 查询
  // ===================================================================

  if ( strcmp( cmd, "menu_main" ) == 0 ) {
    currentMenu = MAIN;
    calibStep = CALIB_IDLE;
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
  size_t heapTotal = 327680;  // ESP32-S3 内部 DRAM 总量
  const char* modeStr = ( pumpMode == MODE_TIME ) ? "TIME"
                      : ( pumpMode == MODE_JET )  ? "JET" : "VOLUME";

  const char* stateStr = "IDLE";
  switch ( pumpState ) {
    case RUNNING:     stateStr = "RUNNING";     break;
    case PAUSED:      stateStr = "PAUSED";      break;
    case DONE:        stateStr = "DONE";        break;
    case ANTI_DRIP:   stateStr = "ANTI_DRIP";   break;
    case STALL_ERROR: stateStr = "STALL_ERROR"; break;
    default: break;
  }

  const char* menuStr = "MAIN";
  switch ( currentMenu ) {
    case SET_FLOW:         menuStr = "SET_FLOW";         break;
    case SET_VOL:          menuStr = "SET_VOL";          break;
    case SET_TIME:         menuStr = "SET_TIME";         break;
    case CALIBRATE:        menuStr = "CALIBRATE";        break;
    case PRIME:            menuStr = "PRIME";            break;
    case SET_JET_VOL:      menuStr = "SET_JET_VOL";      break;
    case SET_JET_INTERVAL: menuStr = "SET_JET_INTERVAL"; break;
    case SET_JET_FLOW:     menuStr = "SET_JET_FLOW";     break;
    case SET_JET_PRESSURE: menuStr = "SET_JET_PRESSURE"; break;
    case SELECT_LIQUID:    menuStr = "SELECT_LIQUID";    break;
    case JET_OPTIONS:      menuStr = "JET_OPTIONS";      break;
    case PRESET_LOAD:      menuStr = "PRESET_LOAD";      break;
    default: break;
  }

  // 进度百分比
  int progress = 0;
  if ( ( pumpState == RUNNING || pumpState == PAUSED || pumpState == DONE ) && targetVolume > 0 ) {
    progress = ( int )( dispensedVolume / targetVolume * 100 );
    if ( progress > 100 ) progress = 100;
  }

  // 已运行秒数
  unsigned long elapsed = 0;
  if ( pumpState == RUNNING )
    elapsed = ( millis() - pumpStartMs ) / 1000;

  // 管路寿命百分比
  int tubePct = ( tubeLifeML > 0 ) ? ( int )( totalDispensed / tubeLifeML * 100 ) : 0;

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
    "\"targetTime\":%.1f,"
    "\"dispensed\":%.2f,"
    "\"elapsed\":%lu,"
    "\"progress\":%d,"
    "\"totalDispensed\":%.1f,"
    "\"tubePct\":%d,"
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
    liquidNames[ currentLiquid ], currentLiquid,
    flowRate, targetVolume, targetTime,
    dispensedVolume, elapsed, progress,
    totalDispensed, tubePct,
    ( pumpMode == MODE_JET ) ? jetCount : 0,
    jetVolume, jetInterval, jetFlowRate, jetPressure,
    stepsPerMl, antiDripVol,
    stepperEnabled ? "true" : "false",
    ( int )calibStep,
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
