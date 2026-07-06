// pump_core.cpp - Stepper motor control, pump state machine, calibration
#include "pump_core.h"
#include "pump_shared.h"
#include "buzzer.h"
#include "eeprom_store.h"
#include <AccelStepper.h>

// ----- Flow-to-pulse conversion -----
float flowRateToPPS(float mLmin) {
  return mLmin * stepsPerMl / 60.0;
}

// ----- Stepper speed update -----
void updateStepperSpeed() {
  float pps = flowRateToPPS(flowRate);
  stepper.setMaxSpeed(pps);
  stepper.setSpeed(0);
  stepper.setAcceleration(pps * 2);
}

// ----- Enable management -----
void ensureStepperOn() {
  if (!stepperEnabled) {
    stepper.enableOutputs();
    stepperEnabled = true;
  }
  lastStepperActivity = millis();
}

void checkIdleDisable() {
  if (stepperEnabled
      && (pumpState == STATE_IDLE || pumpState == PAUSED)
      && millis() - lastStepperActivity > IDLE_DISABLE_MS) {
    stepper.disableOutputs();
    stepperEnabled = false;
    beepDisable();
  }
}

// ----- Pump state machine -----
void startPump() {
  if (pumpMode == MODE_TIME) {
    if (targetVolume <= 0 || targetTime <= 0) return;
    float calcFlow = targetVolume / (targetTime / 60.0);
    flowRate = constrain(calcFlow, 0.1, 2000.0);
  } else {
    if (flowRate <= 0 || targetVolume <= 0) return;
  }

  ensureStepperOn();
  updateStepperSpeed();

  pumpDuration = (unsigned long)targetTime;
  pumpElapsed  = 0;
  pumpStartMs  = millis();
  dispensedVolume = 0;

  long totalSteps = targetVolume * stepsPerMl;
  stepper.setCurrentPosition(0);
  stepper.moveTo(totalSteps);

  beepStart();
  pumpState = RUNNING;
}

void stopPump() {
  stepper.stop();
  pumpState = STATE_IDLE;
}

void pausePump() {
  stepper.stop();
  pausedRemainingSteps = stepper.distanceToGo();
  if (pumpMode == MODE_TIME) {
    pausedElapsedSec = (millis() - pumpStartMs) / 1000;
  }
  beepPause();
  pumpState = PAUSED;
}

void resumePump() {
  ensureStepperOn();
  updateStepperSpeed();
  beepStart();
  if (pumpMode == MODE_TIME) {
    pumpStartMs = millis() - pausedElapsedSec * 1000;
  }
  stepper.moveTo(stepper.currentPosition() + pausedRemainingSteps);
  pumpState = RUNNING;
}

void resetPump() {
  dispensedVolume = 0;
  pumpElapsed     = 0;
  pausedRemainingSteps = 0;
  pausedElapsedSec     = 0;
  stepper.setCurrentPosition(0);
  pumpState = STATE_IDLE;
}

// ----- JET mode -----
void startJetSquirt() {
  ensureStepperOn();
  float pps = jetFlowRate * stepsPerMl / 60.0;
  stepper.setMaxSpeed(pps);
  stepper.setSpeed(0);
  stepper.setAcceleration(pps * jetPressure * 0.4);
  stepper.setCurrentPosition(0);
  long jetSteps = jetVolume * stepsPerMl;
  if (jetSteps < 1) jetSteps = 1;
  stepper.moveTo(jetSteps);
  jetSquirting = true;
}

void startJetCycle() {
  jetCount = 0;
  dispensedVolume = 0;
  stepper.setCurrentPosition(0);
  ensureStepperOn();
  startJetSquirt();
  beepStart();
  pumpState = RUNNING;
}

void stopJetCycle() {
  stepper.stop();
  jetSquirting = false;
  pumpState = STATE_IDLE;
}

void selectLiquid(int idx) {
  if (idx < 0 || idx >= NUM_LIQUIDS) return;
  currentLiquid = idx;
  stepsPerMl = liquidSPM[idx];
  markDirty();
}

// ----- Calibration -----
void calibEnter() {
  calibStep       = CALIB_SELECT_LIQUID;
  calibTargetVol  = 10.0;
  calibActualVol  = 0;
  calibStepsRun   = 0;
  calibNewSPM     = 0;
  calibRunning    = false;
  inputClear();
  currentMenu     = CALIBRATE;
}

void calibStartRun() {
  if (calibTargetVol <= 0) return;
  ensureStepperOn();
  updateStepperSpeed();
  dispensedVolume = 0;
  long totalSteps = calibTargetVol * stepsPerMl;
  stepper.setCurrentPosition(0);
  stepper.moveTo(totalSteps);
  calibRunning = true;
  pumpState = RUNNING;
}

void calibStopRun() {
  calibStepsRun = stepper.currentPosition();
  stepper.stop();
  calibRunning = false;
  pumpState = STATE_IDLE;
}

void calibFinishRun() {
  calibStepsRun = stepper.currentPosition();
  calibRunning = false;
  pumpState = DONE;
}

void calibCalculate() {
  if (calibActualVol > 0 && calibStepsRun > 0) {
    calibNewSPM = (float)calibStepsRun / calibActualVol;
    calibNewSPM = constrain(calibNewSPM, 10, 50000);
  }
}

void calibSave() {
  stepsPerMl = calibNewSPM;
  liquidSPM[currentLiquid] = calibNewSPM;
  markDirty();
  saveParams();
}

// ----- Input buffer (shared across all parameter-entry screens) -----
char inputBuf[8] = "";
int  inputLen    = 0;

void inputClear() {
  memset(inputBuf, 0, sizeof(inputBuf));
  inputLen = 0;
}

void inputBackspace() {
  if (inputLen > 0) {
    inputBuf[--inputLen] = 0;
  }
}

void inputAppend(char c) {
  if (inputLen < 6) {
    inputBuf[inputLen++] = c;
  }
}

float inputToFloat() {
  if (inputLen == 0) return 0;
  return atof(inputBuf);
}
