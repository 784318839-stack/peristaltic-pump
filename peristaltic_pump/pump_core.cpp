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

bool applyFlowSpeed(float mLmin) {
  float pps = flowRateToPPS(mLmin);
  return applySpeed(pps, pps * ACCEL_FACTOR);
}

// moveTo() 返回 MoveResultCode (enum class : int8_t), 被拒时电机一个脉冲都不发。
// 六个调用点统一走这里, 免得各自漏掉返回值 (fas_arch/result_codes.h:66-92)
static bool moveOrLog(const char* who, int32_t steps) {
  MoveResultCode mv = stepper->moveTo(steps);
  if (moveIsOk(mv)) return true;
  Serial.printf("[PUMP] %s moveTo(%ld) rejected: %s\n", who, (long)steps, toString(mv));
  return false;
}

void updateStepperSpeed() {
  applyFlowSpeed(pump.flowRate);
}

void ensureStepperOn() {
  // ENA 始终 HIGH (DM542 不支持运行时切使能, 半流待机降温)
  pump.stepperEnabled = true;
}

bool startPump() {
  if (pump.mode == MODE_TIME) {
    if (pump.targetVolume <= 0 || pump.targetTime <= 0) return false;
    // 升速段耗时是个与速度无关的常量: applyFlowSpeed 给的加速度 A = ACCEL_FACTOR*pps,
    // 于是升到全速要 v/A = 1/ACCEL_FACTOR ≈ 3.33 s, 减速段等长。所以照 targetVolume/targetTime
    // 直接算流量, 电机要到 targetTime+3.33 s 才跑完 —— tick_running() 的截断在 targetTime+1 s
    // 就触发, 永远抢在前面, 于是系统性少出液 (实测模型: 60 s 档 -1.36%, 10 s 档 -8.17%,
    // 5 s 档 -16.3%, 3 s 档 -27.0%) 却按 targetVolume 记管路寿命。
    // 下面反解「总耗时正好等于 targetTime」需要的匀速段流量, 分两种曲线:
    //
    //   梯形 (到得了全速, targetTime >= 2*t_ramp):
    //     T = N/v + t_ramp          ⇒  v = N/(T - t_ramp)
    //   三角形 (到不了全速, targetTime < 2*t_ramp): 峰值 vp = sqrt(N*A), A = v/t_ramp
    //     T = 2*vp/A = 2*sqrt(N*t_ramp/v)  ⇒  v = 4*N*t_ramp/T²
    //
    // 两支在 T = 2*t_ramp 处连续 (都给出 v = N/t_ramp)。注意 T 对 v 全程单调递减 ——
    // 因为 A 与 v 成正比, 提速同时也提加速度, 所以**不存在**「最短耗时」这种物理下限;
    // 真正的限制只有下面 constrain 的 1600 mL/min。被钳住时截断兜底, 而截断那条路径
    // 按实际出液量记账 (见 pump_machine.cpp tick_running), 不会再虚高管路寿命。
    const float t_ramp = 1.0f / ACCEL_FACTOR;   // 3.33 s
    float mLmin = (pump.targetTime >= 2.0f * t_ramp)
                ? pump.targetVolume * 60.0f / (pump.targetTime - t_ramp)
                : pump.targetVolume * 240.0f * t_ramp / (pump.targetTime * pump.targetTime);
    pump.activeFlowRate = constrain(mLmin, 0.1f, 1600.0f);
  } else {
    if (pump.flowRate <= 0 || pump.targetVolume <= 0) return false;
    pump.activeFlowRate = pump.flowRate;
  }
  // targetVolume ≤ 99999 且 stepsPerMl ≤ 50000 ⇒ 乘积最大 5e9 > INT32_MAX (2.147e9),
  // 而超范围的 float→int32 转换是**未定义行为**, 不是回绕, 所以必须先判界再转。
  // 够得着的路径: calib_measure 只要求 val > 0, 于是「0.1 mL / 3000 步」就能把 spm 标到
  // 30000 (落在 calibCalculate 的 [10,50000] 之内), 再配 targetVolume ≥ 71583 就越界。
  // 放在动电机之前判, 免得白设一次速度还打出一条 applySpeed 日志。
  const float stepsF = pump.targetVolume * pump.stepsPerMl;
  if (!(stepsF <= 2147483647.0f)) {          // !(x <= M) 的写法同时挡住 NaN 和 inf
    Serial.printf("[PUMP] startPump rejected: %.0f steps overflows int32 (%.1f mL x %.1f /mL)\n",
                  stepsF, pump.targetVolume, pump.stepsPerMl);
    return false;
  }
  ensureStepperOn();
  if (!applyFlowSpeed(pump.activeFlowRate)) return false;   // applySpeed 已打日志
  pump.pumpDuration = (unsigned long)pump.targetTime;
  pump.pumpElapsed = 0;
  pump.pumpStartMs = millis();
  pump.dispensedVolume = 0;
  stepper->setCurrentPosition(0);
  if (!moveOrLog("startPump", (int32_t)stepsF)) return false;
  beepStart();
  pump_machine_transition(RUNNING);
  return true;
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
  // 校准运行是体积式的, 不走 TIME 模式的计时逻辑 —— 即使用户日常的 mode 就是 TIME
  if (pump.mode == MODE_TIME && !pump.calibRunning) pump.pausedElapsedSec = (millis() - pump.pumpStartMs) / 1000;
  pump_machine_transition(PAUSED);
}

