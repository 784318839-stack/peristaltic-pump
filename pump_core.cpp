// pump_core.cpp - Stepper motor control, pump state machine, calibration
#include "pump_core.h"
#include "pump_state.h"
#include "pump_shared.h"
#include "pump_machine.h"
#include "buzzer.h"
#include "eeprom_store.h"

float flowRateToPPS(float mLmin) {
  return mLmin * pump.stepsPerMl / 60.0;
}

void updateStepperSpeed() {
  float pps = flowRateToPPS(pump.flowRate);
  stepper->setSpeedInHz((uint32_t)pps);
  stepper->setAcceleration((int)(pps * ACCEL_FACTOR));
}

void ensureStepperOn() {
  if (!pump.stepperEnabled) {
    stepper->enableOutputs();
    pump.stepperEnabled = true;
  }
  pump.lastStepperActivity = millis();
}

void checkIdleDisable() {
  if (pump.stepperEnabled
      && (pump.state == STATE_IDLE || pump.state == PAUSED)
      && millis() - pump.lastStepperActivity > IDLE_DISABLE_MS) {
    stepper->disableOutputs();
    pump.stepperEnabled = false;
    beepDisable();
  }
}

void startPump() {
  if (pump.mode == MODE_TIME) {
    if (pump.targetVolume <= 0 || pump.targetTime <= 0) return;
    float calcFlow = pump.targetVolume / (pump.targetTime / 60.0);
    pump.flowRate = constrain(calcFlow, 0.1, 2000.0);
  } else {
    if (pump.flowRate <= 0 || pump.targetVolume <= 0) return;
  }
  ensureStepperOn();
  updateStepperSpeed();
  pump.pumpDuration = (unsigned long)pump.targetTime;
  pump.pumpElapsed = 0;
  pump.pumpStartMs = millis();
  pump.dispensedVolume = 0;
  int32_t totalSteps = (int32_t)(pump.targetVolume * pump.stepsPerMl);
  stepper->setCurrentPosition(0);
  stepper->moveTo(totalSteps);
  beepStart();
  pump_machine_transition(RUNNING);
}

void stopPump() { stepper->forceStop(); pump_machine_transition(STATE_IDLE); }

void pausePump() {
  stepper->forceStop();
  pump.pausedRemainingSteps = stepper->targetPos() - stepper->getCurrentPosition();
  if (pump.mode == MODE_TIME) pump.pausedElapsedSec = (millis() - pump.pumpStartMs) / 1000;
  pump_machine_transition(PAUSED);
}

void resumePump() {
  ensureStepperOn();
  updateStepperSpeed();
  beepStart();
  if (pump.mode == MODE_TIME) pump.pumpStartMs = millis() - pump.pausedElapsedSec * 1000;
  stepper->moveTo(stepper->getCurrentPosition() + pump.pausedRemainingSteps);
  pump_machine_transition(RUNNING);
}

void resetPump() {
  pump.dispensedVolume = 0; pump.pumpElapsed = 0;
  pump.pausedRemainingSteps = 0; pump.pausedElapsedSec = 0;
  stepper->setCurrentPosition(0);
  pump_machine_transition(STATE_IDLE);
}

void startJetSquirt() {
  ensureStepperOn();
  float pps = pump.jetFlowRate * pump.stepsPerMl / 60.0;
  stepper->setSpeedInHz((uint32_t)pps);
  stepper->setAcceleration((int)(pps * pump.jetPressure * 0.4));
  stepper->setCurrentPosition(0);
  int32_t jetSteps = (int32_t)(pump.jetVolume * pump.stepsPerMl);
  if (jetSteps < 1) jetSteps = 1;
  stepper->moveTo(jetSteps);
  pump.jetSquirting = true;
}

void startJetCycle() {
  pump.jetCount = 0; pump.dispensedVolume = 0;
  stepper->setCurrentPosition(0);
  ensureStepperOn(); startJetSquirt();
  beepStart();
  pump_machine_transition(RUNNING);
}

void stopJetCycle() { stepper->forceStop(); pump.jetSquirting = false; pump_machine_transition(STATE_IDLE); }

void selectLiquid(int idx) {
  if (idx < 0 || idx >= NUM_LIQUIDS) return;
  pump.currentLiquid = idx; pump.stepsPerMl = pump.liquidSPM[idx]; markDirty();
}

void calibEnter() {
  pump.calibStep = CALIB_SELECT_LIQUID; pump.calibTargetVol = 10.0;
  pump.calibActualVol = 0; pump.calibStepsRun = 0; pump.calibNewSPM = 0;
  pump.calibRunning = false; inputClear(); pump.currentMenu = CALIBRATE;
}

void calibStartRun() {
  if (pump.calibTargetVol <= 0) return;
  ensureStepperOn(); updateStepperSpeed();
  pump.dispensedVolume = 0;
  pump.targetVolume = pump.calibTargetVol;  // 让遥测 progress 反映校准进度
  int32_t totalSteps = (int32_t)(pump.calibTargetVol * pump.stepsPerMl);
  stepper->setCurrentPosition(0); stepper->moveTo(totalSteps);
  pump.calibRunning = true;
  pump_machine_transition(RUNNING);
}

void calibStopRun() { pump.calibStepsRun = stepper->getCurrentPosition(); stepper->forceStop(); pump.calibRunning = false; pump_machine_transition(STATE_IDLE); }
void calibFinishRun() { pump.calibStepsRun = stepper->getCurrentPosition(); pump.calibRunning = false; pump_machine_transition(DONE); }

void calibCalculate() {
  if (pump.calibActualVol > 0 && pump.calibStepsRun > 0) {
    pump.calibNewSPM = (float)pump.calibStepsRun / pump.calibActualVol;
    pump.calibNewSPM = constrain(pump.calibNewSPM, 10, 50000);
  }
}

void calibSave() { pump.stepsPerMl = pump.calibNewSPM; pump.liquidSPM[pump.currentLiquid] = pump.calibNewSPM; markDirty(); saveParams(); }

static char inputBuf[8] = "";
static int  inputLen = 0;
void inputClear() { memset(inputBuf, 0, sizeof(inputBuf)); inputLen = 0; }
void inputBackspace() { if (inputLen > 0) inputBuf[--inputLen] = 0; }
void inputAppend(char c) { if (inputLen < 6) inputBuf[inputLen++] = c; }
float inputToFloat() { return (inputLen == 0) ? 0 : atof(inputBuf); }
