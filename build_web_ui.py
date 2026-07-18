#!/usr/bin/env python3
"""Generate the WEB_UI C++ string with BLE support."""

html = r"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0,user-scalable=no,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="PumpCtrl">
<meta name="theme-color" content="#0d1117">
<link rel="manifest" href="/manifest.json">
<title>蠕动泵控制器</title>
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{--bg:#0d1117;--card:#161b22;--border:#30363d;--accent:#e94560;--accent2:#0f3460;--green:#238636;--yellow:#d2991d;--red:#da3633;--blue:#1f6feb;--text:#e6edf3;--text2:#8b949e;--text3:#484f58}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:var(--bg);color:var(--text);min-height:100vh;padding-bottom:calc(50px + env(safe-area-inset-bottom))}
header{background:var(--card);border-bottom:1px solid var(--border);padding:12px 16px;display:flex;align-items:center;justify-content:space-between;position:sticky;top:0;z-index:10}
header h1{font-size:18px;font-weight:600}
.dot{width:10px;height:10px;border-radius:50%;display:inline-block;margin-right:4px;transition:background .3s}
.dot.ok{background:var(--green)}.dot.warn{background:var(--yellow)}.dot.err{background:var(--red)}
.dot.ble{background:var(--blue)}
main{max-width:480px;margin:0 auto;padding:12px}
main.hide{display:none}
#blePrompt{max-width:480px;margin:40px auto;text-align:center;padding:20px}
#blePrompt .big-btn{display:inline-block;padding:20px 40px;font-size:22px;font-weight:700;border-radius:16px;background:var(--blue);color:#fff;cursor:pointer;margin:20px 0;touch-action:manipulation}
#blePrompt .big-btn:active{transform:scale(.95);opacity:.8}
.card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:14px 16px;margin-bottom:10px}
.card h2{font-size:14px;color:var(--text2);margin-bottom:10px;text-transform:uppercase;letter-spacing:1px}
.stats{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.stat{text-align:center;padding:8px 4px}
.stat .val{font-size:26px;font-weight:700;color:var(--accent);line-height:1.2}
.stat .val.small{font-size:18px}
.stat .lbl{font-size:11px;color:var(--text2);margin-top:2px}
.progress-bar{height:8px;background:var(--border);border-radius:4px;overflow:hidden;margin:8px 0}
.progress-fill{height:100%;background:var(--accent);border-radius:4px;transition:width .3s;width:0%}
.btn-row{display:flex;flex-wrap:wrap;gap:6px;justify-content:center}
button{border:none;border-radius:10px;padding:12px 16px;font-size:15px;font-weight:600;cursor:pointer;transition:all .15s;touch-action:manipulation;min-height:44px}
button:active{transform:scale(.95);opacity:.8}
.btn-start{background:var(--green);color:#fff;flex:1;min-width:80px}
.btn-pause{background:var(--yellow);color:#000;flex:1;min-width:80px}
.btn-stop{background:var(--red);color:#fff;flex:1;min-width:80px}
.btn-action{background:var(--accent2);color:var(--text);flex:1;min-width:70px;font-size:13px;padding:10px 8px}
.btn-sm{background:var(--border);color:var(--text);font-size:12px;padding:8px 12px;min-height:36px}
.btn-active{background:var(--accent)!important;color:#fff!important}
.mode-tabs{display:flex;gap:4px;margin-bottom:12px}
.mode-tab{flex:1;text-align:center;padding:10px 6px;border-radius:8px;font-size:13px;font-weight:600;background:var(--border);color:var(--text2);cursor:pointer}
.mode-tab.active{background:var(--accent);color:#fff}
.input-row{display:flex;gap:6px;align-items:center;margin:8px 0}
.input-row label{font-size:13px;color:var(--text2);min-width:50px;text-align:right}
.input-row input{flex:1;background:var(--bg);border:1px solid var(--border);border-radius:8px;padding:10px 12px;color:var(--text);font-size:16px;outline:none}
.input-row input:focus{border-color:var(--accent)}
.input-row .unit{font-size:12px;color:var(--text3);min-width:40px}
.jet-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.jet-grid label{font-size:12px;color:var(--text2)}
.jet-grid input{width:100%;background:var(--bg);border:1px solid var(--border);border-radius:6px;padding:8px;color:var(--text);font-size:14px}
.liquid-row{display:flex;gap:6px;margin:8px 0}
.liquid-btn{flex:1;padding:10px;border-radius:8px;background:var(--border);color:var(--text2);text-align:center;font-size:13px;cursor:pointer}
.liquid-btn.active{background:var(--accent);color:#fff}
.toast{position:fixed;bottom:80px;left:50%;transform:translateX(-50%);background:var(--card);border:1px solid var(--border);padding:10px 20px;border-radius:20px;font-size:14px;z-index:100;opacity:0;transition:opacity .3s;pointer-events:none}
.toast.show{opacity:1}
.toast.error{border-color:var(--red);color:var(--red)}
.tab-bar{position:fixed;bottom:0;left:0;right:0;background:var(--card);border-top:1px solid var(--border);display:flex;z-index:50;padding-bottom:env(safe-area-inset-bottom)}
.tab-item{flex:1;text-align:center;padding:8px 4px;color:var(--text3);font-size:10px;cursor:pointer;transition:color .2s}
.tab-item.active{color:var(--accent)}
.tab-item .icon{font-size:20px;display:block;margin-bottom:2px}
.page{display:none}
.page.active{display:block}
</style>
</head>
<body>

<!-- BLE 连接提示 (小横幅) -->
<div id="blePrompt" style="background:var(--card);text-align:center;padding:10px;border-bottom:1px solid var(--border)">
  <span style="color:var(--text2)" id="bleStatus">WiFi 连接中...</span>
  <button onclick="connectBLE()" id="bleBtn" style="margin-left:10px;font-size:12px;padding:6px 12px;min-height:28px;background:var(--blue);color:#fff;border-radius:6px">蓝牙</button>
</div>

<!-- 主界面 -->
<main id="mainUI" class="hide">
  <header>
    <div><span id="connDot" class="err"></span><span id="connText">--</span></div>
    <h1>蠕动泵</h1>
    <button onclick="disconnectBLE()" style="font-size:12px;padding:6px 10px;min-height:28px;background:var(--border);color:var(--text2)">断开</button>
  </header>

  <!-- 页面1: 状态 -->
  <div class="page active" id="pageStatus">
    <div class="card">
      <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:8px">
        <h2 style="margin:0">实时状态</h2>
        <span style="font-size:24px" id="stStateIcon">⏸</span>
      </div>
      <div class="stats">
        <div class="stat"><div class="val" id="stState">--</div><div class="lbl">运行状态</div></div>
        <div class="stat"><div class="val small" id="stMode">--</div><div class="lbl">模式</div></div>
        <div class="stat"><div class="val small" id="stFlow">--</div><div class="lbl">流量 mL/min</div></div>
        <div class="stat"><div class="val small" id="stDisp">--</div><div class="lbl">已出 mL</div></div>
      </div>
      <div class="progress-bar"><div class="progress-fill" id="progressBar"></div></div>
      <div style="display:flex;justify-content:space-between;font-size:11px;color:var(--text2)">
        <span id="stProgress">0%</span><span>目标: <span id="stTarget">--</span> mL</span>
      </div>
    </div>
    <div class="card">
      <h2>运行控制</h2>
      <div class="btn-row">
        <button class="btn-start" onclick="send('start')" id="btnStart">▶ 启动</button>
        <button class="btn-pause" onclick="send('pause')" id="btnPause" disabled>⏸ 暂停</button>
        <button class="btn-stop" onclick="send('stop')" id="btnStop" disabled>■ 停止</button>
      </div>
    </div>
    <div class="card" style="text-align:center">
      <span style="font-size:11px;color:var(--text3)">
        累计: <span id="stTotal">--</span> mL | 管路: <span id="stTube">--</span>% | 电机: <span id="stMotor">--</span>
      </span>
    </div>
  </div>

  <!-- 页面2: 参数 -->
  <div class="page" id="pageParams">
    <div class="card">
      <div class="mode-tabs" id="modeTabs">
        <div class="mode-tab active" data-mode="VOLUME">体积</div>
        <div class="mode-tab" data-mode="TIME">定时</div>
        <div class="mode-tab" data-mode="JET">喷射</div>
      </div>
      <div style="display:flex;gap:6px;margin-bottom:8px">
        <span style="font-size:12px;color:var(--text2)">液体:</span>
        <div class="liquid-row" id="liquidRow" style="margin:0;flex:1"></div>
      </div>
    </div>
    <div class="card" id="paramCard">
      <h2>参数</h2>
      <div id="paramVolume"><div class="input-row"><label>流量</label><input type="number" id="inFlow" value="50" step="0.1" min="0.1" max="9999"><span class="unit">mL/min</span></div><div class="input-row"><label>体积</label><input type="number" id="inVolume" value="10" step="0.1" min="0.1" max="99999"><span class="unit">mL</span></div><button class="btn-action" onclick="setVal('set_flow','inFlow')">应用流量</button><button class="btn-action" onclick="setVal('set_volume','inVolume')">应用体积</button></div>
      <div id="paramTime" style="display:none"><div class="input-row"><label>体积</label><input type="number" id="inTimeVol" value="10" step="0.1" min="0.1" max="99999"><span class="unit">mL</span></div><div class="input-row"><label>时间</label><input type="number" id="inTimeSec" value="30" step="1" min="1" max="86400"><span class="unit">秒</span></div><button class="btn-action" onclick="setVal('set_volume','inTimeVol')">应用体积</button><button class="btn-action" onclick="setVal('set_time','inTimeSec')">应用时间</button></div>
      <div id="paramJet" style="display:none"><div class="jet-grid"><div><label>单次量 mL</label><input type="number" id="inJetVol" value="1" step="0.1" min="0.1" max="10"></div><div><label>间隔 秒</label><input type="number" id="inJetInt" value="3" step="1" min="1" max="60"></div><div><label>流量 mL/min</label><input type="number" id="inJetFlow" value="200" step="1" min="10" max="9999"></div><div><label>压力 1-10</label><input type="number" id="inJetPres" value="5" step="1" min="1" max="10"></div></div><button class="btn-action" onclick="setVal('set_jet_vol','inJetVol')">应用单次量</button><button class="btn-action" onclick="setVal('set_jet_interval','inJetInt')">应用间隔</button><button class="btn-action" onclick="setVal('set_jet_flow','inJetFlow')">应用流量</button><button class="btn-action" onclick="setVal('set_jet_pressure','inJetPres')">应用压力</button></div>
    </div>
  </div>

  <!-- 页面3: 工具 -->
  <div class="page" id="pageTools">
    <div class="card">
      <h2>方案预设</h2>
      <div class="btn-row">
        <button class="btn-sm" onclick="send('preset_load',0)">加载1</button>
        <button class="btn-sm" onclick="send('preset_load',1)">加载2</button>
        <button class="btn-sm" onclick="send('preset_load',2)">加载3</button>
        <button class="btn-sm" onclick="send('preset_load',3)">加载4</button>
      </div>
      <div class="btn-row" style="margin-top:4px">
        <button class="btn-sm" onclick="send('preset_save',0)">存入1</button>
        <button class="btn-sm" onclick="send('preset_save',1)">存入2</button>
        <button class="btn-sm" onclick="send('preset_save',2)">存入3</button>
        <button class="btn-sm" onclick="send('preset_save',3)">存入4</button>
      </div>
    </div>
    <div class="card">
      <h2>快捷操作</h2>
      <div class="btn-row">
        <button class="btn-sm" onclick="send('prime_start')">预灌</button>
        <button class="btn-sm" onclick="send('prime_stop')">停止</button>
        <button class="btn-sm" onclick="send('menu_main')">主菜单</button>
      </div>
    </div>
    <div class="card">
      <h2>校准</h2>
      <div class="btn-row">
        <button class="btn-sm" onclick="send('calib_enter')">进入</button>
        <button class="btn-sm" onclick="send('calib_start_run')">泵出</button>
        <button class="btn-sm" onclick="send('calib_stop_run')">停泵</button>
        <button class="btn-sm" onclick="send('calib_abort')">退出</button>
      </div>
      <div class="input-row"><label>实测mL</label><input type="number" id="inCalibML" value="10" step="0.1" min="0.1"><span class="unit">mL</span></div>
      <div class="btn-row">
        <button class="btn-action" onclick="setVal('calib_measure','inCalibML')">提交实测</button>
        <button class="btn-action" onclick="send('calib_save')" style="background:var(--green);color:#fff">保存校准</button>
      </div>
    </div>
  </div>

  <!-- 底部导航 -->
  <nav class="tab-bar">
    <div class="tab-item active" data-page="pageStatus"><span class="icon">📊</span>状态</div>
    <div class="tab-item" data-page="pageParams"><span class="icon">⚙️</span>参数</div>
    <div class="tab-item" data-page="pageTools"><span class="icon">🔧</span>工具</div>
  </nav>
</main>

<div class="toast" id="toast"></div>

<script>
const $=s=>document.querySelector(s),$$=s=>document.querySelectorAll(s);
const NUS_SVC='6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const NUS_TX='6e400003-b5a3-f393-e0a9-e50e24dcca9e';
const NUS_RX='6e400002-b5a3-f393-e0a9-e50e24dcca9e';

var currentMode='VOLUME',currentState='IDLE';
var bleDevice=null,bleTxChar=null,bleConnected=false;
var pollTimer=null;

// ==================== BLE ====================
async function connectBLE(){
  try{
    $('#bleStatus').textContent='扫描中...';
    // 先尝试名字过滤, 不行就显示所有设备
    try{
      bleDevice=await navigator.bluetooth.requestDevice({
        filters:[{namePrefix:'PumpCtrl'}],
        optionalServices:[NUS_SVC]
      });
    }catch(e){
      bleDevice=await navigator.bluetooth.requestDevice({
        acceptAllDevices:true,
        optionalServices:[NUS_SVC]
      });
    }
    $('#bleStatus').textContent='连接中...';
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
    $('#bleStatus').textContent='失败: '+e.message;
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
  updateConn('err','断开');
  $('#blePrompt').style.display='';
  $('#mainUI').classList.add('hide');
  $('#bleStatus').textContent='已断开';
  $('#bleBtn').textContent='🔵 蓝牙重连';
  if(pollTimer)clearInterval(pollTimer);
  tryWiFiFallback();
}

async function disconnectBLE(){
  if(bleDevice&&bleDevice.gatt.connected)await bleDevice.gatt.disconnect();
}

function onBLEConnected(){
  updateConn('ble','BLE');
  $('#bleStatus').textContent='BLE 已连接';
  $('#bleBtn').style.display='none';
  send('status');
  if(pollTimer)clearInterval(pollTimer);
  pollTimer=setInterval(()=>send('status'),1000);
}

function bleWrite(str){
  if(!bleTxChar)return;
  var enc=new TextEncoder().encode(str+'\\n');
  bleTxChar.writeValueWithoutResponse(enc).catch(()=>{});
}

// ==================== WiFi 回退 ====================
var useWiFi=false;
function tryWiFiFallback(){
  fetch('/api/status').then(r=>r.json()).then(d=>{
    if(d&&d.state){useWiFi=true;onWiFiConnected();}
  }).catch(()=>{});
}

function onWiFiConnected(){
  updateConn('ok','WiFi');
  $('#mainUI').classList.remove('hide');
  $('#bleStatus').textContent='WiFi 已连接';
  poll();
  if(pollTimer)clearInterval(pollTimer);
  pollTimer=setInterval(poll,1000);
}

// ==================== 发送命令 ====================
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
      if(r&&!r.ok)toast(r.error||'失败',true);
      else if(r&&r.ok)toast('OK');
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
      if(r&&!r.ok)toast(r.error||'失败',true);else if(r&&r.ok)toast('OK');
    }).catch(()=>{});
  }
}

// ==================== UI ====================
function updateConn(cls,txt){var d=$('#connDot'),t=$('#connText');d.className='dot '+cls;t.textContent=txt}

var stateIcons={IDLE:'⏸',RUNNING:'▶',PAUSED:'⏸',DONE:'✅','ANTI_DRIP':'↩'};

function updateUI(d){
  currentState=d.state;currentMode=d.mode;
  $('#stState').textContent=d.state;$('#stMode').textContent=d.mode;
  $('#stStateIcon').textContent=stateIcons[d.state]||'❓';
  $('#stFlow').textContent=d.flow.toFixed(1);
  $('#stDisp').textContent=d.dispensed.toFixed(2);
  $('#stTarget').textContent=d.targetVol.toFixed(1);
  $('#progressBar').style.width=d.progress+'%';
  $('#stProgress').textContent=d.progress+'%';
  $('#stTotal').textContent=d.totalDispensed.toFixed(0);
  $('#stTube').textContent=d.tubePct;
  $('#stMotor').textContent=d.stepperEnabled?'使能':'断电';
  $$('.mode-tab').forEach(t=>t.classList.toggle('active',t.dataset.mode===d.mode));
  var ae=document.activeElement;
  if(ae!==$('#inFlow'))$('#inFlow').value=d.flow;
  if(ae!==$('#inVolume'))$('#inVolume').value=d.targetVol;
  if(ae!==$('#inTimeVol'))$('#inTimeVol').value=d.targetVol;
  if(ae!==$('#inTimeSec'))$('#inTimeSec').value=d.targetTime;
  if(ae!==$('#inJetVol'))$('#inJetVol').value=d.jetVolume;
  if(ae!==$('#inJetInt'))$('#inJetInt').value=d.jetInterval;
  if(ae!==$('#inJetFlow'))$('#inJetFlow').value=d.jetFlowRate;
  if(ae!==$('#inJetPres'))$('#inJetPres').value=d.jetPressure;
  $('#paramVolume').style.display=d.mode==='VOLUME'?'':'none';
  $('#paramTime').style.display=d.mode==='TIME'?'':'none';
  $('#paramJet').style.display=d.mode==='JET'?'':'none';
  var idl=d.state==='IDLE',run=d.state==='RUNNING',pau=d.state==='PAUSED';
  $('#btnStart').disabled=!idl;$('#btnPause').disabled=!run;$('#btnStop').disabled=!(run||pau);
  if(d.liquidIdx!==undefined)buildLiquidRow(d.liquidIdx);
}

// WiFi poll
function poll(){
  fetch('/api/status').then(r=>r.json()).then(d=>{if(!d)return updateConn('err','Off');updateConn('ok','WiFi');updateUI(d)}).catch(()=>updateConn('err','Off'));
}

// Tabs
$$('.tab-item').forEach(t=>{t.addEventListener('click',()=>{
  $$('.tab-item').forEach(x=>x.classList.remove('active'));
  t.classList.add('active');
  $$('.page').forEach(p=>p.classList.remove('active'));
  $('#'+t.dataset.page).classList.add('active');
})});

// Mode tabs
$$('.mode-tab').forEach(t=>{t.addEventListener('click',()=>{
  if(t.dataset.mode!==currentMode){
    if(bleConnected){bleWrite('{"cmd":"set_mode","params":{"mode":"'+t.dataset.mode+'"}}')}
    else{send('set_mode&m='+t.dataset.mode)}
  }
})});

// Liquid
var liqNames=['水','粘稠','有机','自定义'];
function buildLiquidRow(idx){
  var r=$('#liquidRow');r.innerHTML='';
  liqNames.forEach((n,i)=>{var b=document.createElement('div');b.className='liquid-btn'+(i===idx?' active':'');b.textContent=n;b.onclick=()=>{
    if(bleConnected){bleWrite('{"cmd":"set_liquid","params":{"index":'+i+'}}')}
    else{send('set_liquid&i='+i)}
  };r.appendChild(b)})
}
buildLiquidRow(0);

// Toast
var tt;function toast(msg,isErr){var e=$('#toast');e.textContent=msg;e.className='toast show'+(isErr?' error':'');if(tt)clearTimeout(tt);tt=setTimeout(()=>e.classList.remove('show'),2000)}

// WiFi always preferred (100% reliable)
setTimeout(tryWiFiFallback,200);
$('#wifiFallback').innerHTML='或 <a href="#" onclick="event.preventDefault();tryWiFiFallback()" style="color:var(--blue)">使用WiFi</a>';
setTimeout(tryWiFiFallback,500);
</script>
</body>
</html>"""

# Write as a C++ raw literal string
cpp = 'static const char WEB_UI[] PROGMEM = R"rawliteral(\n'
cpp += html
cpp += '\n)rawliteral";'

with open(r'C:\Users\xg821\peristaltic_pump\web_ui_gen.h', 'w', encoding='utf-8') as f:
    f.write(cpp)

print(f"Generated web_ui_gen.h: {len(html)} chars HTML, {len(cpp)} bytes total")