bool resumePump() {
  ensureStepperOn();
  if (!applyFlowSpeed(pump.activeFlowRate)) return false;   // applySpeed 已打日志
  if (pump.mode == MODE_TIME && !pump.calibRunning) pump.pumpStartMs = millis() - pump.pausedElapsedSec * 1000;
  // 失败就别转 RUNNING: 电机不动而 isRunning()==false, tick_running() 会立刻判定跑完
  if (!moveOrLog("resumePump", stepper->getCurrentPosition() + pump.pausedRemainingSteps)) return false;
  beepStart();
  pump_machine_transition(RUNNING);
  return true;
}

void resetPump() {
  pump.dispensedVolume = 0; pump.pumpElapsed = 0;
  pump.pausedRemainingSteps = 0; pump.pausedElapsedSec = 0;
  stepper->setCurrentPosition(0);
  pump_machine_transition(STATE_IDLE);
}

bool startJetSquirt() {
  ensureStepperOn();
  float pps = pump.jetFlowRate * pump.stepsPerMl / 60.0;
  if (!applySpeed(pps, pps * pump.jetPressure * 0.4f)) return false;   // applySpeed 已打日志
  stepper->setCurrentPosition(0);
  int32_t jetSteps = (int32_t)(pump.jetVolume * pump.stepsPerMl);
  if (jetSteps < 1) jetSteps = 1;
  if (!moveOrLog("startJetSquirt", jetSteps)) return false;
  // 只在真的动起来之后置位: tick_running() 的等待分支下一轮会重试本次喷射
  pump.jetSquirting = true;
  return true;
}

bool startJetCycle() {
  pump.jetCount = 0; pump.dispensedVolume = 0;
  stepper->setCurrentPosition(0);
  ensureStepperOn();
  if (!startJetSquirt()) return false;
  beepStart();
  pump_machine_transition(RUNNING);
  return true;
}

void stopJetCycle() { stepper->forceStop(); pump.jetSquirting = false; pump_machine_transition(STATE_IDLE); }

void selectLiquid(int idx) {
  if (idx < 0 || idx >= NUM_LIQUIDS) return;
  pump.currentLiquid = idx; pump.stepsPerMl = pump.liquidSPM[idx]; markDirty();
}

void calibEnter() {
  // 校准全程不碰 pump.mode / flowRate / targetVolume / currentLiquid / stepsPerMl ——
  // 它需要的量都放在 calib* 字段里, 只有 calibSave() 才提交结果。
  // (旧做法是进向导时把 mode 强制成 VOLUME、退出时还原, 但 calib_save 在 calibLeave()
  //  之前就 markDirty()+saveParams(), 会把被强制的 VOLUME 写进 EEPROM offset 18,
  //  用户的 TIME / JET 模式就此丢失。)
  pump.calibStep = CALIB_SELECT_LIQUID; pump.calibTargetVol = 10.0;
  pump.calibLiquid = pump.currentLiquid;  // 默认沿用当前液体, 第 1 步可另选
  pump.calibFlowRate = pump.flowRate;     // 默认沿用当前流量, 第 2 步可单独改
  pump.calibActualVol = 0; pump.calibStepsRun = 0; pump.calibNewSPM = 0;
  pump.calibRunning = false; pump.currentMenu = CALIBRATE;
}

