/******************************************************************************
 * command_protocol.cpp 鈥?杩滅▼鍛戒护鍗忚瀹炵幇
 *
 * 绾跨▼瀹夊叏璁捐 :
 *   - Web / BLE / 涓插彛鍥炶皟 : 鍙皟鐢?enqueueCommand() 鍏ラ槦
 *   - loop()            : 璋冪敤 processCommandQueue() 鍑洪槦骞舵墽琛?
 *   - 鎵€鏈夌姸鎬佽鍐欓兘鍦?loop() 绾跨▼涓婁覆琛屽寲 , 鏃犻渶浜掓枼閿?
 ******************************************************************************/
#include "command_protocol.h"
#include "pump_shared.h"
#include "pump_state.h"
#include "wifi_manager.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

// ============================================================================
//                            FreeRTOS 鍛戒护闃熷垪
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
  return enqueueCommandClient( json, 0 );  // clientId = 0 琛ㄧず鏃犲鎴风 ( 涓插彛 )
}

bool enqueueCommandClient( const char* json, uint32_t clientId ) {
  if ( !cmdQueue ) return false;
  CommandMsg msg;
  msg.clientId = clientId;
  strncpy( msg.json, json, CMD_JSON_MAX - 1 );
  msg.json[CMD_JSON_MAX - 1] = '\0';
  BaseType_t ret = xQueueSend( cmdQueue, &msg, 0 );  // 闈為樆濉炲叆闃?
  return ( ret == pdTRUE );
}

void processCommandQueue() {
  if ( !cmdQueue ) return;
  CommandMsg msg;
  while ( xQueueReceive( cmdQueue, &msg, 0 ) == pdTRUE ) {
    const char* response = parseAndExecute( msg.json );
    // 濡傛灉鏈?HTTP 瀹㈡埛绔叧鑱斾笖娉ㄥ唽浜嗗洖璋?, 灏嗗搷搴斿彂鍥?
    if ( msg.clientId != 0 && g_responseCb && response ) {
      g_responseCb( msg.clientId, response );
    }
  }
}

// ============================================================================
//                            JSON 瑙ｆ瀽 & 鍛戒护璺敱
// ============================================================================

// PSRAM 鍝嶅簲缂撳啿鍖?
static char* responseBuf = nullptr;
#define RESPONSE_BUF_SIZE 512

void initResponseBuffer() {
  responseBuf = (char*)heap_caps_malloc(RESPONSE_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!responseBuf) responseBuf = (char*)malloc(RESPONSE_BUF_SIZE);
  if (responseBuf) responseBuf[0] = '\0';
}

// 鏋勯€犳垚鍔熷搷搴?
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

// 鏋勯€犻敊璇搷搴?
static const char* errResponse( const char* cmd, const char* error ) {
  snprintf( responseBuf, RESPONSE_BUF_SIZE,
            "{\"type\":\"response\",\"id\":\"%s\",\"ok\":false,\"error\":\"%s\"}", cmd, error );
  return responseBuf;
}

