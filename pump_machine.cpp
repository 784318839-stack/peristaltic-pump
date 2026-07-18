#include "pump_machine.h"
#include "pump_state.h"
#include "pump_core.h"
#include "buzzer.h"
#include "eeprom_store.h"

static void tick_running();
static void tick_anti_drip();
static void tick_done();
static void tick_stall_error();

static void on_entry(State newState) {
  switch (newState) {
    case RUNNING: break;
    case DONE: beepDone(); break;
    case PAUSED: beepPause(); break;
    case STATE_IDLE: pump.dispensedVolume = 0; break;
    case STALL_ERROR: beepCancel(); beepCancel(); beepCancel(); break;
    default: break;
  }
}

void pump_machine_transition(State newState) {
  if (pump.state == newState) return;
  pump.prevState = pump.state;
  pump.state = newState;
  on_entry(newState);
}

void pump_machine_tick() {
  switch (pump.state) {
    case RUNNING: tick_running(); break;
    case ANTI_DRIP: tick_anti_drip(); break;
    case DONE: tick_done(); break;
    case STALL_ERROR: tick_stall_error(); break;
    default: break;
  }
}

static void tick_running() {
  pump.lastStepperActivity = millis();

  if (pump.calibRunning) {
    if (!stepper->isRunning()) { pump.dispensedVolume = pump.calibTargetVol; calibFinishRun(); }
    else { pump.dispensedVolume = (float)stepper->getCurrentPosition() / pump.stepsPerMl; }
    return;
  }

  if (pump.currentMenu == PRIME) {
    pump.dispensedVolume = (float)stepper->getCurrentPosition() / pump.stepsPerMl;
    return;
  }

  if (pump.mode == MODE_JET) {
    if (pump.jetSquirting) {
      if (!stepper->isRunning()) {
        pump.jetCount++; pump.dispensedVolume = pump.jetCount * pump.jetVolume;
        pump.totalDispensed += pump.jetVolume; pump.completionCount++;
        if (pump.completionCount >= 10) { markDirty(); pump.completionCount = 0; }
        pump.jetSquirting = false; pump.jetWaitStart = millis();
      }
    } else {
      unsigned long elapsed = millis() - pump.jetWaitStart;
      unsigned long intervalMs = (unsigned long)(pump.jetInterval * 1000);
      if (elapsed >= intervalMs) startJetSquirt();
    }
    return;
  }

  pump.pumpElapsed = (millis() - pump.pumpStartMs) / 1000;

  if (!stepper->isRunning()) {
    pump.dispensedVolume = pump.targetVolume;
    if (pump.antiDripVol > 0) {
      stepper->setSpeedInHz((uint32_t)(flowRateToPPS(pump.flowRate) * 0.3));
      stepper->setAcceleration((int)flowRateToPPS(pump.flowRate));
      stepper->setCurrentPosition(0);
      stepper->moveTo(-(int32_t)(pump.antiDripVol * pump.stepsPerMl));
      pump_machine_transition(ANTI_DRIP);
    } else {
      pump.totalDispensed += pump.targetVolume; pump.completionCount++;
      if (pump.completionCount >= 10) { markDirty(); pump.completionCount = 0; }
      pump_machine_transition(DONE);
    }
  } else {
    pump.dispensedVolume = (float)stepper->getCurrentPosition() / pump.stepsPerMl;
    if (pump.mode == MODE_TIME && pump.pumpElapsed >= pump.pumpDuration + 1) {
      stepper->forceStop(); pump.totalDispensed += pump.targetVolume;
      pump.completionCount++; if (pump.completionCount >= 10) { markDirty(); pump.completionCount = 0; }
      pump_machine_transition(DONE);
    }
  }

  int32_t curPos = stepper->getCurrentPosition();
  if (curPos != pump.stallLastPosition) { pump.stallLastPosition = curPos; pump.stallCheckTime = millis(); }
  else if (millis() - pump.stallCheckTime > STALL_TIMEOUT_MS) { stepper->forceStop(); pump.stepperEnabled = false; pump_machine_transition(STALL_ERROR); }
}

static void tick_anti_drip() {
  pump.lastStepperActivity = millis();
  if (!stepper->isRunning()) { pump.totalDispensed += pump.targetVolume; pump.completionCount++; if (pump.completionCount >= 10) { markDirty(); pump.completionCount = 0; } pump_machine_transition(DONE); }
}

static unsigned long done_entry_ms = 0;
static void tick_done() {
  pump.lastStepperActivity = millis();
  if (pump.prevState != DONE) { done_entry_ms = millis(); pump.prevState = DONE; }
  if (millis() - done_entry_ms > 2000) pump_machine_transition(STATE_IDLE);
}

static void tick_stall_error() { pump.lastStepperActivity = millis(); }
