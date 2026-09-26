#ifndef PUMP_STATE_H
#define PUMP_STATE_H
#include <Arduino.h>
#include "pump_shared.h"

struct PumpState {
  State     state         = STATE_IDLE;
  PumpMode  mode          = MODE_VOLUME;
  Menu      currentMenu   = MAIN;
  CalibStep calibStep     = CALIB_IDLE;
  float stepsPerMl       = 250.0;
  float flowRate         = 50.0;
  // 本次运行实际使用的流量。TIME 模式下由 体积/时间 反算得到, 不能覆写 flowRate
  // (那是用户设定值, 覆写会让 UI 显示一个用户没设过的数, 重启后又变回去)
  float activeFlowRate   = 50.0;
  float targetVolume     = 10.0;
  float dispensedVolume  = 0;
  float targetTime       = 30.0;
  unsigned long pumpStartMs  = 0;
  unsigned long pumpElapsed  = 0;
  unsigned long pumpDuration = 0;
  float jetVolume       = 1.0;
  float jetInterval     = 3.0;
  float jetFlowRate     = 200.0;
  float jetPressure     = 5.0;
  int   jetCount        = 0;
  bool  jetSquirting    = false;
  unsigned long jetWaitStart = 0;
  float liquidSPM[NUM_LIQUIDS];
  int   currentLiquid = 0;
  float antiDripVol     = 0.05;
  float totalDispensed  = 0;
  float tubeLifeML      = 50000;
  int   completionCount = 0;
  bool  calibRunning    = false;
  float calibTargetVol  = 10.0;
  // 本次校准使用的流量。刻意不用 pump.flowRate —— 校准大体积时(如 1500 mL)需要
  // 单独提速, 而改 flowRate 会污染用户的日常设定(同 A18 的思路)
  float calibFlowRate   = 50.0;
  float calibActualVol  = 0;
  long  calibStepsRun   = 0;
  float calibNewSPM     = 0;
  // 向导第 1 步选的液体只记在这里, 不动 currentLiquid / stepsPerMl; 只有 calib_save
  // 才提交 —— 中途放弃不会改掉日常的液体选择, 校准运行也用它自己的 stepsPerMl
  int   calibLiquid     = 0;
  bool          stepperEnabled      = true;
  bool eepromDirty = false;
  long          pausedRemainingSteps = 0;
  unsigned long pausedElapsedSec     = 0;
  PumpState() { for (int i = 0; i < NUM_LIQUIDS; i++) liquidSPM[i] = 250.0; }
};

extern PumpState pump;
#endif
