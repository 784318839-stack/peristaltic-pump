static const char WEB_UI[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0,user-scalable=no,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="PumpCtrl">
<meta name="theme-color" content="#0a0a0f">
<link rel="manifest" href="/manifest.json">
<title>蠕动泵</title>
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#0a0a0f;--surface:#13131a;--card:#18181f;--border:#252530;
  --accent:#ff5e6d;--accent-glow:rgba(255,94,109,.25);
  --green:#3dd68c;--green-glow:rgba(61,214,140,.25);
  --amber:#f5a623;--amber-glow:rgba(245,166,35,.25);
  --blue:#4d94ff;--blue-glow:rgba(77,148,255,.25);
  --text:#eeeef2;--text2:#8888;--text3:#52525e;
  --radius:14px;--radius-sm:10px;
}
body{
  font-family:-apple-system,BlinkMacSystemFont,'SF Pro Display','Segoe UI',sans-serif;
  background:var(--bg);color:var(--text);min-height:100vh;
  padding-bottom:calc(64px + env(safe-area-inset-bottom));
  -webkit-font-smoothing:antialiased;
}

/* ---- connection banner ---- */
#connBanner{
  position:fixed;top:0;left:0;right:0;z-index:100;
  background:var(--surface);border-bottom:1px solid var(--border);
  padding:10px 16px;display:flex;align-items:center;justify-content:center;gap:10px;
  font-size:13px;color:var(--text2);
}
#connBanner .dot{width:8px;height:8px;border-radius:50%;flex-shrink:0}
#connBanner .dot.ok{background:var(--green);box-shadow:0 0 8px var(--green-glow)}
#connBanner .dot.warn{background:var(--amber);box-shadow:0 0 8px var(--amber-glow)}
#connBanner .dot.err{background:#e5484d}
#connBanner .dot.ble{background:var(--blue);box-shadow:0 0 8px var(--blue-glow)}

/* ---- main ---- */
main{max-width:460px;margin:44px auto 0;padding:12px 14px}
main.hide{display:none}

/* ---- pages ---- */
.page{display:none}
.page.active{display:block}

/* ---- cards ---- */
.card{
  background:var(--card);border:1px solid var(--border);border-radius:var(--radius);
  padding:16px;margin-bottom:10px;
}
.card-hdr{
  display:flex;align-items:center;justify-content:space-between;margin-bottom:12px;
}
.card-hdr h2{font-size:12px;font-weight:600;color:var(--text3);text-transform:uppercase;letter-spacing:.06em}

/* ---- status overview ---- */
.state-hero{display:flex;align-items:center;gap:14px;margin-bottom:14px}
.state-icon{
  width:52px;height:52px;border-radius:50%;display:flex;align-items:center;justify-content:center;
  font-size:26px;background:var(--surface);border:2px solid var(--border);flex-shrink:0
}
.state-icon.run{animation: pulse 1.2s ease-in-out infinite}
.state-icon.run{border-color:var(--green);box-shadow:0 0 20px var(--green-glow)}
.state-icon.done{border-color:var(--green)}
.state-hero .info{flex:1;min-width:0}
.state-hero .st-label{font-size:12px;color:var(--text2);margin-bottom:2px}
.state-hero .st-value{font-size:14px;font-weight:600}
.state-hero .st-value.big{font-size:22px}

@keyframes pulse{0%,100%{box-shadow:0 0 8px var(--green-glow)}50%{box-shadow:0 0 24px var(--green-glow)}}

/* ---- stat pills ---- */
.stat-row{display:flex;gap:8px;margin-bottom:12px}
.stat-pill{flex:1;text-align:center;background:var(--surface);border-radius:var(--radius-sm);padding:10px 6px}
.stat-pill .val{font-size:20px;font-weight:700;color:var(--text);line-height:1.2}
.stat-pill .val.highlight{color:var(--accent)}
.stat-pill .lbl{font-size:10px;color:var(--text3);margin-top:3px}

