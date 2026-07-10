/******************************************************************************
 * 蠕动泵控制器 — YZ1515 精密点液 / 喷射工作站 ( 模块化版 )
 *
 * 硬件平台 : ESP32-S3-WROOM-1-N16 ( 16 MB Flash )
 * 依赖库   : FastAccelStepper, EEPROM, ArduinoJson, NimBLE-Arduino, WiFi, ESPmDNS
 *            Adafruit NeoPixel ( WS2812 状态指示灯 )
 *
 * 功能清单 :
 *   1.  VOL 模式     — 设定流量 + 目标体积 , 恒速泵出 , 到位自动停止
 *   2.  定时模式     — 设定体积 + 时间 , 自动推算流量 , 超时停止
 *   3.  喷射模式     — 设定单次量 + 间隔时间 , 循环喷射 , 适配粘稠液体
 *   4.  暂停 / 恢复  — 运行中随时暂停 , 保存断点 , 可从断点继续
 *   5.  校准向导     — 5 步引导校准 steps/mL
 *   6.  高级设置     — 防滴回吸量 / 管路寿命 / 液体选择
 *   7.  预灌快排     — 全速运转排空管路或冲洗
 *   8.  防滴回吸     — 泵送完成后反转吸回余液 , 防止滴漏
 *   9.  匀加速曲线   — 约 0.5 秒缓启动 / 缓停止 , 避免水锤效应
 *  10.  自动关使能   — 待机 / 暂停 5 秒后电机线圈自动断电
 *  11.  非阻塞蜂鸣   — 按键 / 确认 / 取消 / 启停 / 完成各有不同音效
 *  12.  EEPROM 记忆  — 所有参数 + 累计总量 + 管路寿命掉电不丢失
 *  13.  管路寿命告警 — 累计量超过管路寿命 80% 时 WS2812 红灯闪烁
 *  14.  方案预设     — 4 个槽位 , 一键保存 / 加载全部运行参数
 *  15.  远程控制     — WiFi ( HTTP API + SoftAP ) + BLE UART + USB 串口
 *  16.  多液体校准   — 4 种液体独立 steps/mL , 切换液体自动同步
 *  17.  WS2812 指示灯 — 颜色随泵状态变化 ( 待机绿 / 运行蓝 / 暂停琥珀 / 完成绿闪 )
 *
 * 模块划分 :
 *   buzzer.h/cpp      — 非阻塞蜂鸣器驱动 ( buzzer_tick() 必须在 loop 中调用 )
 *   eeprom_store.h/cpp — EEPROM 持久化 & 方案预设
 *   pump_core.h/cpp   — 步进电机控制 / 泵状态机 / 校准 / 输入缓冲
 *   led.h/cpp         — WS2812 状态指示灯 ( led_tick() 必须在 loop 中调用 )
 *   pump_shared.h     — 枚举 / extern 声明 / 引脚常量 ( 所有模块共享 )
 *   command_protocol  — JSON 命令解析 & 遥测
 *   serial_commands   — USB CDC & 硬件 UART 串口命令
 *   wifi_manager      — SoftAP + 可选 STA 回退
 *   web_handlers      — HTTP API 服务器
 *   bluetooth_manager — BLE UART ( NUS )
 ******************************************************************************/

#include <Arduino.h>
// ===== 远程控制模块 =====
#include "command_protocol.h"
#include "serial_commands.h"
#include "wifi_manager.h"
#include "web_handlers.h"
#include "bluetooth_manager.h"

// ===== 重构后的泵控模块 =====
#include "pump_shared.h"   // 枚举 / extern / 引脚
#include "buzzer.h"        // 非阻塞蜂鸣器
#include "eeprom_store.h"  // EEPROM 读写
#include "pump_core.h"     // 步进电机 / 泵状态机 / 校准
#include "led.h"           // WS2812 状态灯

// ============================================================================
//                              全局变量定义
// ============================================================================
// ( pump_shared.h 中声明为 extern , 此处为实际定义 )

