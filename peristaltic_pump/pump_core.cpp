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

bool applySpeed(float pps, float accel) {
  if (!stepper || !isfinite(pps) || pps <= 0.0f) return false;
  // setSpeedInHz(0) 会被拒绝, 所以低速段统一走 millHz; 库的下限是 5 millHz
  uint32_t millihz = (uint32_t)(pps * 1000.0f);
  if (millihz < 5) millihz = 5;
  // setAcceleration() 拒绝 <= 0, 至少给 1
  int32_t a = (isfinite(accel) && accel >= 1.0f) ? (int32_t)(accel + 0.5f) : 1;
  bool ok = (stepper->setSpeedInMilliHz(millihz) == 0);
  if (stepper->setAcceleration(a) != 0) ok = false;
  if (!ok) Serial.printf("[PUMP] applySpeed rejected: pps=%.4f Hz accel=%ld\n", pps, (long)a);
  return ok;
}

void applyFlowSpeed(float mLmin) {
  float pps = flowRateToPPS(mLmin);
  applySpeed(pps, pps * ACCEL_FACTOR);
}

void updateStepperSpeed() {
  applyFlowSpeed(pump.flowRate);
}

void ensureStepperOn() {
  // ENA 始终 HIGH (DM542 不支持运行时切使能, 半流待机降温)
  pump.stepperEnabled = true;
}

void startPump() {
  if (pump.mode == MODE_TIME) {
    if (pump.targetVolume <= 0 || pump.targetTime <= 0) return;
    pump.activeFlowRate = constrain(pump.targetVolume / (pump.targetTime / 60.0), 0.1f, 1600.0f);
  } else {
    if (pump.flowRate <= 0 || pump.targetVolume <= 0) return;
    pump.activeFlowRate = pump.flowRate;
  }
  ensureStepperOn();
  applyFlowSpeed(pump.activeFlowRate);
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
  // forceStop() 只是停止追加命令, 队列里已规划的仍会走完 (库文档: 约 20ms 内停下)。
  // isRunning() 的定义含 !isQueueEmpty(), 所以轮询到 false 就是真的停稳了。
  // 不等的话 pausedRemainingSteps 会偏大 —— 1600 Hz 下 20ms ≈ 32 步 ≈ 0.13 mL。
  unsigned long t0 = millis();
  while (stepper->isRunning() && millis() - t0 < 200) delay(1);
  pump.pausedRemainingSteps = stepper->targetPos() - stepper->getCurrentPosition();
  if (pump.mode == MODE_TIME) pump.pausedElapsedSec = (millis() - pump.pumpStartMs) / 1000;
  pump_machine_transition(PAUSED);
}

void resumePump() {
  ensureStepperOn();
  applyFlowSpeed(pump.activeFlowRate);
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
  applySpeed(pps, pps * pump.jetPressure * 0.4f);
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

void calibStopRun() { pump.calibStepsRun = stepper->getCurrentPosition(); stepper->forceStop(); pump.calibRunning = false; pump_machine_transition(STATE_IDLE); }
void calibFinishRun() { pump.calibStepsRun = stepper->getCurrentPosition(); pump.calibRunning = false; pump.calibStep = CALIB_MEASURE; pump_machine_transition(DONE); }

bool calibCalculate() {
  // !(x > 0) 的写法同时挡住 NaN
  if (!(pump.calibActualVol > 0) || !(pump.calibStepsRun > 0)) return false;
  float spm = (float)pump.calibStepsRun / pump.calibActualVol;
  if (!isfinite(spm)) return false;
  pump.calibNewSPM = constrain(spm, 10.0f, 50000.0f);
  return true;
}

void calibSave() {
  // 最后一道防线: stepsPerMl 落盘后会让 flowRateToPPS() 归零、dispensedVolume 变 inf
  if (!(pump.calibNewSPM >= 10.0f)) return;
  pump.stepsPerMl = pump.calibNewSPM;
  pump.liquidSPM[pump.currentLiquid] = pump.calibNewSPM;
  markDirty();
  saveParams();
}
