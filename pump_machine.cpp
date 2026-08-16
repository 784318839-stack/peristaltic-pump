#include "pump_machine.h"
#include "pump_state.h"
#include "pump_core.h"
#include "buzzer.h"
#include "eeprom_store.h"
#include "tmc2226.h"

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
      // 长间隔节能: 等待 2s 后断电, 下次喷射前 2s 重新上电
      if (pump.jetInterval > 15.0) {
        if (pump.stepperEnabled && elapsed >= JET_OFF_DELAY_MS) {
          digitalWrite(ENA_PIN, HIGH); pump.stepperEnabled = false;
        } else if (!pump.stepperEnabled && elapsed + JET_OFF_DELAY_MS >= intervalMs) {
          ensureStepperOn();
        }
      }
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
      stepper->forceStopAndNewPosition(stepper->getCurrentPosition()); pump.totalDispensed += pump.targetVolume;
      pump.completionCount++; if (pump.completionCount >= 10) { markDirty(); pump.completionCount = 0; }
      pump_machine_transition(DONE);
    }

    // StallGuard 硬件堵转检测 (仅 StealthChop 速度区间, SG_RESULT 才有效)
    // 注意: 通信失败 (read_ex=false) 时跳过且不计数, 避免误判堵转
    if (millis() >= pump.sgNextCheck) {
      pump.sgNextCheck = millis() + SG_CHECK_INTERVAL_MS;
      uint32_t pps = (uint32_t)flowRateToPPS(pump.flowRate);
      if (pps <= SG_MAX_PPS) {
        uint32_t sg = 0;
        if (tmc2226_read_ex(TMC_REG_SG_RESULT, &sg)) {
          sg &= 0x3FF;
          if (sg <= SG_STALL_THRESHOLD) {
            if (++pump.sgLowCount >= SG_STALL_CONSECUTIVE) {
              Serial.printf("[STALL] SG_RESULT=%lu, stopping\n", (unsigned long)sg);
              stepper->forceStopAndNewPosition(stepper->getCurrentPosition());
              digitalWrite(ENA_PIN, HIGH); pump.stepperEnabled = false;
              pump_machine_transition(STALL_ERROR);
            }
          } else {
            pump.sgLowCount = 0;
          }
        }
      } else {
        pump.sgLowCount = 0;
      }
    }
  }
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