void calibLeave() {
  pump.currentMenu = MAIN;
  pump.calibStep = CALIB_IDLE;
}

float calibSPM() {
  return pump.liquidSPM[pump.calibLiquid];
}

bool calibStartRun() {
  if (pump.calibTargetVol <= 0) return false;
  // 与 startPump() 同一道判界: calibTargetVol ≤ 99999, calibSPM() ≤ 50000 ⇒ 乘积最大 5e9,
  // 超过 INT32_MAX 时 float→int32 是 UB。校准恰恰是最容易撞上的地方 —— 上一次校准要是
  // 把某个液体的 spm 标歪了 (calib_measure 只校验 val > 0), 这里就直接越界。
  const float stepsF = pump.calibTargetVol * calibSPM();
  if (!(stepsF <= 2147483647.0f)) {          // !(x <= M) 的写法同时挡住 NaN 和 inf
    Serial.printf("[PUMP] calibStartRun rejected: %.0f steps overflows int32 (%.1f mL x %.1f /mL)\n",
                  stepsF, pump.calibTargetVol, calibSPM());
    return false;
  }
  ensureStepperOn();
  // activeFlowRate 是「本次运行实际用的流量」, resumePump() 靠它恢复速度。
  // 不写的话校准中途暂停再继续, 会退回上一次普通运行的流量。
  pump.activeFlowRate = pump.calibFlowRate;
  if (!applyFlowSpeed(pump.calibFlowRate)) return false;   // applySpeed 已打日志
  pump.dispensedVolume = 0;
  // 遥测的 elapsed 是 millis() - pumpStartMs, 不设的话校准运行时会一直显示
  // 上一次普通运行(或开机)以来的秒数
  pump.pumpStartMs = millis();
  // 刻意不写 pump.targetVolume = pump.calibTargetVol。
  // targetVolume 是会落盘的(EEPROM offset 10), 而 calibSave() 会 markDirty()+saveParams()
  // —— 覆写它等于「用 1500 mL 校准一次, 用户的目标体积就被永久改成 1500」。
  // 遥测的 progress 改为在 calibRunning 时拿 calibTargetVol 当分母。
  stepper->setCurrentPosition(0);
  // 失败时 calibRunning 保持 false: 否则 tick_running() 看到 isRunning()==false 会调
  // calibFinishRun(), 拿 calibStepsRun=0 进入量杯步骤, calibCalculate() 才报错, 白走一步
  if (!moveOrLog("calibStartRun", (int32_t)stepsF)) return false;
  pump.calibRunning = true;
  pump_machine_transition(RUNNING);
  return true;
}

void calibStopRun() {
  // 顺序不能反。forceStop() 只是停止追加命令, 队列里已规划的还会走约 20ms; 先读位置的话
  // calibStepsRun 偏小, 而 calibCalculate() 拿它除以用户量到的实际体积 —— 那 20ms 的液量是
  // 真出了的, 于是 spm 系统性偏低, calibSave() 落盘后之后每次分液都少。
  // 1600 Hz 下 20ms ≈ 32 步; 校准流量 1600 mL/min + spm 250 时是 133 步 ≈ 0.53 mL,
  // 校准 10 mL 就是 -5.3%。与 pausePump() 同一套处理。
  stepper->forceStop();
  unsigned long t0 = millis();
  while (stepper->isRunning() && millis() - t0 < 200) delay(1);
  pump.calibStepsRun = stepper->getCurrentPosition();
  pump.calibRunning = false;
  pump_machine_transition(STATE_IDLE);
}
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
  // 唯一的提交点: 向导里选的液体到这里才成为日常选择
  pump.currentLiquid = pump.calibLiquid;
  pump.liquidSPM[pump.calibLiquid] = pump.calibNewSPM;
  pump.stepsPerMl = pump.calibNewSPM;
  markDirty();
  saveParams();
}