// ----- 状态机 -----
State     pumpState     = STATE_IDLE;
State     prevPumpState = STATE_IDLE;
PumpMode  pumpMode      = MODE_VOLUME;
Menu      currentMenu   = MAIN;
CalibStep calibStep     = CALIB_IDLE;

// ----- 泵送核心参数 -----
float stepsPerMl      = 250.0;  // 400 pulse/rev (细分 400)
float flowRate        = 50.0;
float targetVolume    = 10.0;
float dispensedVolume = 0;
float targetTime      = 30.0;
unsigned long pumpStartMs  = 0;
unsigned long pumpElapsed  = 0;
unsigned long pumpDuration = 0;

// ----- 喷射参数 -----
float jetVolume       = 1.0;
float jetInterval     = 3.0;
float jetFlowRate     = 200.0;
float jetPressure     = 5.0;
int   jetCount        = 0;
bool  jetSquirting    = false;
unsigned long jetWaitStart = 0;

// ----- 多液体校准 -----
const char* liquidNames[NUM_LIQUIDS] = { "Wtr", "Thk", "Org", "Cst" };
float liquidSPM[NUM_LIQUIDS] = { 250.0, 250.0, 250.0, 250.0 };
int   currentLiquid = 0;

// ----- 累计 & 管路寿命 -----
float antiDripVol     = 0.05;
float totalDispensed  = 0;
float tubeLifeML      = 50000;
int   completionCount = 0;

// ----- 校准子状态 -----
bool  calibRunning  = false;
float calibTargetVol = 10.0;
float calibActualVol = 0;
long  calibStepsRun  = 0;
float calibNewSPM    = 0;

// ----- 使能 & 超时 & 堵转检测 -----
bool         stepperEnabled      = true;
unsigned long lastStepperActivity = 0;
long          stallLastPosition    = 0;
unsigned long stallCheckTime      = 0;

// ----- 暂停断点 ( pump_core.cpp 引用 ) -----
long          pausedRemainingSteps = 0;
unsigned long pausedElapsedSec    = 0;

// ----- EEPROM & 预设 -----
bool eepromDirty = false;
int  presetSlot  = 0;

// ----- 引脚 & 常量 -----
#define IDLE_DISABLE_MS      5000   // 空闲 5 秒自动关电机使能
#define COMPLETIONS_PER_SAVE 10     // 每 10 次完成写 EEPROM

// ----- 硬件对象 -----
FastAccelStepperEngine stepperEngine;
FastAccelStepper *stepper = nullptr;

