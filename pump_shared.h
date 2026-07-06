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
enum State    { STATE_IDLE, RUNNING, PAUSED, DONE, ANTI_DRIP };
enum Menu     { MAIN, SET_FLOW, SET_VOL, SET_TIME, CALIBRATE, PRIME,
                SET_JET_VOL, SET_JET_INTERVAL, SET_JET_FLOW, SET_JET_PRESSURE,
                SELECT_LIQUID, JET_OPTIONS, PRESET_LOAD };
enum CalibStep { CALIB_IDLE, CALIB_SELECT_LIQUID, CALIB_SET_VOL,
                 CALIB_RUN, CALIB_MEASURE, CALIB_RESULT, CALIB_SETTINGS };
enum SetEdit  { SET_NONE, SET_ANTI_DRIP, SET_TUBE_LIFE, SET_LIQUID };

// ============================================================================
//                          鍏ㄥ眬鍙橀噺 extern 澹版槑
// ============================================================================

// ----- 鐘舵€佹満 -----
extern State     pumpState;
extern State     prevPumpState;
extern PumpMode  pumpMode;
extern Menu      currentMenu;
extern CalibStep calibStep;
extern SetEdit   settingEdit;

// ----- 娉甸€佹牳蹇冨弬鏁?-----
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
// extern unsigned long lastUserActivity; // disabled
// extern bool         screensaverActive; // disabled

// ----- EEPROM -----
extern bool eepromDirty;

// ----- 鏂规棰勮 -----
extern int presetSlot;
// ----- 暂停断点 (used by pump_core.cpp) -----
extern long          pausedRemainingSteps;
extern unsigned long pausedElapsedSec;

// ----- 鏁板瓧杈撳叆缂撳啿 (灏界杩滅▼鎺у埗涓嶇洿鎺ョ敤, 澹版槑浠ヤ究瀹屾暣) -----
extern char inputBuf[8];
extern int  inputLen;

// ============================================================================
//                           Pump control function declarations
//   Implementations moved to buzzer.h / eeprom_store.h / pump_core.h.
// ============================================================================

#include "buzzer.h"
#include "eeprom_store.h"
#include "pump_core.h"
#include "led.h"

// Removed from here (now in module headers):
//   buzzer.h:       beepInput/Confirm/Cancel/Start/Pause/Done/Disable, buzzer_tick
//   eeprom_store.h: markDirty, saveParams, loadParams, isPresetValid, savePreset, loadPreset
//   pump_core.h:    flowRateToPPS, updateStepperSpeed, ensureStepperOn, checkIdleDisable,
//                   startPump, stopPump, pausePump, resumePump, resetPump,
//                   startJetSquirt/Cycle, stopJetCycle, selectLiquid,
//                   calibEnter/StartRun/StopRun/FinishRun/Calculate/Save,
//                   inputClear/Backspace/Append, inputToFloat

// 鏄剧ず
// void updateDisplay(); // OLED disabled

// 鎸夐敭澶勭悊
// void handleKey(char key); // keypad disabled

// ----- 纭欢瀵硅薄 (瀹氫箟鍦?.ino) -----
#include <AccelStepper.h>
// #include <U8g2lib.h> // OLED disabled
// #include <Keypad.h> // keypad disabled
#include <EEPROM.h>

extern AccelStepper stepper;
// extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2; // OLED disabled
// extern Keypad keypad; // keypad disabled

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