const char* parseAndExecute( const char* json ) {
  JsonDocument doc;  // ArduinoJson v7 榛樿鏍堝垎閰?

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
  //  杩愯鎺у埗鍛戒护
  // ===================================================================

  if ( strcmp( cmd, "start" ) == 0 ) {
    if ( pump.state == STALL_ERROR ) return errResponse( cmd, "Motor stalled! Reset first" );
    if ( pump.state != STATE_IDLE && pump.state != DONE ) return errResponse( cmd, "Pump not idle" );
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
    if ( pump.state == RUNNING || pump.state == PAUSED ) stopPump();
    if ( pump.mode == MODE_JET ) stopJetCycle();
    resetPump();
    pump.jetCount = 0;
    return okResponse( cmd );
  }

  // ===================================================================
  //  妯″紡 & 娑蹭綋閫夋嫨
  // ===================================================================

  if ( strcmp( cmd, "set_mode" ) == 0 ) {
    if ( pump.state != STATE_IDLE && pump.state != DONE )
      return errResponse( cmd, "Cannot change mode while running" );
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
  //  鍙傛暟璁剧疆
  // ===================================================================

  if ( strcmp( cmd, "set_flow" ) == 0 ) {
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 0.1 || val > 2000.0 )
      return errResponse( cmd, "Value out of range ( 0.1 - 2000 )" );
    pump.flowRate = constrain( val, 0.1f, 9999.0f );
    updateStepperSpeed();
    pump.currentMenu = MAIN;
    markDirty();
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "set_volume" ) == 0 ) {
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
  //  鍠峰皠妯″紡鍙傛暟
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
    if ( isnan( val ) || val < 10 || val > 2000.0 )
      return errResponse( cmd, "Value out of range ( 10 - 2000 )" );
    pump.jetFlowRate = constrain( val, 10.0f, 9999.0f );
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
  //  鏍″噯鍚戝 ( 6 姝ヨ繙绋嬪懡浠?)
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
    pump.currentLiquid = idx;
    pump.calibStep = CALIB_SET_VOL;
    beepConfirm();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "calib_set_vol" ) == 0 ) {
    if ( pump.currentMenu != CALIBRATE )
      return errResponse( cmd, "Not in calibration menu" );
    if ( pump.calibStep != CALIB_SET_VOL && pump.calibStep != CALIB_SELECT_LIQUID )
      return errResponse( cmd, "Calib not ready for volume input" );
    float val = params["value"] | NAN;
    if ( isnan( val ) || val < 0.1 || val > 99999 )
      return errResponse( cmd, "Volume out of range ( 0.1 - 99999 )" );
    pump.calibTargetVol = constrain( val, 0.1f, 99999.0f );
    pump.calibStep = CALIB_RUN;
    beepConfirm();
    return okResponse( cmd );
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
    calibCalculate();
    pump.calibStep = CALIB_RESULT;
    beepConfirm();
    char data[128];
    snprintf( data, sizeof( data ), "{\"oldSPM\":%.1f,\"newSPM\":%.1f}", pump.stepsPerMl, pump.calibNewSPM );
    return okResponse( cmd, data );
  }

  if ( strcmp( cmd, "calib_save" ) == 0 ) {
    if ( pump.currentMenu != CALIBRATE || pump.calibStep != CALIB_RESULT )
      return errResponse( cmd, "Not at calib result step" );
    calibSave();
    pump.calibStep = CALIB_SETTINGS;
    beepDone();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "calib_abort" ) == 0 ) {
    pump.currentMenu = MAIN;
    pump.calibStep = CALIB_IDLE;
    pump.calibRunning = false;
    if ( pump.state == RUNNING ) stopPump();
    beepCancel();
    return okResponse( cmd );
  }

  if ( strcmp( cmd, "calib_settings_done" ) == 0 ) {
    if ( pump.currentMenu != CALIBRATE || pump.calibStep != CALIB_SETTINGS )
      return errResponse( cmd, "Not at calib settings step" );
    pump.currentMenu = MAIN;
    pump.calibStep = CALIB_IDLE;
    beepConfirm();
    return okResponse( cmd );
  }

  // ===================================================================
  //  楂樼骇璁剧疆
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
  //  棰勭亴 / 蹇帓
  // ===================================================================

  if ( strcmp( cmd, "prime_start" ) == 0 ) {
    if ( pump.state != STATE_IDLE ) return errResponse( cmd, "Pump not idle" );
    pump.currentMenu = PRIME;
    ensureStepperOn();
    stepper->setSpeedInHz( ( uint32_t )flowRateToPPS( 2000.0 ) );
    stepper->setAcceleration( ( int )flowRateToPPS( 2000.0 ) );
    stepper->setCurrentPosition( 0 );
    stepper->moveTo( 999999999 );  /* 杩滆秴瀹為檯, RMT 纭欢鎸佺画杩愯鐩村埌 forceStop */
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
  //  鏂规棰勮
  // ===================================================================

  if ( strcmp( cmd, "preset_load" ) == 0 ) {
    int slot = params["slot"] | -1;
    if ( slot < 0 || slot > 3 ) return errResponse( cmd, "Invalid slot ( 0-3 )" );
    if ( !isPresetValid( slot ) ) return errResponse( cmd, "Preset slot empty" );
    loadPreset( slot );
    pump.presetSlot = slot;
    pump.currentMenu = MAIN;
    beepConfirm();
    char data[64];
    snprintf( data, sizeof( data ), "{\"slot\":%d}", slot );
    return okResponse( cmd, data );
  }

  if ( strcmp( cmd, "preset_save" ) == 0 ) {
    int slot = params["slot"] | -1;
    if ( slot < 0 || slot > 3 ) return errResponse( cmd, "Invalid slot ( 0-3 )" );
    savePreset( slot );
    pump.presetSlot = slot;
    beepConfirm();
    char data[64];
    snprintf( data, sizeof( data ), "{\"slot\":%d}", slot );
    return okResponse( cmd, data );
  }

  // ===================================================================
  //  鑿滃崟 & 鏌ヨ
  // ===================================================================

  if ( strcmp( cmd, "menu_main" ) == 0 ) {
    pump.currentMenu = MAIN;
    pump.calibStep = CALIB_IDLE;
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
//                                閬ユ祴
// ============================================================================

static char* telemetryBuf = nullptr;
#define TELEMETRY_BUF_SIZE 1024

// PSRAM 鐘舵€?(姣忔閬ユ祴鍒锋柊)
static size_t psramFree = 0, psramTotal = 0;

void initTelemetryBuffer() {
  // 浼樺厛鍒嗛厤鍒?PSRAM
  telemetryBuf = (char*)heap_caps_malloc(TELEMETRY_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!telemetryBuf) {
    telemetryBuf = (char*)malloc(TELEMETRY_BUF_SIZE);  // 鍥為€€鍒板唴閮?RAM
  }
  if (telemetryBuf) telemetryBuf[0] = '\0';
}

const char* buildTelemetryJson() {
  if (!telemetryBuf) return "{}";

  // 鍒锋柊 PSRAM 缁熻
  psramTotal = ESP.getPsramSize();
  psramFree  = ESP.getFreePsram();

  // 鍐呴儴 RAM 缁熻
  size_t heapFree = ESP.getFreeHeap();
  size_t heapTotal = 327680;  // ESP32-S3 鍐呴儴 DRAM 鎬婚噺
  const char* modeStr = ( pump.mode == MODE_TIME ) ? "TIME"
                      : ( pump.mode == MODE_JET )  ? "JET" : "VOLUME";

  const char* stateStr = "IDLE";
  switch ( pump.state ) {
    case RUNNING:     stateStr = "RUNNING";     break;
    case PAUSED:      stateStr = "PAUSED";      break;
    case DONE:        stateStr = "DONE";        break;
    case ANTI_DRIP:   stateStr = "ANTI_DRIP";   break;
    case STALL_ERROR: stateStr = "STALL_ERROR"; break;
    default: break;
  }

  const char* menuStr = "MAIN";
  switch ( pump.currentMenu ) {
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

  // 杩涘害鐧惧垎姣?
  int progress = 0;
  if ( ( pump.state == RUNNING || pump.state == PAUSED || pump.state == DONE ) && pump.targetVolume > 0 ) {
    progress = ( int )( pump.dispensedVolume / pump.targetVolume * 100 );
    if ( progress > 100 ) progress = 100;
  }

  // 宸茶繍琛岀鏁?
  unsigned long elapsed = 0;
  if ( pump.state == RUNNING )
    elapsed = ( millis() - pump.pumpStartMs ) / 1000;

  // 绠¤矾瀵垮懡鐧惧垎姣?
  int tubePct = ( pump.tubeLifeML > 0 ) ? ( int )( pump.totalDispensed / pump.tubeLifeML * 100 ) : 0;

  // WiFi 鐘舵€?
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
    LIQUID_NAMES[ pump.currentLiquid ], pump.currentLiquid,
    pump.flowRate, pump.targetVolume, pump.calibTargetVol, pump.targetTime,
    pump.dispensedVolume, elapsed, progress,
    pump.totalDispensed, tubePct,
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