// ============================================================================
//                              初始化
// ============================================================================
void setup() {
  Serial.begin( 115200 );
  delay( 2000 );

  // Step -1 : 降频降温 (240MHz -> 160MHz, 泵控制完全够用)
  setCpuFrequencyMhz( 160 );
  Serial.printf( "[SETUP] CPU: %d MHz\n", getCpuFrequencyMhz() );

  Serial.println( "[SETUP] start" );

  // Step 0 : PSRAM 检测 & 初始化 (ESP32-S3-N16R8, 8MB Octal PSRAM)
  if ( psramFound() ) {
    size_t psramSize = ESP.getPsramSize();
    Serial.printf( "[SETUP] PSRAM: %d KB (%.1f MB)\n", psramSize / 1024, psramSize / 1048576.0 );
    Serial.printf( "[SETUP] Free PSRAM: %d KB\n", ESP.getFreePsram() / 1024 );
  } else {
    Serial.println( "[SETUP] PSRAM: NOT FOUND! Check Tools->PSRAM = OPI PSRAM" );
  }
  Serial.printf( "[SETUP] Free internal heap: %d KB\n", ESP.getFreeHeap() / 1024 );

  // Step 0b : 初始化 PSRAM 优先分配的缓冲区 (省内部 SRAM)
  initTelemetryBuffer();
  initResponseBuffer();
  initSerialBuffers();

  // Step 1 : EEPROM 加载参数
  EEPROM.begin( 512 );
  if ( !loadParams() ) saveParams();
  Serial.println( "[SETUP] eeprom ok" );

  // Step 2 : GPIO + 步进电机 (FastAccelStepper RMT 硬件脉冲, 免疫 WiFi 抖动)
  pinMode( BUZZER_PIN, OUTPUT );
  digitalWrite( BUZZER_PIN, LOW );
  pinMode( STEP_PIN, OUTPUT );
  pinMode( DIR_PIN,  OUTPUT );
  digitalWrite( STEP_PIN, LOW );
  digitalWrite( DIR_PIN,  LOW );
  pinMode( ENA_PIN, OUTPUT );
  digitalWrite( ENA_PIN, LOW );
  stepperEngine.init();
  stepper = stepperEngine.stepperConnectToPin( STEP_PIN );
  if ( stepper ) {
    stepper->setDirectionPin( DIR_PIN );
    stepper->setEnablePin( ENA_PIN );
    stepper->setAutoEnable( true );
    stepper->enableOutputs();
    stepperEnabled = true;
  }
  lastStepperActivity = millis();
  updateStepperSpeed();
  Serial.println( "[SETUP] gpio ok" );

  // Step 3 : 串口命令
  initSerialCommands();
  Serial.println( "[SETUP] serial ok" );

  initHardwareUart();
  Serial.println( "[SETUP] hw uart ok" );

  // Step 4 : BLE
  initBluetooth();
  Serial.println( "[SETUP] ble ok" );

  // Step 5 : WiFi + Web Server
  initWiFi();
  initWebServer();
  Serial.println( "[SETUP] wifi ok" );

  // Step 6 : WS2812 状态指示灯
  led_init();
  Serial.println( "[SETUP] led ok" );

  Serial.println( "[SETUP] done" );
}

