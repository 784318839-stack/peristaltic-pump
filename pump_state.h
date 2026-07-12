#ifndef PUMP_STATE_H
#define PUMP_STATE_H
#include <Arduino.h>
#include "pump_shared.h"

struct PumpState {
  State     state         = STATE_IDLE;
  State     prevState     = STATE_IDLE;
  PumpMode  mode          = MODE_VOLUME;
  Menu      currentMenu   = MAIN;
  CalibStep calibStep     = CALIB_IDLE;
  float stepsPerMl       = 250.0;
  float flowRate         = 50.0;
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
  float calibActualVol  = 0;
  long  calibStepsRun   = 0;
  float calibNewSPM     = 0;
  bool          stepperEnabled      = true;
  unsigned long lastStepperActivity = 0;
  long          stallLastPosition   = 0;
  unsigned long stallCheckTime      = 0;
  bool eepromDirty = false;
  int  presetSlot  = 0;
  long          pausedRemainingSteps = 0;
  unsigned long pausedElapsedSec     = 0;
  PumpState() { for (int i = 0; i < NUM_LIQUIDS; i++) liquidSPM[i] = 250.0; }
};

extern PumpState pump;
#endif
