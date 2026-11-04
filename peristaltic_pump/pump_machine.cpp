#include "pump_machine.h"
#include "pump_state.h"
#include "pump_core.h"
#include "buzzer.h"
#include "eeprom_store.h"

static void tick_running();
static void tick_anti_drip();
static void tick_done();

static unsigned long done_entry_ms = 0;

static void on_entry(State newState) {
  switch (newState) {
    case RUNNING: break;
    case DONE: done_entry_ms = millis(); beepDone(); break;
    case PAUSED: beepPause(); break;
    case STATE_IDLE: pump.dispensedVolume = 0; break;
    default: break;
  }
}

void pump_machine_transition(State newState) {
  if (pump.state == newState) return;
  pump.state = newState;
  on_entry(newState);
}

void pump_machine_tick() {
  switch (pump.state) {
    case RUNNING: tick_running(); break;
    case ANTI_DRIP: tick_anti_drip(); break;
    case DONE: tick_done(); break;
    default: break;
  }
}

static void tick_running() {
  if (pump.calibRunning) {
    // 用 calibSPM() 而不是 pump.stepsPerMl: 校准的液体可能不是日常选中的那个,
    // 而 stepsPerMl 属于日常设定, 校准全程不改它
    if (!stepper->isRunning()) { pump.dispensedVolume = pump.calibTargetVol; calibFinishRun(); }
    else { pump.dispensedVolume = (float)stepper->getCurrentPosition() / calibSPM(); }
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
      // 返回值刻意不看: 失败时 jetSquirting 仍是 false, 下一个 interval 会自动重试本次喷射,
      // 比在这里报错更好 —— 喷射是周期性动作, 没有「命令发起方」可以回报
      if (elapsed >= intervalMs) startJetSquirt();
    }
    return;
  }

  pump.pumpElapsed = (millis() - pump.pumpStartMs) / 1000;

  if (!stepper->isRunning()) {
    pump.dispensedVolume = pump.targetVolume;
    // 回吸速度设失败就别进 ANTI_DRIP: tick_anti_drip() 一看 isRunning()==false 就会立刻判定
    // 回吸完成, 等于静默跳过这一步。落到下面的 DONE 分支, 行为可预期。
    if (pump.antiDripVol > 0) {
      float pps = flowRateToPPS(pump.activeFlowRate);
      int32_t backSteps = -(int32_t)(pump.antiDripVol * pump.stepsPerMl);
      if (applySpeed(pps * 0.3f, pps)) {            // 失败时 applySpeed 自己会打日志
        stepper->setCurrentPosition(0);
        if (moveIsOk(stepper->moveTo(backSteps))) {
          pump_machine_transition(ANTI_DRIP);
          return;
        }
        Serial.printf("[PUMP] anti-drip moveTo(%ld) rejected\n", (long)backSteps);
      }
    }
    pump.totalDispensed += pump.targetVolume; pump.completionCount++;
    if (pump.completionCount >= 10) { markDirty(); pump.completionCount = 0; }
    pump_machine_transition(DONE);
  } else {
    pump.dispensedVolume = (float)stepper->getCurrentPosition() / pump.stepsPerMl;
    if (pump.mode == MODE_TIME && pump.pumpElapsed >= pump.pumpDuration + 1) {
      stepper->forceStop();
      // 等真停再读数, 否则少算约 20ms 的行程 (1600 Hz 下 ≈32 步 ≈0.13 mL), 管路寿命记偏小
      unsigned long t0 = millis();
      while (stepper->isRunning() && millis() - t0 < 200) delay(1);
      // 能走到这里只剩一种情况: startPump() 反解出的流量被 constrain 钳在 1600 mL/min,
      // 电机跑不完 targetVolume。(斜坡补偿已让其余档位的总耗时正好等于 targetTime,
      // 自然完成会先于截断; 见 pump_core.cpp startPump() 的两支公式。)
      // 所以按实际出液量记账, 不是按 targetVolume, 否则管路寿命虚高 —— 500 mL/10 s 这种
      // 物理上做不到的档位, 旧代码会记 500 而实际只出了 249。
      float actual = (float)stepper->getCurrentPosition() / pump.stepsPerMl;
      if (!isfinite(actual) || actual < 0.0f) actual = 0.0f;
      if (actual > pump.targetVolume) actual = pump.targetVolume;
      pump.dispensedVolume = actual;
      pump.totalDispensed += actual;
      pump.completionCount++; if (pump.completionCount >= 10) { markDirty(); pump.completionCount = 0; }
      pump_machine_transition(DONE);
    }
  }
}

static void tick_anti_drip() {
  if (!stepper->isRunning()) { pump.totalDispensed += pump.targetVolume; pump.completionCount++; if (pump.completionCount >= 10) { markDirty(); pump.completionCount = 0; } pump_machine_transition(DONE); }
}

static void tick_done() {
  if (millis() - done_entry_ms > DONE_HOLD_MS) pump_machine_transition(STATE_IDLE);
}