/* ---- progress ---- */
.progress-wrap{margin-bottom:4px}
.progress-bar{height:6px;background:var(--surface);border-radius:3px;overflow:hidden;margin-bottom:6px}
.progress-fill{height:100%;background:var(--accent);border-radius:3px;transition:width .4s ease;width:0%}
.progress-meta{display:flex;justify-content:space-between;font-size:11px;color:var(--text2)}
.progress-meta .pct{font-weight:700;color:var(--accent)}

/* ---- buttons ---- */
.btn-row{display:flex;gap:8px}
button{
  border:none;border-radius:var(--radius-sm);font-size:15px;font-weight:600;
  cursor:pointer;transition:all .12s;touch-action:manipulation;
  min-height:46px;padding:11px 18px;
  -webkit-tap-highlight-color:transparent;user-select:none;
}
button:active{transform:scale(.96)}
button:disabled{opacity:.35;pointer-events:none}

.btn-play{flex:1;background:var(--green);color:#000;font-size:16px;box-shadow:0 2px 12px var(--green-glow)}
.btn-pause{flex:1;background:var(--amber);color:#000;box-shadow:0 2px 12px var(--amber-glow)}
.btn-stop{flex:1;background:#e5484d;color:#fff}
.btn-ghost{flex:1;background:var(--surface);color:var(--text);border:1px solid var(--border);font-size:13px}

/* ---- mode tabs ---- */
.mode-tabs{display:flex;gap:4px;background:var(--surface);border-radius:var(--radius-sm);padding:3px}
.mode-tab{
  flex:1;text-align:center;padding:9px 4px;border-radius:8px;
  font-size:13px;font-weight:600;color:var(--text3);cursor:pointer;transition:all .2s
}
.mode-tab.active{background:var(--accent);color:#fff;box-shadow:0 2px 8px var(--accent-glow)}

/* ---- liquid chips ---- */
.liquid-row{display:flex;gap:6px;margin-top:8px}
.liquid-chip{
  flex:1;text-align:center;padding:7px 2px;border-radius:8px;
  font-size:12px;color:var(--text3);cursor:pointer;background:var(--surface);
  border:1px solid transparent;transition:all .15s
}
.liquid-chip.active{background:var(--accent);color:#fff;border-color:var(--accent)}

/* ---- inputs ---- */
.input-row{display:flex;gap:8px;align-items:center;margin-bottom:8px}
.input-row label{font-size:12px;color:var(--text2);min-width:42px}
.input-row input{
  flex:1;background:var(--surface);border:1px solid var(--border);border-radius:8px;
  padding:10px 12px;color:var(--text);font-size:15px;outline:none;transition:border .2s
}
.input-row input:focus{border-color:var(--accent);box-shadow:0 0 0 3px var(--accent-glow)}
.input-row .unit{font-size:11px;color:var(--text3);min-width:52px;text-align:right}

/* ---- jet grid ---- */
.jet-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:8px}
.jet-grid label{display:block;font-size:11px;color:var(--text2);margin-bottom:3px}
.jet-grid input{
  width:100%;background:var(--surface);border:1px solid var(--border);
  border-radius:7px;padding:9px;color:var(--text);font-size:14px;outline:none
}
.jet-grid input:focus{border-color:var(--accent)}

/* ---- footer bar ---- */
.footer-bar{
  position:fixed;bottom:0;left:0;right:0;z-index:50;
  background:var(--card);border-top:1px solid var(--border);
  font-size:10px;color:var(--text3);text-align:center;
  padding:8px 12px calc(8px + env(safe-area-inset-bottom))
}
.footer-bar span{color:var(--text2)}

/* ---- tab bar ---- */
.tab-bar{
  position:fixed;bottom:0;left:0;right:0;z-index:50;
  background:var(--surface);border-top:1px solid var(--border);
  display:flex;padding-bottom:env(safe-area-inset-bottom)
}
.tab-item{
  flex:1;text-align:center;padding:10px 4px 8px;color:var(--text3);
  font-size:10px;font-weight:500;cursor:pointer;transition:color .2s
}
.tab-item.active{color:var(--accent)}
.tab-item svg{display:block;margin:0 auto 3px;width:22px;height:22px;opacity:.7}
.tab-item.active svg{opacity:1}

/* ---- toast ---- */
.toast{
  position:fixed;bottom:90px;left:50%;transform:translateX(-50%);
  background:var(--card);border:1px solid var(--border);padding:10px 22px;
  border-radius:20px;font-size:13px;z-index:200;opacity:0;transition:opacity .25s;
  pointer-events:none;backdrop-filter:blur(12px)
}
.toast.show{opacity:1}
.toast.err{border-color:#e5484d;color:#e5484d}

/* ---- preset slots ---- */
.slot-row{display:flex;gap:6px;margin-bottom:6px}
.slot-btn{
  flex:1;padding:10px 4px;border-radius:var(--radius-sm);text-align:center;
  font-size:12px;font-weight:600;cursor:pointer;background:var(--surface);
  color:var(--text2);border:1px solid var(--border);transition:all .12s
}
.slot-btn:active{background:var(--accent);color:#fff;border-color:var(--accent)}
.slot-hint{font-size:10px;color:var(--text3);text-align:center;margin-top:4px}

/* ---- misc ---- */
.spacer4{height:4px}.spacer8{height:8px}
</style>
</head>
<body>

<!-- connection banner -->
<div id="connBanner">
  <span class="dot err" id="connDot"></span>
  <span id="connText">等待连接...</span>
  <button onclick="connectBLE()" id="bleBtn" style="margin-left:auto;font-size:11px;padding:5px 12px;min-height:0;border-radius:14px;background:transparent;border:1px solid var(--border);color:var(--text2)">BLE</button>
</div>

<main id="mainUI" class="hide">

  <!-- ==================== STATUS PAGE ==================== -->
  <div class="page active" id="pageStatus">

    <!-- state hero -->
    <div class="card">
      <div class="state-hero">
        <div class="state-icon" id="stIcon">⏸</div>
        <div class="info">
          <div class="st-label">运行状态</div>
          <div class="st-value big" id="stState">--</div>
        </div>
        <div style="text-align:right">
          <div class="st-label">模式</div>
          <div class="st-value" id="stMode">--</div>
          <div class="st-label" style="margin-top:4px">液体</div>
          <div class="st-value" id="stLiquid" style="font-size:12px">--</div>
        </div>
      </div>

      <div class="stat-row">
        <div class="stat-pill"><div class="val highlight" id="stFlow">--</div><div class="lbl">流量 mL/min</div></div>
        <div class="stat-pill"><div class="val" id="stDisp">--</div><div class="lbl">已出 mL</div></div>
      </div>

      <div class="progress-wrap">
        <div class="progress-bar"><div class="progress-fill" id="progressBar"></div></div>
        <div class="progress-meta">
          <span><span class="pct" id="stProgress">0%</span></span>
          <span>目标 <b id="stTarget">--</b> mL</span>
        </div>
      </div>
    </div>

    <!-- run controls -->
    <div class="card">
      <div class="btn-row">
        <button class="btn-play" onclick="send('start')" id="btnStart">&#9654; 启动</button>
        <button class="btn-pause" onclick="send('pause')" id="btnPause" disabled>&#9646;&#9646; 暂停</button>
        <button class="btn-stop" onclick="send('stop')" id="btnStop" disabled>&#9632; 停止</button>
      </div>
    </div>

    <!-- device info -->
    <div class="footer-bar">
      累计 <span id="stTotal">--</span> mL &nbsp;|&nbsp;
      管路 <span id="stTube">--</span>% &nbsp;|&nbsp;
      电机 <span id="stMotor">--</span>
    </div>
  </div>

  <!-- ==================== PARAMS PAGE ==================== -->
  <div class="page" id="pageParams">
    <div class="card">
      <div class="mode-tabs" id="modeTabs">
        <div class="mode-tab active" data-mode="VOLUME">体积模式</div>
        <div class="mode-tab" data-mode="TIME">定时模式</div>
        <div class="mode-tab" data-mode="JET">喷射模式</div>
      </div>
      <div class="liquid-row" id="liquidRow"></div>
    </div>

    <div class="card" id="paramCard">
      <div id="paramVolume">
        <div class="input-row"><label>流量</label><input type="number" id="inFlow" value="50" step="0.1" min="0.1" max="2000"><span class="unit">mL/min</span></div>
        <div class="input-row"><label>体积</label><input type="number" id="inVolume" value="10" step="0.1" min="0.1" max="99999"><span class="unit">mL</span></div>
        <div class="btn-row"><button class="btn-ghost" onclick="setVal('set_flow','inFlow')">应用流量</button><button class="btn-ghost" onclick="setVal('set_volume','inVolume')">应用体积</button></div>
      </div>
      <div id="paramTime" style="display:none">
        <div class="input-row"><label>体积</label><input type="number" id="inTimeVol" value="10" step="0.1" min="0.1" max="99999"><span class="unit">mL</span></div>
        <div class="input-row"><label>时间</label><input type="number" id="inTimeSec" value="30" step="1" min="1" max="86400"><span class="unit">秒</span></div>
        <div class="btn-row"><button class="btn-ghost" onclick="setVal('set_volume','inTimeVol')">应用体积</button><button class="btn-ghost" onclick="setVal('set_time','inTimeSec')">应用时间</button></div>
      </div>
      <div id="paramJet" style="display:none">
        <div class="jet-grid">
          <div><label>单次量 mL</label><input type="number" id="inJetVol" value="1" step="0.1" min="0.1" max="10"></div>
          <div><label>间隔 秒</label><input type="number" id="inJetInt" value="3" step="1" min="1" max="60"></div>
          <div><label>流量 mL/min</label><input type="number" id="inJetFlow" value="200" step="1" min="10" max="2000"></div>
          <div><label>压力 1-10</label><input type="number" id="inJetPres" value="5" step="1" min="1" max="10"></div>
        </div>
        <div class="btn-row" style="flex-wrap:wrap">
          <button class="btn-ghost" onclick="setVal('set_jet_vol','inJetVol')" style="min-width:0;font-size:12px">单次量</button>
          <button class="btn-ghost" onclick="setVal('set_jet_interval','inJetInt')" style="min-width:0;font-size:12px">间隔</button>
          <button class="btn-ghost" onclick="setVal('set_jet_flow','inJetFlow')" style="min-width:0;font-size:12px">流量</button>
          <button class="btn-ghost" onclick="setVal('set_jet_pressure','inJetPres')" style="min-width:0;font-size:12px">压力</button>
        </div>
      </div>
    </div>
  </div>

  <!-- ==================== TOOLS PAGE ==================== -->
  <div class="page" id="pageTools">
    <div class="card">
      <div class="card-hdr"><h2>方案预设</h2></div>
      <div class="slot-row">
        <div class="slot-btn" onclick="send('preset_load',0)">加载 1</div>
        <div class="slot-btn" onclick="send('preset_load',1)">加载 2</div>
        <div class="slot-btn" onclick="send('preset_load',2)">加载 3</div>
        <div class="slot-btn" onclick="send('preset_load',3)">加载 4</div>
      </div>
      <div class="slot-row">
        <div class="slot-btn" onclick="send('preset_save',0)" style="font-size:10px">存入 1</div>
        <div class="slot-btn" onclick="send('preset_save',1)" style="font-size:10px">存入 2</div>
        <div class="slot-btn" onclick="send('preset_save',2)" style="font-size:10px">存入 3</div>
        <div class="slot-btn" onclick="send('preset_save',3)" style="font-size:10px">存入 4</div>
      </div>
      <div class="slot-hint">加载 = 恢复整套参数 &nbsp;|&nbsp; 存入 = 保存当前设置</div>
    </div>

    <div class="card">
      <div class="card-hdr"><h2>快捷操作</h2></div>
      <div class="btn-row">
        <button class="btn-ghost" onclick="send('prime_start')">预灌/快排</button>
        <button class="btn-ghost" onclick="send('prime_stop')">停止预灌</button>
        <button class="btn-ghost" onclick="send('menu_main')">主菜单</button>
      </div>
    </div>

    <div class="card">
      <div class="card-hdr"><h2>校准向导</h2></div>
      <div class="btn-row" style="margin-bottom:8px">
        <button class="btn-ghost" onclick="send('calib_enter')" style="font-size:13px">进入校准</button>
        <button class="btn-ghost" onclick="send('calib_start_run')" style="font-size:13px">泵出</button>
        <button class="btn-ghost" onclick="send('calib_stop_run')" style="font-size:13px">停泵</button>
        <button class="btn-ghost" onclick="send('calib_abort')" style="font-size:13px">退出</button>
      </div>
      <div class="input-row"><label>实测 mL</label><input type="number" id="inCalibML" value="10" step="0.1" min="0.1"><span class="unit">量筒读数</span></div>
      <div class="btn-row">
        <button class="btn-ghost" onclick="setVal('calib_measure','inCalibML')" style="border-color:var(--accent);color:var(--accent)">提交实测</button>
        <button onclick="send('calib_save')" style="flex:1;background:var(--green);color:#000;box-shadow:0 2px 12px var(--green-glow)">保存校准</button>
      </div>
    </div>
  </div>

</main>

<!-- bottom tab bar -->
<nav class="tab-bar" id="tabBar">
  <div class="tab-item active" data-page="pageStatus">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/></svg>
    状态
  </div>
  <div class="tab-item" data-page="pageParams">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M12 1v4m0 14v4M4.2 4.2l2.8 2.8m10 10l2.8 2.8M1 12h4m14 0h4M4.2 19.8l2.8-2.8m10-10l2.8-2.8"/></svg>
    参数
  </div>
  <div class="tab-item" data-page="pageTools">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76z"/></svg>
    工具
  </div>
</nav>

<div class="toast" id="toast"></div>

<script>
const $=s=>document.querySelector(s),$$=s=>document.querySelectorAll(s);
const NUS_SVC='6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const NUS_TX='6e400003-b5a3-f393-e0a9-e50e24dcca9e';
const NUS_RX='6e400002-b5a3-f393-e0a9-e50e24dcca9e';

var currentMode='VOLUME',currentState='IDLE';
var bleDevice=null,bleTxChar=null,bleConnected=false;
var pollTimer=null,useWiFi=false;

// ==================== BLE ====================
async function connectBLE(){
  try{
    $('#connText').textContent='扫描中...';
    try{
      bleDevice=await navigator.bluetooth.requestDevice({
        filters:[{namePrefix:'PumpCtrl'}],optionalServices:[NUS_SVC]
      });
    }catch(e){
      bleDevice=await navigator.bluetooth.requestDevice({
        acceptAllDevices:true,optionalServices:[NUS_SVC]
      });
    }
    $('#connText').textContent='连接中...';
    var server=await bleDevice.gatt.connect();
    var svc=await server.getPrimaryService(NUS_SVC);
    bleTxChar=await svc.getCharacteristic(NUS_TX);
    var rxChar=await svc.getCharacteristic(NUS_RX);
    await rxChar.startNotifications();
    rxChar.addEventListener('characteristicvaluechanged',onBLEData);
    bleDevice.addEventListener('gattserverdisconnected',onBLEDisconnect);
    bleConnected=true;
    onBLEConnected();
  }catch(e){
    $('#connText').textContent='BLE 失败, 尝试 WiFi...';
    console.log('BLE:',e);
    tryWiFiFallback();
  }
}

function onBLEData(event){
  var data=new TextDecoder().decode(event.target.value);
  try{var d=JSON.parse(data);if(d.state)updateUI(d)}catch(e){}
}

function onBLEDisconnect(){
  bleConnected=false;bleTxChar=null;
  updateConn('err','已断开');
  $('#mainUI').classList.add('hide');
  $('#bleBtn').style.display='';
  if(pollTimer)clearInterval(pollTimer);
  setTimeout(tryWiFiFallback,300);
}

async function disconnectBLE(){
  if(bleDevice&&bleDevice.gatt.connected)await bleDevice.gatt.disconnect();
}

function onBLEConnected(){
  updateConn('ble','BLE 已连接');
  $('#bleBtn').style.display='none';
  $('#mainUI').classList.remove('hide');
  send('status');
  if(pollTimer)clearInterval(pollTimer);
  pollTimer=setInterval(()=>send('status'),1000);
}

function bleWrite(str){
  if(!bleTxChar)return;
  var enc=new TextEncoder().encode(str+'\\n');
  bleTxChar.writeValueWithoutResponse(enc).catch(()=>{});
}

// ==================== WiFi fallback ====================
function tryWiFiFallback(){
  fetch('/api/status').then(r=>r.json()).then(d=>{
    if(d&&d.state){useWiFi=true;onWiFiConnected();}
  }).catch(()=>{});
}

function onWiFiConnected(){
  updateConn('ok','WiFi 已连接');
  $('#mainUI').classList.remove('hide');
  $('#bleBtn').style.display='none';
  poll();
  if(pollTimer)clearInterval(pollTimer);
  pollTimer=setInterval(poll,1000);
}

// ==================== send commands ====================
function send(cmd,slot){
  if(bleConnected){
    if(cmd==='status'){bleWrite('s');return}
    var params='';
    if(slot!==undefined)params='"slot":'+slot;
    if(params)bleWrite('{"cmd":"'+cmd+'","params":{'+params+'}}');
    else bleWrite('{"cmd":"'+cmd+'","params":{}}');
    return;
  }
  if(useWiFi){
    var url='/api/cmd?c='+cmd;
    if(slot!==undefined)url+='&s='+slot;
    fetch(url).then(r=>r.json()).then(r=>{
      if(r&&!r.ok)toast(r.error||'失败','err');
      else if(r&&r.ok)toast('&#10003;');
    }).catch(()=>{});
  }
}

function setVal(cmd,id){
  var v=document.getElementById(id).value;
  if(bleConnected){
    bleWrite('{"cmd":"'+cmd+'","params":{"value":'+v+'}}');
    return;
  }
  if(useWiFi){
    fetch('/api/cmd?c='+cmd+'&v='+v).then(r=>r.json()).then(r=>{
      if(r&&!r.ok)toast(r.error||'失败','err');
      else if(r&&r.ok)toast('&#10003;');
    }).catch(()=>{});
  }
}

// ==================== UI update ====================
function updateConn(cls,txt){
  var d=$('#connDot'),t=$('#connText');
  d.className='dot '+cls;t.textContent=txt;
}

var stateIcons={IDLE:'⏸',RUNNING:'&#9654;',PAUSED:'⏸',DONE:'&#10003;',ANTI_DRIP:'↩'};
var stateClasses={IDLE:'',RUNNING:'run',PAUSED:'',DONE:'done',ANTI_DRIP:'done'};
var stateLabels={IDLE:'待机',RUNNING:'运行中',PAUSED:'已暂停',DONE:'已完成',ANTI_DRIP:'回吸中'};

function updateUI(d){
  currentState=d.state;currentMode=d.mode;

  // state hero
  $('#stIcon').innerHTML=stateIcons[d.state]||'?';
  $('#stIcon').className='state-icon '+(stateClasses[d.state]||'');
  $('#stState').textContent=stateLabels[d.state]||d.state;
  $('#stMode').textContent=d.mode==='TIME'?'定时':d.mode==='JET'?'喷射':'体积';
  $('#stLiquid').textContent=d.liquid||'--';

  // stats
  $('#stFlow').textContent=d.flow.toFixed(1);
  $('#stDisp').textContent=d.dispensed.toFixed(2);
  $('#stTarget').textContent=d.targetVol.toFixed(1);
  $('#progressBar').style.width=d.progress+'%';
  $('#stProgress').textContent=d.progress+'%';
  $('#stTotal').textContent=d.totalDispensed.toFixed(0);
  $('#stTube').textContent=d.tubePct;
  $('#stMotor').textContent=d.stepperEnabled?'使能':'断电';

  // mode tabs
  $$('.mode-tab').forEach(t=>t.classList.toggle('active',t.dataset.mode===d.mode));

  // inputs (skip if user is editing)
  var ae=document.activeElement;
  if(ae!==$('#inFlow'))$('#inFlow').value=d.flow;
  if(ae!==$('#inVolume'))$('#inVolume').value=d.targetVol;
  if(ae!==$('#inTimeVol'))$('#inTimeVol').value=d.targetVol;
  if(ae!==$('#inTimeSec'))$('#inTimeSec').value=d.targetTime;
  if(ae!==$('#inJetVol'))$('#inJetVol').value=d.jetVolume;
  if(ae!==$('#inJetInt'))$('#inJetInt').value=d.jetInterval;
  if(ae!==$('#inJetFlow'))$('#inJetFlow').value=d.jetFlowRate;
  if(ae!==$('#inJetPres'))$('#inJetPres').value=d.jetPressure;

  // param panels
  $('#paramVolume').style.display=d.mode==='VOLUME'?'':'none';
  $('#paramTime').style.display=d.mode==='TIME'?'':'none';
  $('#paramJet').style.display=d.mode==='JET'?'':'none';

  // buttons
  var idl=d.state==='IDLE',run=d.state==='RUNNING',pau=d.state==='PAUSED';
  $('#btnStart').disabled=!idl;
  $('#btnPause').disabled=!run;
  $('#btnStop').disabled=!(run||pau);

  // liquid
  if(d.liquidIdx!==undefined)buildLiquidRow(d.liquidIdx);
}

// WiFi poll
function poll(){
  fetch('/api/status').then(r=>r.json()).then(d=>{
    if(!d)return updateConn('err','离线');
    updateConn('ok','WiFi');
    updateUI(d);
  }).catch(()=>updateConn('err','离线'));
}

// ==================== tabs ====================
$$('.tab-item').forEach(t=>{t.addEventListener('click',()=>{
  $$('.tab-item').forEach(x=>x.classList.remove('active'));
  t.classList.add('active');
  $$('.page').forEach(p=>p.classList.remove('active'));
  $('#'+t.dataset.page).classList.add('active');
})});

// mode tabs
$$('.mode-tab').forEach(t=>{t.addEventListener('click',()=>{
  if(t.dataset.mode!==currentMode){
    if(bleConnected){bleWrite('{"cmd":"set_mode","params":{"mode":"'+t.dataset.mode+'"}}')}
    else{send('set_mode&m='+t.dataset.mode)}
  }
})});

// liquid chips
var liqNames=['水','粘稠','有机','自定义'];
function buildLiquidRow(idx){
  var r=$('#liquidRow');r.innerHTML='';
  liqNames.forEach((n,i)=>{
    var b=document.createElement('div');
    b.className='liquid-chip'+(i===idx?' active':'');
    b.textContent=n;
    b.onclick=()=>{
      if(bleConnected){bleWrite('{"cmd":"set_liquid","params":{"index":'+i+'}}')}
      else{send('set_liquid&i='+i)}
    };
    r.appendChild(b);
  });
}
buildLiquidRow(0);

// ==================== toast ====================
var tt;
function toast(msg,cls){
  var e=$('#toast');
  e.innerHTML=msg;
  e.className='toast show'+(cls?' '+cls:'');
  if(tt)clearTimeout(tt);
  tt=setTimeout(()=>e.classList.remove('show'),1800);
}

// ==================== init ====================
setTimeout(tryWiFiFallback,200);
setTimeout(tryWiFiFallback,600);
</script>
</body>
</html>
)rawliteral";
