/******************************************************************************
 * pump_shared.h - .ino 涓?.cpp 妯″潡闂寸殑鍏变韩绫诲瀷 & extern 澹版槑
 *
 * 鏈枃浠剁敱 .ino 鍜屾墍鏈夋ā鍧?.cpp 鍏卞悓鍖呭惈
 * - 鏋氫妇瀹氫箟鍦ㄦ澶? 淇濊瘉鎵€鏈夌紪璇戝崟鍏冧竴鑷?
 * - 鍏ㄥ眬鍙橀噺鐢?extern 澹版槑, 瀹為檯瀹氫箟鍦?peristaltic_pump.ino
 ******************************************************************************/
#ifndef PUMP_SHARED_H
#define PUMP_SHARED_H

#include <Arduino.h>

// ============================================================================
//                              鏋氫妇瀹氫箟
// ============================================================================

enum PumpMode { MODE_VOLUME, MODE_TIME, MODE_JET };
enum State    { STATE_IDLE, RUNNING, PAUSED, DONE, ANTI_DRIP, STALL_ERROR };
enum Menu     { MAIN, SET_FLOW, SET_VOL, SET_TIME, CALIBRATE, PRIME,
                SET_JET_VOL, SET_JET_INTERVAL, SET_JET_FLOW, SET_JET_PRESSURE,
                SELECT_LIQUID, JET_OPTIONS, PRESET_LOAD };
enum CalibStep { CALIB_IDLE, CALIB_SELECT_LIQUID, CALIB_SET_VOL,
                 CALIB_RUN, CALIB_MEASURE, CALIB_RESULT, CALIB_SETTINGS };
// (SetEdit removed — OLED/settings editing disabled)

// ============================================================================
//                          鍏ㄥ眬鍙橀噺 extern 澹版槑
// ============================================================================

// ----- 鐘舵€佹満 -----
extern State     pumpState;
extern State     prevPumpState;
extern PumpMode  pumpMode;
extern Menu      currentMenu;
extern CalibStep calibStep;

// ----- 泵送核心参数 -----
extern float stepsPerMl;
extern float flowRate;
extern float targetVolume;
extern float dispensedVolume;
extern float targetTime;
extern unsigned long pumpStartMs;
extern unsigned long pumpElapsed;
extern unsigned long pumpDuration;

// ----- 鍠峰皠鍙傛暟 -----
extern float jetVolume;
extern float jetInterval;
extern float jetFlowRate;
extern float jetPressure;
extern int   jetCount;
extern bool  jetSquirting;
extern unsigned long jetWaitStart;

// ----- 澶氭恫浣?-----
#define NUM_LIQUIDS 4
extern const char* liquidNames[NUM_LIQUIDS];
extern float liquidSPM[NUM_LIQUIDS];
extern int   currentLiquid;

// ----- 绱 & 绠¤矾 -----
extern float antiDripVol;
extern float totalDispensed;
extern float tubeLifeML;
extern int   completionCount;

// ----- 鏍″噯瀛愮姸鎬?& 楂樼骇璁剧疆 -----
extern bool  calibRunning;
extern float calibTargetVol;
extern float calibActualVol;
extern long  calibStepsRun;
extern float calibNewSPM;

// ----- 浣胯兘 & 灞忎繚 -----
extern bool         stepperEnabled;
extern unsigned long lastStepperActivity;
extern long          stallLastPosition;
extern unsigned long stallCheckTime;
#define STALL_TIMEOUT_MS  3000

// ----- EEPROM -----
extern bool eepromDirty;

// ----- 鏂规棰勮 -----
extern int presetSlot;
// ----- 暂停断点 (used by pump_core.cpp) -----
extern long          pausedRemainingSteps;
extern unsigned long pausedElapsedSec;

// ============================================================================
//                           Pump control function declarations
//   Implementations moved to buzzer.h / eeprom_store.h / pump_core.h.
// ============================================================================

#include "buzzer.h"
#include "eeprom_store.h"
#include "pump_core.h"
#include "led.h"

// ----- 硬件对象 (定义在 .ino) -----
#include <AccelStepper.h>
#include <EEPROM.h>

extern AccelStepper stepper;

// ----- 甯搁噺 -----
#define STEP_PIN   16
#define DIR_PIN    17
#define ENA_PIN    18
#define BUZZER_PIN 5                // 无源蜂鸣器

// ----- 硬件 UART ( USB-TTL 直连 PC ) -----
#define HW_UART_RX 21
#define HW_UART_TX 47

// ----- Timing constants (used by pump_core.cpp) -----
#define IDLE_DISABLE_MS   5000       // 待机/暂停后 5 秒自动关电机使能

#endif // PUMP_SHARED_H



