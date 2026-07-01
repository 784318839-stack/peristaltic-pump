/******************************************************************************
 * pump_shared.h - .ino 与 .cpp 模块间的共享类型 & extern 声明
 *
 * 本文件由 .ino 和所有模块 .cpp 共同包含
 * - 枚举定义在此处, 保证所有编译单元一致
 * - 全局变量用 extern 声明, 实际定义在 peristaltic_pump.ino
 ******************************************************************************/
#ifndef PUMP_SHARED_H
#define PUMP_SHARED_H

#include <Arduino.h>

// ============================================================================
//                              枚举定义
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
//                          全局变量 extern 声明
// ============================================================================

// ----- 状态机 -----
extern State     pumpState;
extern State     prevPumpState;
extern PumpMode  pumpMode;
extern Menu      currentMenu;
extern CalibStep calibStep;
extern SetEdit   settingEdit;

// ----- 泵送核心参数 -----
extern float stepsPerMl;
extern float flowRate;
extern float targetVolume;
extern float dispensedVolume;
extern float targetTime;
extern unsigned long pumpStartMs;
extern unsigned long pumpElapsed;
extern unsigned long pumpDuration;

// ----- 喷射参数 -----
extern float jetVolume;
extern float jetInterval;
extern float jetFlowRate;
extern float jetPressure;
extern int   jetCount;
extern bool  jetSquirting;
extern unsigned long jetWaitStart;

// ----- 多液体 -----
#define NUM_LIQUIDS 4
extern const char* liquidNames[NUM_LIQUIDS];
extern float liquidSPM[NUM_LIQUIDS];
extern int   currentLiquid;

// ----- 累计 & 管路 -----
extern float antiDripVol;
extern float totalDispensed;
extern float tubeLifeML;
extern int   completionCount;

// ----- 校准子状态 & 高级设置 -----
extern bool  calibRunning;
extern float calibTargetVol;
extern float calibActualVol;
extern long  calibStepsRun;
extern float calibNewSPM;

// ----- 使能 & 屏保 -----
extern bool         stepperEnabled;
extern unsigned long lastStepperActivity;
extern unsigned long lastUserActivity;
extern bool         screensaverActive;

// ----- EEPROM -----
extern bool eepromDirty;

// ----- 方案预设 -----
extern int presetSlot;

// ----- 数字输入缓冲 (尽管远程控制不直接用, 声明以便完整) -----
extern char inputBuf[8];
extern int  inputLen;

// ============================================================================
//                           泵控制函数声明
// ============================================================================

void startPump();
void pausePump();
void resumePump();
void resetPump();
void stopPump();
void startJetSquirt();
void startJetCycle();
void stopJetCycle();
void selectLiquid(int idx);
void calibEnter();
void calibStartRun();
void calibStopRun();
void calibFinishRun();
void calibCalculate();
void calibSave();
void updateStepperSpeed();
void ensureStepperOn();
void checkIdleDisable();
void markDirty();
void saveParams();
bool loadParams();
bool isPresetValid(int slot);
void savePreset(int slot);
void loadPreset(int slot);
float flowRateToPPS(float mLmin);

// 蜂鸣器
void beepInput();
void beepConfirm();
void beepCancel();
void beepStart();
void beepPause();
void beepDone();
void beepDisable();
void buzz(int freq, int ms);

// 显示
void updateDisplay();

// 按键处理
void handleKey(char key);

// ----- 硬件对象 (定义在 .ino) -----
#include <AccelStepper.h>
#include <U8g2lib.h>
#include <Keypad.h>
#include <EEPROM.h>

extern AccelStepper stepper;
extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2;
extern Keypad keypad;

// ----- 常量 -----
#define STEP_PIN   16
#define DIR_PIN    17
#define ENA_PIN    18
#define BUZZER_PIN 5

#endif // PUMP_SHARED_H
