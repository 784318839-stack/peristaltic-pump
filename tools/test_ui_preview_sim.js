/* 无头驱动 tools/ui_preview_sim.js（预览用的模拟后端），断言它与固件语义一致。
 *
 *   node tools/test_ui_preview_sim.js
 *
 * 退出码 0 = 全部通过，非 0 = 有断言失败。
 * 模拟后端是照 command_protocol.cpp / pump_machine.cpp / pump_core.cpp 手写的，
 * 改了固件的命令校验、校准流程或遥测字段，就要同步改它并让这里继续通过。
 */
const fs = require('fs');
const nodePath = require('path');

const TOOLS = __dirname;
const ROOT = nodePath.join(TOOLS, '..');
/* 本仓库 core.autocrlf=true，读进来先统一成 \n 再做正则和比对 */
const read = (p) => fs.readFileSync(p, 'utf8').replace(/\r\n/g, '\n');
let sim = read(nodePath.join(TOOLS, 'ui_preview_sim.js')).trim();

/* 前端 JS 只做语法检查 —— 它逐字来自 index.html，实际观感要在浏览器里看 */
const indexHtml = read(nodePath.join(ROOT, 'peristaltic_pump', 'index.html'));
const uiBlocks = [...indexHtml.matchAll(/<script>\n([\s\S]*?)<\/script>/g)].map(m => m[1]);
if (uiBlocks.length !== 1) {
  console.error(`index.html: expected exactly 1 script block, got ${uiBlocks.length}`);
  process.exit(1);
}
const ui = uiBlocks[0];

/* expose internals + stop the wall-clock ticker so time is driven deterministically */
const tail = '})();';
if (!sim.endsWith(tail)) { console.error('sim IIFE tail not found'); process.exit(1); }
sim = sim.slice(0, -tail.length) + 'window.__sim = { D: D, tick: tick, exec: exec, telemetry: telemetry }; })();';

const logs = [];
global.document = {
  getElementById: (id) => id === 'simLogBody' ? {
    appendChild: (n) => logs.push(n.textContent),
    childNodes: { length: 0 },
    parentElement: { scrollTop: 0, scrollHeight: 0 },
  } : null,
  createElement: () => ({ className: '', textContent: '' }),
  querySelectorAll: () => [],
};
global.setInterval = () => 0;
global.window = global;

/* syntax check the firmware UI script (it is copied verbatim from index.html) */
new (require('vm').Script)(ui, { filename: 'index.html:UI' });

eval(sim);
const S = global.__sim;

let pass = 0;
const fails = [];
function ck(name, cond, extra) {
  if (cond) { pass++; }
  else { fails.push(name + (extra !== undefined ? '  →  ' + JSON.stringify(extra) : '')); }
}
function cmd(c, args) { return S.exec(c, args || {}); }
function advance(ms) { for (let t = 0; t < ms / 100; t++) S.tick(100); }
function snap() {
  const d = S.D;
  return { mode: d.mode, flowRate: d.flowRate, targetVolume: d.targetVolume, targetTime: d.targetTime, liquidIdx: d.liquidIdx, stepsPerMl: d.stepsPerMl, spm: d.liquidSPM.slice() };
}

/* ---------- A. daily settings snapshot, TIME mode ---------- */
ck('set_mode TIME', cmd('set_mode', { m: 'TIME' }).ok);
ck('set_flow 123', cmd('set_flow', { v: 123 }).ok);
ck('set_volume 45', cmd('set_volume', { v: 45 }).ok);
ck('set_time 60', cmd('set_time', { v: 60 }).ok);
ck('set_liquid 2', cmd('set_liquid', { i: 2 }).ok);
const before = snap();
ck('baseline mode=TIME', before.mode === 'TIME', before);
ck('baseline spm synced to liquid 2', before.stepsPerMl === before.spm[2], before);

/* ---------- B. TIME-mode countdown inputs ---------- */
cmd('set_mode', { m: 'TIME' });
cmd('start');
advance(10000);
let t = S.telemetry();
ck('TIME running elapsed≈10s', t.elapsed === 10, t.elapsed);
ck('TIME mode unchanged while running', t.mode === 'TIME', t.mode);
ck('TIME progress is volume based (firmware formula)',
   t.progress === Math.min(100, Math.floor(t.dispensed / t.targetVol * 100)), { p: t.progress, disp: t.dispensed, tv: t.targetVol });
cmd('pause');
const pausedElapsed = S.telemetry().elapsed;
advance(5000);
ck('elapsed frozen while PAUSED (countdown must not jump back)',
   S.telemetry().elapsed === pausedElapsed, { pausedElapsed, now: S.telemetry().elapsed });
cmd('resume');
advance(2000);
ck('elapsed resumes from the frozen value', S.telemetry().elapsed === pausedElapsed + 2, S.telemetry().elapsed);
cmd('stop');
ck('elapsed is 0 when IDLE', S.telemetry().elapsed === 0, S.telemetry().elapsed);
ck('daily settings untouched by a TIME run', JSON.stringify(snap()) === JSON.stringify(before), snap());