// ============================================================================
//                              主循环
// ============================================================================
void loop() {
  // ---- Tick 驱动服务 ( 每帧必须调用 ) ----
  buzzer_tick();    // 非阻塞蜂鸣器状态机推进
  led_tick();       // WS2812 呼吸灯动画推进

  // ---- 远程命令处理 ----
  processSerialCommands();
  processHardwareUart();
  handleWebClients();
  handleBluetooth();
  wifiMaintain();  // STA 连接状态维护 & mDNS

  // ---- 防滴回吸状态 (RMT 硬件自运行, 只检查完成) ----
  if ( pumpState == ANTI_DRIP ) {
    lastStepperActivity = millis();
    if ( !stepper->isRunning() ) {
      pumpState = DONE;
      totalDispensed += targetVolume;
      completionCount++;
      if ( completionCount >= COMPLETIONS_PER_SAVE ) {
        markDirty(); completionCount = 0;
      }
      beepDone();
    }
  }

  // ---- 运行状态 ----
  if ( pumpState == RUNNING ) {
    lastStepperActivity = millis();
    if ( calibRunning ) {
      /* 校准运行中 — RMT 硬件自运行, 只读位置 */
      if ( !stepper->isRunning() ) {
        dispensedVolume = calibTargetVol;
        calibFinishRun();
      } else {
        dispensedVolume = ( float )stepper->getCurrentPosition() / stepsPerMl;
      }
    } else if ( currentMenu == PRIME ) {
      /* 预灌模式 : 全速无限运转 — runForward() 已启动, RMT 硬件自运行 */
      dispensedVolume = ( float )stepper->getCurrentPosition() / stepsPerMl;
    } else if ( pumpMode == MODE_JET ) {
      // 喷射模式
      if ( jetSquirting ) {
        if ( !stepper->isRunning() ) {
          jetCount++;
          dispensedVolume = jetCount * jetVolume;
          totalDispensed += jetVolume;
          completionCount++;
          if ( completionCount >= COMPLETIONS_PER_SAVE ) { markDirty(); completionCount = 0; }
          jetSquirting = false;
          jetWaitStart = millis();
        }
      } else {
        // 等待间隔中
        unsigned long elapsed = millis() - jetWaitStart;
        unsigned long intervalMs = ( unsigned long )( jetInterval * 1000 );
        if ( jetInterval > 15 && stepperEnabled ) {
          stepper->disableOutputs(); stepperEnabled = false; beepDisable();
        }
        if ( !stepperEnabled && jetInterval > 15 && elapsed >= intervalMs - 2000 ) {
          stepper->enableOutputs(); stepperEnabled = true;
        }
        if ( elapsed >= intervalMs ) startJetSquirt();
      }
    } else {
      // VOL 模式 / 定时模式
      pumpElapsed = ( millis() - pumpStartMs ) / 1000;
      if ( !stepper->isRunning() ) {
        dispensedVolume = targetVolume;
        if ( antiDripVol > 0 ) {
          // 进入防滴回吸
          stepper->setSpeedInHz( ( uint32_t )( flowRateToPPS( flowRate ) * 0.3 ) );
          stepper->setAcceleration( ( int )flowRateToPPS( flowRate ) );
          stepper->setCurrentPosition( 0 );
          stepper->moveTo( -( int32_t )( antiDripVol * stepsPerMl ) );
          pumpState = ANTI_DRIP;
        } else {
          pumpState = DONE;
          totalDispensed += targetVolume;
          completionCount++;
          if ( completionCount >= COMPLETIONS_PER_SAVE ) { markDirty(); completionCount = 0; }
        }
      } else {
        dispensedVolume = ( float )stepper->getCurrentPosition() / stepsPerMl;
        // 定时模式超时保护
        if ( pumpMode == MODE_TIME && pumpElapsed >= pumpDuration + 1 ) {
          stepper->forceStop();
          pumpState = DONE;
          totalDispensed += targetVolume;
          completionCount++;
          if ( completionCount >= COMPLETIONS_PER_SAVE ) { markDirty(); completionCount = 0; }
        }
      }
    }

    // ---- 堵转检测 (位置超时) ----
    // 跳过 JET 等待期 (jetSquirting=false 时电机不转)
    bool motorShouldMove = !( pumpMode == MODE_JET && !jetSquirting );
    if ( motorShouldMove ) {
      int32_t curPos = stepper->getCurrentPosition();
      if ( curPos != stallLastPosition ) {
        stallLastPosition = curPos;
        stallCheckTime    = millis();
      } else if ( millis() - stallCheckTime > STALL_TIMEOUT_MS ) {
        /* 3 秒位置不变 → 堵转 */
        stepper->forceStop();
        stepper->disableOutputs();
        stepperEnabled = false;
        pumpState = STALL_ERROR;
        beepCancel(); beepCancel(); beepCancel();  // 三连音报警
      }
    }
  }

  // ---- 堵转错误状态 ----
  if ( pumpState == STALL_ERROR ) {
    // LED 红色快闪由 led_update 根据状态处理
    lastStepperActivity = millis();  // 防止空闲关使能冲突
  }

  // ---- DONE 音效 & 2 秒后自动回到 IDLE ----
  if ( pumpState == DONE ) {
    static unsigned long doneAt = 0;
    if ( prevPumpState != DONE ) {
      if ( prevPumpState != ANTI_DRIP ) beepDone();  /* ANTI_DRIP 已经响过 */
      doneAt = millis();
    }
    if ( millis() - doneAt > 2000 ) {
      pumpState = STATE_IDLE;
    }
  }
  prevPumpState = pumpState;

  // ---- 空闲关使能 ----
  checkIdleDisable();

  // ---- 闲时 EEPROM 写入 ----
  if ( eepromDirty && ( pumpState == STATE_IDLE || pumpState == DONE ) ) saveParams();

  // ---- WS2812 状态同步 ----
  led_update();
}
