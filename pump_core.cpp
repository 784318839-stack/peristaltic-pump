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
  digitalWrite(ENA_PIN, LOW);   // TMC2226 ENN 低有效
  pump.stepperEnabled = true;
  pump.lastStepperActivity = millis();
}

void startPump() {
  if (pump.mode == MODE_TIME) {
    if (pump.targetVolume <= 0 || pump.targetTime <= 0) return;
    float calcFlow = pump.targetVolume / (pump.targetTime / 60.0);
    pump.flowRate = constrain(calcFlow, 0.1, 1600.0);
  } else {
    if (pump.flowRate <= 0 || pump.targetVolume <= 0) return;
  }
  ensureStepperOn();
  updateStepperSpeed();
  pump.pumpDuration = (unsigned long)pump.targetTime;
  pump.pumpElapsed = 0;
  pump.pumpStartMs = millis();
  pump.dispensedVolume = 0;
  pump.sgLowCount = 0; pump.sgNextCheck = millis() + SG_CHECK_INTERVAL_MS;
  int32_t totalSteps = (int32_t)(pump.targetVolume * pump.stepsPerMl);
  stepper->setCurrentPosition(0);
  stepper->moveTo(totalSteps);
  beepStart();
  pump_machine_transition(RUNNING);
}

// 立即停止电机并精确保持位置:
// forceStopAndNewPosition() 丢弃队列中未执行的脉冲 (无 ~20ms 滑行),
// 并把位置计数器/目标精确设到 pos — 专用于暂停/停止场景。
static void stopStepperExact() {
  stepper->forceStopAndNewPosition(stepper->getCurrentPosition());
}

void stopPump() { stopStepperExact(); pump_machine_transition(STATE_IDLE); }

void pausePump() {
  // 先快照原始目标 (forceStopAndNewPosition 会覆写 target), 再立即停止
  int32_t posAtPause = stepper->getCurrentPosition();
  pump.pausedRemainingSteps = stepper->targetPos() - posAtPause;
  stepper->forceStopAndNewPosition(posAtPause);
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
  pump.currentMenu = MAIN;   // 防止预灌(PRIME)等状态下 stop 后菜单残留导致卡死
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

void stopJetCycle() { stopStepperExact(); pump.jetSquirting = false; pump_machine_transition(STATE_IDLE); }

void selectLiquid(int idx) {
  if (idx < 0 || idx >= NUM_LIQUIDS) return;
  pump.currentLiquid = idx; pump.stepsPerMl = pump.liquidSPM[idx]; markDirty();
}

void calibEnter() {
  pump.calibStep = CALIB_SELECT_LIQUID; pump.calibTargetVol = 10.0;
  pump.calibActualVol = 0; pump.calibStepsRun = 0; pump.calibNewSPM = 0;
  pump.calibRunning = false; pump.currentMenu = CALIBRATE;
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

void calibStopRun() { pump.calibStepsRun = stepper->getCurrentPosition(); stopStepperExact(); pump.calibRunning = false; pump_machine_transition(STATE_IDLE); }
void calibFinishRun() { pump.calibStepsRun = stepper->getCurrentPosition(); pump.calibRunning = false; pump.calibStep = CALIB_MEASURE; pump_machine_transition(DONE); }

void calibCalculate() {
  if (pump.calibActualVol > 0 && pump.calibStepsRun > 0) {
    pump.calibNewSPM = (float)pump.calibStepsRun / pump.calibActualVol;
    pump.calibNewSPM = constrain(pump.calibNewSPM, 10, 50000);
  }
}

void calibSave() { pump.stepsPerMl = pump.calibNewSPM; pump.liquidSPM[pump.currentLiquid] = pump.calibNewSPM; markDirty(); saveParams(); }