/* ---------- C. calibration must not touch daily settings ---------- */
ck('calib_enter', cmd('calib_enter').ok);
ck('calibEnter leaves daily settings alone', JSON.stringify(snap()) === JSON.stringify(before), snap());
ck('calib defaults copied from daily', S.D.calibLiquid === before.liquidIdx && S.D.calibFlowRate === before.flowRate,
   { calibLiquid: S.D.calibLiquid, calibFlowRate: S.D.calibFlowRate });

const sel = cmd('calib_select_liquid', { i: 1 });
ck('calib_select_liquid ok', sel.ok, sel);
ck('calib_select_liquid returns data', sel.data && sel.data.calibLiquid === 1, sel.data);
ck('daily liquidIdx NOT changed by the wizard', S.D.liquidIdx === before.liquidIdx, S.D.liquidIdx);
ck('daily stepsPerMl NOT changed by the wizard', S.D.stepsPerMl === before.stepsPerMl, S.D.stepsPerMl);
ck('calibLiquid = 1', S.D.calibLiquid === 1, S.D.calibLiquid);

/* flow out of range — 必须排在成功那次之前: 成功后 calibStep 前进到 3 就不收了 */
const badFlow = cmd('calib_set_vol', { v: 100, f: 5000 });
ck('calib flow >1600 rejected', !badFlow.ok && /Calib flow out of range/.test(badFlow.error), badFlow);
ck('rejected flow leaves calibTargetVol untouched (validate-then-write)',
   S.D.calibTargetVol === 10, S.D.calibTargetVol);
ck('rejected flow leaves calibStep at 2', S.D.calibStep === 2, S.D.calibStep);

const sv = cmd('calib_set_vol', { v: 100, f: 800 });
ck('calib_set_vol ok', sv.ok, sv);
ck('calib_set_vol echoes settings', sv.data && sv.data.calibTargetVol === 100 && sv.data.calibFlow === 800, sv.data);
ck('calibTargetVol=100', S.D.calibTargetVol === 100, S.D.calibTargetVol);
ck('calibFlowRate=800', S.D.calibFlowRate === 800, S.D.calibFlowRate);
ck('daily flowRate NOT changed', S.D.flowRate === before.flowRate, S.D.flowRate);
ck('daily targetVolume NOT changed', S.D.targetVolume === before.targetVolume, S.D.targetVolume);

/* everything the main panel would send is rejected during CALIBRATE */
const locked = [
  ['set_mode', { m: 'VOLUME' }, /Cannot change mode during calibration/],
  ['set_flow', { v: 50 }, /Cannot change flow during calibration/],
  ['set_volume', { v: 10 }, /Cannot change volume during calibration/],
  ['set_time', { v: 30 }, /Cannot change time during calibration/],
  ['set_liquid', { i: 0 }, /Cannot change liquid during calibration/],
  ['start', {}, /Use calib_start_run during calibration/],
  ['prime_start', {}, /Cannot prime during calibration/],
];
for (const [c, a, re] of locked) {
  const r = cmd(c, a);
  ck(`${c} rejected during calibration`, !r.ok && re.test(r.error || ''), r);
}
ck('daily settings still intact after the rejected commands', JSON.stringify(snap()) === JSON.stringify(before), snap());

/* ---------- D. calibration run ---------- */
ck('calib_start_run', cmd('calib_start_run').ok);
ck('calib run does NOT overwrite targetVolume', S.D.targetVolume === before.targetVolume, S.D.targetVolume);
ck('activeFlowRate = calibFlowRate', S.D.activeFlowRate === 800, S.D.activeFlowRate);
ck('state RUNNING', S.D.state === 'RUNNING', S.D.state);
t = S.telemetry();
ck('telemetry mode still TIME during calibration', t.mode === 'TIME', t.mode);
ck('telemetry calibFlow=800', t.calibFlow === 800, t.calibFlow);
ck('telemetry calibLiquid=1', t.calibLiquid === 1, t.calibLiquid);
ck('telemetry flow is still the daily 123', t.flow === before.flowRate, t.flow);
advance(3000);
t = S.telemetry();
ck('calib dispensed ≈ 800 mL/min × 3 s = 40 mL', Math.abs(t.dispensed - 40) < 1.5, t.dispensed);
ck('calib progress uses calibTargetVol as denominator',
   t.progress === Math.min(100, Math.floor(t.dispensed / t.calibTargetVol * 100)), { p: t.progress, disp: t.dispensed });

/* stop mid-run keeps the wizard at CALIB_RUN and clears calibRunning */
cmd('stop');
ck('stop clears calibRunning', S.D.calibRunning === false, S.D.calibRunning);
ck('stop leaves calibStep at 3 (CALIB_RUN)', S.D.calibStep === 3, S.D.calibStep);
ck('stop recorded steps with calibSPM', S.D.calibStepsRun === Math.round(40 * S.D.liquidSPM[1]) || S.D.calibStepsRun > 0, S.D.calibStepsRun);

