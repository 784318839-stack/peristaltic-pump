/* ==========================================================================
 * 模拟后端 —— 仅为预览而存在，固件里没有这一段。
 * 状态机、命令校验范围、错误消息、遥测字段全部照 command_protocol.cpp /
 * pump_machine.cpp / pump_core.cpp 的实际语义实现，便于在浏览器里复核交互。
 * ========================================================================== */
(function () {
  var SIM_MS_PER_TICK = 100;
  var speed = 1;

  var D = {
    state: 'IDLE', mode: 'VOLUME', menu: 'MAIN',
    liquidIdx: 0, liquid: ['Wtr', 'Thk', 'Liq1', 'Liq2'],
    stepsPerMl: 250, liquidSPM: [250, 320, 250, 250],
    flowRate: 50, activeFlowRate: 50,
    targetVolume: 10, targetTime: 30,
    dispensed: 0, totalDispensed: 12340.5, tubeLifeML: 50000,
    antiDripVol: 0.05,
    jetVolume: 1, jetInterval: 3, jetFlowRate: 200, jetPressure: 5, jetCount: 0,
    calibStep: 0, calibTargetVol: 10, calibActualVol: 0,
    calibStepsRun: 0, calibNewSPM: 0, calibRunning: false,
    calibFlowRate: 50, calibLiquid: 0,
    stepperEnabled: true,
    wifiMode: 'ap', wifiIP: '192.168.4.1', wifiClients: 1,
    elapsedMs: 0, pausedElapsedMs: 0, prime: false,
    jetSquirting: false, jetWaitMs: 0,
    antiDripLeftMs: 0, antiDripCharged: 0, doneLeftMs: 0, bootMs: 0
  };

  /* 校准用的 stepsPerMl —— 与日常的 D.stepsPerMl 分开 (照抄 pump_core.cpp calibSPM()) */
  function calibSPM() { return D.liquidSPM[D.calibLiquid]; }

  /* ---- 运动曲线模型 (照 pump_core.cpp applyFlowSpeed + FastAccelStepper 的梯形规划) ----
     applyFlowSpeed() 给的加速度是 A = ACCEL_FACTOR*pps, 与目标速度成正比, 于是升到全速的
     时间 t_ramp = v/A = 1/ACCEL_FACTOR ≈ 3.33 s 是个**与速度无关的常量**, 减速段同样长。
     早先这里是理想积分器 (dispensed += flow*dt), 复现不出「定时模式的截断永远早于自然
     完成、于是系统性少出液」—— 见 pump_core.cpp startPump() 与 README 审查档案 N1。
     截断路径记多少管路寿命完全由 rampDispensed() 的返回值决定, 所以这段必须与固件同源。 */
  var ACCEL_FACTOR = 0.3;                 /* 与 pump_shared.h 的 ACCEL_FACTOR 同源 */
  var T_RAMP = 1 / ACCEL_FACTOR;          /* 3.333 s, 升速段 (= 减速段) */
  /* 2*T_RAMP ≈ 6.667 s 只是梯形/三角形的**分支点** (在该总耗时上峰值速度恰好等于指令速度),
     不是「最短耗时」。因为 A 与 v 成正比, 提速同时也提加速度, T(v) 全程单调递减, 没有下限 */
  var T_MIN  = 2 / ACCEL_FACTOR;

  /* 匀速段流量 mLmin、目标体积 total, 返回 t 秒时的实际出液量 (mL)。
     total <= 0 表示没有终点 (排气 / 喷射), 退化成匀速积分。 */
  function rampDispensed(mLmin, tSec, total) {
    var v = mLmin / 60;                                    /* mL/s */
    if (!(v > 0) || !(tSec > 0)) return 0;
    if (!(total > 0)) return v * tSec;
    var A = v / T_RAMP;                                    /* mL/s², = ACCEL_FACTOR*v */
    var vp, T;
    if (total >= v * T_RAMP) { vp = v; T = total / v + T_RAMP; }        /* 梯形: 到得了全速 */
    else { vp = Math.sqrt(total * A); T = 2 * vp / A; }                 /* 三角形: 到不了 */
    if (tSec >= T) return total;
    var tAcc = vp / A;                                     /* 梯形=T_RAMP, 三角形=T/2 */
    if (tSec <= tAcc) return 0.5 * A * tSec * tSec;
    if (tSec <= T - tAcc) return vp * (tSec - tAcc / 2);
    var r = T - tSec;
    return total - 0.5 * A * r * r;
  }

  /* 跑完 total 毫升需要的总秒数 —— 梯形是 total/v + T_RAMP, 三角形恒为 T_MIN */
  function runDurationSec(mLmin, total) {
    var v = mLmin / 60;
    if (!(v > 0) || !(total > 0)) return 0;
    var A = v / T_RAMP;
    return (total >= v * T_RAMP) ? total / v + T_RAMP : 2 * Math.sqrt(total / A);
  }

  /* ---- 日志 ---- */
  function log(cls, txt) {
    var b = document.getElementById('simLogBody');
    if (!b) return;
    var d = document.createElement('div');
    d.className = cls;
    d.textContent = txt;
    b.appendChild(d);
    while (b.childNodes.length > 120) b.removeChild(b.firstChild);
    var p = b.parentElement;
    p.scrollTop = p.scrollHeight;
  }

  /* ---- 遥测：字段与 buildTelemetryJson() 一致 ---- */
  function telemetry() {
    /* progress 照抄 buildTelemetryJson(): 只在 RUNNING/PAUSED/DONE 算, 分母在校准运行时
       用 calibTargetVol, 其余一律用 targetVolume (JET / TIME 也一样 —— 定时模式的倒计时
       是前端自己按 elapsed/targetTime 画的, 不用这个字段) */
    var prog = 0;
    if (D.state === 'RUNNING' || D.state === 'PAUSED' || D.state === 'DONE') {
      var denom = D.calibRunning ? D.calibTargetVol : D.targetVolume;
      if (denom > 0) prog = Math.min(100, Math.floor(D.dispensed / denom * 100));
    }

    return {
      type: 'telemetry', ts: Math.floor(D.bootMs / 1000),
      state: D.state, menu: D.menu, mode: D.mode,
      liquid: D.liquid[D.liquidIdx], liquidIdx: D.liquidIdx,
      flow: D.flowRate, targetVol: D.targetVolume, calibTargetVol: D.calibTargetVol,
      calibFlow: D.calibFlowRate, calibLiquid: D.calibLiquid,
      targetTime: D.targetTime,
      /* elapsed: 固件只在 RUNNING 报实时值, PAUSED 报冻结的 pumpElapsed, 其余为 0 */
      dispensed: D.dispensed,
      elapsed: (D.state === 'RUNNING' || D.state === 'PAUSED') ? Math.floor(D.elapsedMs / 1000) : 0,
      progress: Math.round(prog),
      totalDispensed: D.totalDispensed,
      tubePct: D.tubeLifeML > 0 ? Math.min(999, Math.round(D.totalDispensed / D.tubeLifeML * 100)) : 0,
      tubeLifeML: D.tubeLifeML,
      jetCount: D.jetCount, jetVolume: D.jetVolume, jetInterval: D.jetInterval,
      jetFlowRate: D.jetFlowRate, jetPressure: D.jetPressure,
      stepsPerMl: D.stepsPerMl, antiDripVol: D.antiDripVol,
      stepperEnabled: D.stepperEnabled, calibStep: D.calibStep,
      wifiMode: D.wifiMode, wifiIP: D.wifiIP, wifiClients: D.wifiClients,
      psramFree: 7936, psramTotal: 8192, heapFree: 262, heapTotal: 320
    };
  }

  /* ---- 状态机推进，按 pump_machine.cpp 的 tick_* 语义 ---- */
  function tick(dt) {
    D.bootMs += dt;
    if (D.state === 'RUNNING') {
      if (D.menu === 'PRIME') {
        D.dispensed += 1500 * dt / 60000;
        return;
      }
      if (D.calibRunning) {
        /* 校准按 calibFlowRate 跑 (calibStartRun 已把它写进 activeFlowRate),
           步数按 calibSPM() 算, 与日常的 flowRate / stepsPerMl 无关 */
        D.elapsedMs += dt;
        D.dispensed = rampDispensed(D.activeFlowRate, D.elapsedMs / 1000, D.calibTargetVol);
        if (D.dispensed >= D.calibTargetVol) {
          D.dispensed = D.calibTargetVol;
          D.calibStepsRun = Math.round(D.calibTargetVol * calibSPM());
          D.calibRunning = false;
          D.calibStep = 4;               /* CALIB_MEASURE */
          enterDone();
          log('t', '  ↳ calibFinishRun() → CALIB_MEASURE');
        }
        return;
      }
      if (D.mode === 'JET') {
        if (D.jetSquirting) {
          D.dispensed += D.jetFlowRate * dt / 60000;
          if (D.dispensed >= D.jetVolume) {
            D.jetCount++; D.dispensed = 0;
            D.totalDispensed += D.jetVolume;
            D.jetSquirting = false; D.jetWaitMs = 0;
          }
        } else {
          D.jetWaitMs += dt;
          if (D.jetWaitMs >= D.jetInterval * 1000) { D.jetSquirting = true; D.dispensed = 0; }
        }
        return;
      }
      D.elapsedMs += dt;
      D.dispensed = rampDispensed(D.activeFlowRate, D.elapsedMs / 1000, D.targetVolume);
      /* 截断条件照抄 pump_machine.cpp tick_running(): pumpElapsed >= pumpDuration + 1,
         其中 pumpElapsed 是整数秒 (millis() 差 / 1000), 所以这里也要 floor */
      var timeUp = (D.mode === 'TIME') && (Math.floor(D.elapsedMs / 1000) >= D.targetTime + 1);
      if (D.dispensed >= D.targetVolume) {
        D.dispensed = D.targetVolume;
        finishRun(D.targetVolume);                 /* 跑完全程, 记满额 */
      } else if (timeUp) {
        /* 没跑完就被截断 —— 流量被 1600 mL/min 钳住, 或 targetTime 短于 T_MIN ≈ 6.67 s 的
           三角形曲线物理下限。按实际出液量记账, 记满额会让管路寿命虚高 (审查档案 N1)。 */
        log('t', '  ↳ TIME 截断: 实际 ' + D.dispensed.toFixed(2) + ' / ' + D.targetVolume + ' mL');
        finishRun(D.dispensed);
      }
    } else if (D.state === 'ANTI_DRIP') {
      D.antiDripLeftMs -= dt;
      if (D.antiDripLeftMs <= 0) { D.totalDispensed += D.antiDripCharged; enterDone(); }
    } else if (D.state === 'DONE') {
      D.doneLeftMs -= dt;                 /* DONE_HOLD_MS = 2000 */
      if (D.doneLeftMs <= 0) { D.state = 'IDLE'; D.dispensed = 0; log('t', '  ↳ DONE → IDLE (2s 到期)'); }
    }
  }

  /* charged = 本次要记进管路寿命的毫升数。正常跑完就是 targetVolume; TIME 模式被截断时
     是实际出液量 —— 与 pump_machine.cpp tick_running() 的两条路径一一对应 */
  function finishRun(charged) {
    if (D.antiDripVol > 0) {
      D.state = 'ANTI_DRIP';
      D.antiDripCharged = charged;
      /* 回吸以 0.3× 速度反转 antiDripVol 毫升 */
      D.antiDripLeftMs = D.antiDripVol / Math.max(0.001, D.activeFlowRate * 0.3) * 60000;
      log('t', '  ↳ 完成 → ANTI_DRIP 回吸 ' + D.antiDripVol + ' mL');
    } else {
      D.totalDispensed += charged;
      enterDone();
    }
  }
  function enterDone() { D.state = 'DONE'; D.doneLeftMs = 2000; }

  setInterval(function () { tick(SIM_MS_PER_TICK * speed); }, SIM_MS_PER_TICK);

  /* ---- 命令处理：校验范围与错误消息照抄固件 ---- */
  function ok(id, data) { return data ? { type: 'response', id: id, ok: true, data: data } : { type: 'response', id: id, ok: true }; }
  function err(id, msg) { return { type: 'response', id: id, ok: false, error: msg }; }
  function rng(cmd, v, lo, hi, label) {
    if (isNaN(v)) return err(cmd, 'Missing or NaN value');
    if (v < lo || v > hi) return err(cmd, 'Value out of range ( ' + label + ' )');
    return null;
  }
  function resetPump() {
    D.dispensed = 0; D.elapsedMs = 0; D.pausedElapsedMs = 0;
    D.prime = false; D.jetSquirting = false; D.jetCount = 0; D.calibRunning = false;
  }

  function exec(cmd, p) {
    var e;
    switch (cmd) {
      case 'start':
        if (D.state !== 'IDLE' && D.state !== 'DONE') return err(cmd, 'Pump not idle');
        if (D.menu === 'CALIBRATE') return err(cmd, 'Use calib_start_run during calibration');
        if (D.mode === 'JET') {
          D.state = 'RUNNING'; D.jetSquirting = true; D.dispensed = 0; D.jetWaitMs = 0;
        } else {
          if (D.mode === 'TIME') {
            if (D.targetVolume <= 0 || D.targetTime <= 0) return err(cmd, 'Invalid target');
            /* 与 pump_core.cpp startPump() 逐字对应: 反解「总耗时正好 = targetTime」需要的
               匀速段流量。梯形支 (T >= 2*T_RAMP) v = N/(T - T_RAMP); 三角形支 v = 4*N*T_RAMP/T²。
               T 对 v 全程单调递减 (A 与 v 成正比), 所以没有「最短耗时」下限, 唯一限制是 1600 钳位;
               被钳住时由截断兜底, 并按实际出液量记账 */
            var mLmin = (D.targetTime >= 2 * T_RAMP)
                      ? D.targetVolume * 60 / (D.targetTime - T_RAMP)
                      : D.targetVolume * 240 * T_RAMP / (D.targetTime * D.targetTime);
            D.activeFlowRate = Math.min(1600, Math.max(0.1, mLmin));
            log('t', '  ↳ TIME 斜坡补偿: ' + D.activeFlowRate.toFixed(2) + ' mL/min' +
                     ' (总耗时 ' + runDurationSec(D.activeFlowRate, D.targetVolume).toFixed(2) + ' s / 目标 ' + D.targetTime + ' s)');
          } else {
            if (D.flowRate <= 0 || D.targetVolume <= 0) return err(cmd, 'Invalid target');
            D.activeFlowRate = D.flowRate;
          }
          D.dispensed = 0; D.elapsedMs = 0; D.state = 'RUNNING';
          /* 固件里 applySpeed() 会拒绝 pps<1 之外的非法值；这里模拟 A3 修复后的行为 */
          var pps = D.activeFlowRate * D.stepsPerMl / 60;
          log('t', '  ↳ applySpeed: pps=' + pps.toFixed(3) + ' Hz' + (pps < 1 ? ' (走 millHz 通道)' : ''));
        }
        return ok(cmd);
      case 'pause':
        if (D.state !== 'RUNNING') return err(cmd, 'Pump not running');
        D.pausedElapsedMs = D.elapsedMs; D.state = 'PAUSED'; return ok(cmd);
      case 'resume':
        if (D.state !== 'PAUSED') return err(cmd, 'Pump not paused');
        D.elapsedMs = D.pausedElapsedMs; D.state = 'RUNNING'; return ok(cmd);
      case 'stop': case 'reset':
        /* 校准运行中走 calibStopRun(): 记步数 + 清 calibRunning, calibStep 停在 3 可重跑 */
        if (D.calibRunning) {
          D.calibStepsRun = Math.round(D.dispensed * calibSPM());
          D.calibRunning = false;
          log('t', '  ↳ calibStopRun() → steps=' + D.calibStepsRun + ' (calibStep 仍为 3, 可重新启动)');
        }
        resetPump(); D.state = 'IDLE'; return ok(cmd);
      case 'set_mode':
        if (D.state !== 'IDLE' && D.state !== 'DONE') return err(cmd, 'Cannot change mode while running');
        if (D.menu === 'CALIBRATE') return err(cmd, 'Cannot change mode during calibration');
        if (['VOLUME', 'TIME', 'JET'].indexOf(p.m) < 0) return err(cmd, 'Invalid mode ( use VOLUME / TIME / JET )');
        D.mode = p.m; resetPump(); D.state = 'IDLE'; return ok(cmd);
      case 'set_liquid':
        if (D.menu === 'CALIBRATE') return err(cmd, 'Cannot change liquid during calibration');
        if (p.i === undefined || p.i < 0 || p.i > 3) return err(cmd, 'Invalid liquid index ( 0-3 )');
        D.liquidIdx = p.i; D.stepsPerMl = D.liquidSPM[p.i]; return ok(cmd);
      case 'set_flow':
        if (D.menu === 'CALIBRATE') return err(cmd, 'Cannot change flow during calibration');
        e = rng(cmd, p.v, 0.1, 1600, '0.1 - 1600');   if (e) return e; D.flowRate = p.v; return ok(cmd);
      case 'set_volume':
        if (D.menu === 'CALIBRATE') return err(cmd, 'Cannot change volume during calibration');
        e = rng(cmd, p.v, 0.1, 99999, '0.1 - 99999'); if (e) return e; D.targetVolume = p.v; return ok(cmd);
      case 'set_time':
        if (D.menu === 'CALIBRATE') return err(cmd, 'Cannot change time during calibration');
        e = rng(cmd, p.v, 1, 86400, '1 - 86400');     if (e) return e; D.targetTime = p.v; return ok(cmd);
      case 'set_jet_vol':    e = rng(cmd, p.v, 0.1, 10.0, '0.1 - 10.0');  if (e) return e; D.jetVolume = p.v; return ok(cmd);
      case 'set_jet_interval': e = rng(cmd, p.v, 1, 60, '1 - 60');         if (e) return e; D.jetInterval = p.v; return ok(cmd);
      case 'set_jet_flow':   e = rng(cmd, p.v, 10, 1600, '10 - 1600');    if (e) return e; D.jetFlowRate = p.v; return ok(cmd);
      case 'set_jet_pressure': e = rng(cmd, p.v, 1, 10, '1 - 10');         if (e) return e; D.jetPressure = p.v; return ok(cmd);
      case 'set_anti_drip':  e = rng(cmd, p.v, 0, 5.0, '0 - 5.0');        if (e) return e; D.antiDripVol = p.v; return ok(cmd);
      case 'set_tube_life':  e = rng(cmd, p.v, 0, 200000, '0 - 200000');  if (e) return e; D.tubeLifeML = p.v; return ok(cmd);
      /* 换管后清零累计; 不动 tubeLifeML 阈值, 也不校验状态 (固件同样不校验) */
      case 'reset_tube_life': D.totalDispensed = 0; return ok(cmd);
      case 'jet_start':
        if (D.mode !== 'JET') return err(cmd, 'Not in jet mode');
        if (D.state !== 'IDLE') return err(cmd, 'Pump not idle');
        D.state = 'RUNNING'; D.jetSquirting = true; return ok(cmd);
      case 'jet_stop':
        if (D.mode !== 'JET') return err(cmd, 'Not in jet mode');
        if (D.state !== 'RUNNING') return err(cmd, 'Jet cycle not running');
        resetPump(); D.state = 'IDLE'; return ok(cmd);
      case 'calib_enter':
        if (D.state !== 'IDLE') return err(cmd, 'Pump not idle');
        /* 校准不碰 mode / flowRate / targetVolume / liquidIdx / stepsPerMl,
           只把日常值抄进 calib* 当默认值 */
        D.calibStep = 1; D.calibTargetVol = 10; D.calibActualVol = 0;
        D.calibLiquid = D.liquidIdx; D.calibFlowRate = D.flowRate;
        D.calibStepsRun = 0; D.calibNewSPM = 0; D.calibRunning = false; D.menu = 'CALIBRATE';
        log('t', '  ↳ calibEnter(): calibLiquid=' + D.calibLiquid + ' calibFlow=' + D.calibFlowRate +
                 ' (日常 mode=' + D.mode + ' / flow=' + D.flowRate + ' 未动)');
        return ok(cmd);
      case 'calib_select_liquid':
        if (D.menu !== 'CALIBRATE' || D.calibStep !== 1) return err(cmd, 'Not at calib liquid selection step');
        if (p.i === undefined || p.i < 0 || p.i > 3) return err(cmd, 'Invalid liquid index ( 0-3 )');
        D.calibLiquid = p.i; D.calibStep = 2;
        return ok(cmd, { calibLiquid: p.i, name: D.liquid[p.i] });
      case 'calib_set_vol':
        if (D.menu !== 'CALIBRATE') return err(cmd, 'Not in calibration menu');
        if (D.calibStep !== 2 && D.calibStep !== 1) return err(cmd, 'Calib not ready for volume input');
        e = rng(cmd, p.v, 0.1, 99999, '0.1 - 99999'); if (e) return e;
        /* 可选 flow: 写 calibFlowRate, 不写 flowRate。两个值都校验通过再一起写,
           否则流量非法时报了错、体积却已经改了一半 */
        var hasFlow = (p.f !== undefined && !isNaN(p.f));
        if (hasFlow && (p.f < 0.1 || p.f > 1600)) return err(cmd, 'Calib flow out of range ( 0.1 - 1600 )');
        D.calibTargetVol = p.v;
        if (hasFlow) D.calibFlowRate = p.f;
        D.calibStep = 3;
        return ok(cmd, { calibTargetVol: D.calibTargetVol, calibFlow: D.calibFlowRate });
      case 'calib_start_run':
        if (D.menu !== 'CALIBRATE' || D.calibStep !== 3) return err(cmd, 'Not at calib run step');
        /* 不再覆写 targetVolume (那字段会落盘); activeFlowRate 供暂停后恢复速度用 */
        D.activeFlowRate = D.calibFlowRate;
        D.calibRunning = true; D.dispensed = 0; D.elapsedMs = 0; D.state = 'RUNNING';
        log('t', '  ↳ applyFlowSpeed(' + D.calibFlowRate + ' mL/min)  steps=' +
                 Math.round(D.calibTargetVol * calibSPM()) + '  (targetVolume 仍是 ' + D.targetVolume + ')');
        return ok(cmd);
      case 'calib_stop_run':
        if (!D.calibRunning) return err(cmd, 'Calib not running');
        D.calibStepsRun = Math.round(D.dispensed * calibSPM());
        D.calibRunning = false; D.calibStep = 4; resetPump(); D.state = 'IDLE'; return ok(cmd);
      case 'calib_measure':
        if (D.menu !== 'CALIBRATE' || D.calibStep !== 4) return err(cmd, 'Not at calib measure step');
        if (isNaN(p.v) || p.v <= 0 || p.v > 99999) return err(cmd, 'Measured volume out of range');
        D.calibActualVol = p.v;
        /* calibCalculate() 现在返回 bool，算不出就报错并停在 MEASURE */
        if (!(D.calibActualVol > 0) || !(D.calibStepsRun > 0)) return err(cmd, 'No steps recorded - did the motor run?');
        D.calibNewSPM = Math.min(50000, Math.max(10, D.calibStepsRun / D.calibActualVol));
        D.calibStep = 5;
        return ok(cmd, { oldSPM: calibSPM(), newSPM: D.calibNewSPM });
      case 'calib_save':
        if (D.menu !== 'CALIBRATE' || D.calibStep !== 5) return err(cmd, 'Not at calib result step');
        if (!(D.calibNewSPM >= 10)) return err(cmd, 'Invalid stepsPerMl, please recalibrate');
        /* 唯一的提交点: 到这里 calibLiquid 才成为日常液体, 新 spm 才落进 liquidSPM[] */
        D.liquidIdx = D.calibLiquid;
        D.liquidSPM[D.calibLiquid] = D.calibNewSPM;
        D.stepsPerMl = D.calibNewSPM; D.calibStep = 6;
        log('t', '  ↳ calibSave() 提交: liquidIdx=' + D.liquidIdx + ' stepsPerMl=' + D.stepsPerMl.toFixed(1) +
                 ' (mode / flowRate / targetVolume 一个都没动)');
        return ok(cmd);
      case 'calib_settings_done':
        if (D.menu !== 'CALIBRATE' || D.calibStep !== 6) return err(cmd, 'Not at calib settings step');
        D.menu = 'MAIN'; D.calibStep = 0; return ok(cmd);
      case 'calib_abort':
        D.menu = 'MAIN'; D.calibStep = 0; resetPump(); D.state = 'IDLE'; return ok(cmd);
      case 'prime_start':
        if (D.state !== 'IDLE') return err(cmd, 'Pump not idle');
        if (D.menu === 'CALIBRATE') return err(cmd, 'Cannot prime during calibration');
        D.menu = 'PRIME'; D.prime = true; D.dispensed = 0; D.state = 'RUNNING'; return ok(cmd);
      case 'prime_stop':
        if (D.menu !== 'PRIME') return err(cmd, 'Not in prime mode');
        D.prime = false; D.menu = 'MAIN'; resetPump(); D.state = 'IDLE'; return ok(cmd);
      case 'menu_main':
        /* 从校准菜单返回也统一走 calibLeave() 语义: 先停校准电机(→IDLE), 再清菜单和步骤 */
        if (D.calibRunning) {
          D.calibStepsRun = Math.round(D.dispensed * calibSPM());
          D.calibRunning = false; D.state = 'IDLE';
          log('t', '  ↳ menu_main: calibStopRun() → steps=' + D.calibStepsRun + ', calibLeave()');
        }
        D.menu = 'MAIN'; D.calibStep = 0; return ok(cmd);
      case 'get_state': return telemetry();
      case 'wifi_restart':
        D.wifiMode = 'ap'; D.wifiIP = '192.168.4.1'; return ok(cmd);
      default: return err(cmd, 'Unknown command');
    }
  }

  /* ---- WiFi 扫描：模拟 ~2.4s 的 done:false 轮询，然后给结果 ---- */
  var scanStart = 0;
  var FAKE_AP = [
    { ssid: 'CMCC-5G-2A7F', rssi: -42, secure: true },
    { ssid: 'Xiaomi_Router_8842', rssi: -58, secure: true },
    { ssid: 'TP-LINK_Guest', rssi: -67, secure: false },
    { ssid: 'HUAWEI-B315', rssi: -71, secure: true },
    { ssid: 'iPhone 热点', rssi: -79, secure: true }
  ];
  function scan() {
    /* A19: ANTI_DRIP 也要挡，否则扫描会冻住状态机 8 秒 */
    if (D.state === 'RUNNING' || D.state === 'PAUSED' || D.state === 'ANTI_DRIP') {
      return { ok: false, error: 'Pump busy', done: true, networks: [] };
    }
    if (!scanStart) scanStart = Date.now();
    if (Date.now() - scanStart < 2400) return { ok: true, done: false, networks: [] };
    scanStart = 0;
    return { ok: true, done: true, networks: FAKE_AP };
  }

  /* ---- 劫持 fetch ---- */
  window.fetch = function (url, opts) {
    opts = opts || {};
    var u = String(url);
    var body;

    if (u.indexOf('/api/status') === 0) {
      body = telemetry();
    } else if (u.indexOf('/api/scan') === 0) {
      body = scan();
    } else if (u.indexOf('/api/wifi') === 0) {
      var cfg = {};
      try { cfg = JSON.parse(opts.body || '{}'); } catch (x) { cfg = {}; }
      if (cfg.ssid) { D.wifiMode = 'sta+ap'; D.wifiIP = '192.168.31.' + (100 + (cfg.ssid.length * 7) % 150); }
      else { D.wifiMode = 'ap'; D.wifiIP = '192.168.4.1'; }
      body = { ok: true, saved: true };
      log('c', 'POST /api/wifi  ssid=' + (cfg.ssid || '(空→仅热点)'));
      log('r', '  ← ok:true saved:true  →  wifiMode=' + D.wifiMode + ' ip=' + D.wifiIP);
    } else if (u.indexOf('/api/cmd') === 0) {
      var q = u.split('?')[1] || '';
      var p = {};
      q.split('&').forEach(function (kv) {
        var i = kv.indexOf('=');
        if (i > 0) p[kv.slice(0, i)] = kv.slice(i + 1);
      });
      var cmd = p.c || '';
      var args = {};
      if (p.v !== undefined) args.v = parseFloat(p.v);
      if (p.i !== undefined) args.i = parseInt(p.i, 10);
      if (p.m !== undefined) args.m = p.m;
      if (p.s !== undefined) args.s = parseInt(p.s, 10);
      if (p.f !== undefined) args.f = parseFloat(p.f);
      body = exec(cmd, args);
      log(body.ok ? 'c' : 'e', '→ ' + cmd + (q.indexOf('&') > 0 ? '  ' + q.slice(q.indexOf('&') + 1) : ''));
      log(body.ok ? 'r' : 'e', '  ← ' + (body.ok ? 'ok' : '✗ ' + body.error) +
          (body.data ? '  data=' + JSON.stringify(body.data) : ''));
    } else {
      body = { ok: false, error: 'Not found' };
    }
    return Promise.resolve({
      ok: true, status: 200,
      json: function () { return Promise.resolve(body); },
      text: function () { return Promise.resolve(JSON.stringify(body)); }
    });
  };

  /* ---- 倍速控制 ---- */
  document.querySelectorAll('#simBar button[data-sp]').forEach(function (b) {
    b.addEventListener('click', function () {
      speed = parseInt(b.dataset.sp, 10);
      document.querySelectorAll('#simBar button[data-sp]').forEach(function (x) { x.classList.remove('on'); });
      b.classList.add('on');
      log('t', '[倍速 ' + speed + '×]');
    });
  });

  log('t', '[模拟后端已就绪 — 状态机按 pump_machine.cpp 语义仿真，命令校验范围照抄 command_protocol.cpp]');
})();