cmd('calib_start_run');
advance(8000);   /* 100 mL @ 800 mL/min = 7.5 s，再等 0.5 s（DONE 保持 2 s 后才回 IDLE） */
ck('calib run finished → CALIB_MEASURE (4)', S.D.calibStep === 4, S.D.calibStep);
ck('calibRunning cleared after finish', S.D.calibRunning === false, S.D.calibRunning);
ck('state DONE right after the calib run', S.D.state === 'DONE', S.D.state);
ck('calib dispensed == calibTargetVol', S.D.dispensed === 100, S.D.dispensed);
const steps = S.D.calibStepsRun;
ck('calibStepsRun == calibTargetVol × liquidSPM[calibLiquid]',
   steps === Math.round(100 * S.D.liquidSPM[1]), { steps, expect: Math.round(100 * S.D.liquidSPM[1]) });

const m = cmd('calib_measure', { v: 95 });
ck('calib_measure ok', m.ok, m);
ck('calib_measure oldSPM is the CALIB liquid spm', m.data.oldSPM === before.spm[1], m.data);
ck('newSPM = steps/95', Math.abs(m.data.newSPM - steps / 95) < 0.01, m.data);
ck('daily liquidIdx still untouched before save', S.D.liquidIdx === before.liquidIdx, S.D.liquidIdx);

ck('calib_save', cmd('calib_save').ok);
ck('save commits liquidIdx = calibLiquid', S.D.liquidIdx === 1, S.D.liquidIdx);
ck('save commits stepsPerMl', S.D.stepsPerMl === S.D.calibNewSPM, { spm: S.D.stepsPerMl, new: S.D.calibNewSPM });
ck('save writes only the calibrated liquid slot',
   S.D.liquidSPM[1] === S.D.calibNewSPM && S.D.liquidSPM[2] === before.spm[2] && S.D.liquidSPM[0] === before.spm[0], S.D.liquidSPM);
ck('save did NOT change mode', S.D.mode === before.mode, S.D.mode);
ck('save did NOT change flowRate', S.D.flowRate === before.flowRate, S.D.flowRate);
ck('save did NOT change targetVolume', S.D.targetVolume === before.targetVolume, S.D.targetVolume);
ck('save did NOT change targetTime', S.D.targetTime === before.targetTime, S.D.targetTime);

ck('calib_settings_done', cmd('calib_settings_done').ok);
ck('back to MAIN / calibStep 0', S.D.menu === 'MAIN' && S.D.calibStep === 0, { menu: S.D.menu, step: S.D.calibStep });
ck('mode is still the user-s TIME after the whole wizard', S.D.mode === 'TIME', S.D.mode);
ck('main panel editable again', cmd('set_flow', { v: 200 }).ok && S.D.flowRate === 200, S.D.flowRate);
cmd('set_flow', { v: before.flowRate });

/* ---------- E. abort path changes nothing ---------- */
const preAbort = snap();
cmd('calib_enter');
cmd('calib_select_liquid', { i: 3 });
cmd('calib_set_vol', { v: 1500, f: 1600 });
ck('abort leaves every daily setting untouched', JSON.stringify(snap()) === JSON.stringify(preAbort), { now: snap(), preAbort });
ck('abort → MAIN / step 0', cmd('calib_abort').ok && S.D.menu === 'MAIN' && S.D.calibStep === 0, S.D.menu);

/* ---------- F. menu_main during a calibration run ---------- */
cmd('calib_enter');
cmd('calib_select_liquid', { i: 0 });
cmd('calib_set_vol', { v: 50, f: 300 });
cmd('calib_start_run');
advance(2000);
ck('menu_main stops the calib run', cmd('menu_main').ok && S.D.calibRunning === false, S.D.calibRunning);
ck('menu_main clears the wizard', S.D.menu === 'MAIN' && S.D.calibStep === 0, { menu: S.D.menu, step: S.D.calibStep });
ck('menu_main leaves daily settings untouched', JSON.stringify(snap()) === JSON.stringify(preAbort), snap());

/* ---------- G. HTTP query parsing of &f= ---------- */
cmd('calib_enter');
cmd('calib_select_liquid', { i: 0 });
let fetched = null;
window.fetch('/api/cmd?c=calib_set_vol&v=250&f=640').then(r => r.json()).then(b => { fetched = b; });
setTimeout(() => {
  ck('&f= parsed into the flow param', S.D.calibFlowRate === 640 && S.D.calibTargetVol === 250,
     { calibFlowRate: S.D.calibFlowRate, calibTargetVol: S.D.calibTargetVol });
  ck('fetch response echoes the settings', fetched && fetched.ok && fetched.data.calibFlow === 640, fetched);
  console.log(`\nassertions passed: ${pass}`);
  if (fails.length) {
    console.log(`FAILED: ${fails.length}`);
    fails.forEach(f => console.log('  ✗ ' + f));
    process.exit(1);
  }
  console.log('FAILED: 0');
  console.log('\n--- last sim log lines ---');
  logs.slice(-14).forEach(l => console.log('  ' + l));
}, 20);
