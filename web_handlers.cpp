/******************************************************************************
 * web_handlers.cpp - HTTP 服务实现 (WiFiServer, 无 AsyncTCP 依赖)
 *
 * 使用 ESP32 内置 WiFiServer/WiFiClient, 不依赖 AsyncTCP/ESPAsyncWebServer
 * 彻底解决 Core 3.x 的 tcp_alloc LWIP 锁冲突
 *
 * Web UI 通过 HTTP 轮询 /api/status 获取实时数据
 * 命令通过 /api/cmd?c=xxx 发送
 ******************************************************************************/
#include "web_handlers.h"
#include "command_protocol.h"
#include "wifi_manager.h"
#include "pump_shared.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// ============================================================================
//                            HTTP 服务器
// ============================================================================
static WiFiServer server(80);

// 内嵌 Web UI (编译进固件, 无需 LittleFS)
static const char WEB_UI[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0,user-scalable=no">
<title>蠕动泵控制器</title>
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
:root{
  --bg:#0d1117;--card:#161b22;--border:#30363d;
  --accent:#e94560;--accent2:#0f3460;--green:#238636;--yellow:#d2991d;--red:#da3633;
  --text:#e6edf3;--text2:#8b949e;--text3:#484f58;
}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:var(--bg);color:var(--text);min-height:100vh;padding-bottom:env(safe-area-inset-bottom)}
header{background:var(--card);border-bottom:1px solid var(--border);padding:12px 16px;display:flex;align-items:center;justify-content:space-between;position:sticky;top:0;z-index:10}
header h1{font-size:18px;font-weight:600}
#connDot{width:10px;height:10px;border-radius:50%;display:inline-block;margin-right:6px;transition:background .3s}
#connDot.ok{background:var(--green)}#connDot.warn{background:var(--yellow)}#connDot.err{background:var(--red)}
main{max-width:480px;margin:0 auto;padding:12px}
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
</style>
</head>
<body>
<header>
  <div><span id="connDot" class="err"></span><span id="connText">--</span></div>
  <h1>蠕动泵</h1>
  <div style="font-size:12px;color:var(--text3)" id="wifiInfo"></div>
</header>
<main>
  <div class="card">
    <h2>实时状态</h2>
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
    <div class="mode-tabs" id="modeTabs">
      <div class="mode-tab active" data-mode="VOLUME">体积模式</div>
      <div class="mode-tab" data-mode="TIME">定时模式</div>
      <div class="mode-tab" data-mode="JET">喷射模式</div>
    </div>
    <div style="display:flex;gap:6px;margin-bottom:8px">
      <span style="font-size:12px;color:var(--text2)">液体:</span>
      <div class="liquid-row" id="liquidRow" style="margin:0;flex:1"></div>
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
  <div class="card" id="paramCard">
    <h2>参数设置</h2>
    <div id="paramVolume"><div class="input-row"><label>流量</label><input type="number" id="inFlow" value="50" step="0.1" min="0.1" max="2000"><span class="unit">mL/min</span></div><div class="input-row"><label>体积</label><input type="number" id="inVolume" value="10" step="0.1" min="0.1" max="999.9"><span class="unit">mL</span></div><button class="btn-action" onclick="setVal('set_flow','inFlow')">应用流量</button><button class="btn-action" onclick="setVal('set_volume','inVolume')">应用体积</button></div>
    <div id="paramTime" style="display:none"><div class="input-row"><label>体积</label><input type="number" id="inTimeVol" value="10" step="0.1" min="0.1" max="999.9"><span class="unit">mL</span></div><div class="input-row"><label>时间</label><input type="number" id="inTimeSec" value="30" step="1" min="1" max="86400"><span class="unit">秒</span></div><button class="btn-action" onclick="setVal('set_volume','inTimeVol')">应用体积</button><button class="btn-action" onclick="setVal('set_time','inTimeSec')">应用时间</button></div>
    <div id="paramJet" style="display:none"><div class="jet-grid"><div><label>单次量 mL</label><input type="number" id="inJetVol" value="1" step="0.1" min="0.1" max="10"></div><div><label>间隔 秒</label><input type="number" id="inJetInt" value="3" step="1" min="1" max="60"></div><div><label>流量 mL/min</label><input type="number" id="inJetFlow" value="200" step="1" min="10" max="2000"></div><div><label>压力 1-10</label><input type="number" id="inJetPres" value="5" step="1" min="1" max="10"></div></div><button class="btn-action" onclick="setVal('set_jet_vol','inJetVol')">应用单次量</button><button class="btn-action" onclick="setVal('set_jet_interval','inJetInt')">应用间隔</button><button class="btn-action" onclick="setVal('set_jet_flow','inJetFlow')">应用流量</button><button class="btn-action" onclick="setVal('set_jet_pressure','inJetPres')">应用压力</button></div>
  </div>
  <div class="card">
    <h2>快捷操作</h2>
    <div class="btn-row">
      <button class="btn-sm" onclick="send('prime_start')">预灌/快排</button>
      <button class="btn-sm" onclick="send('prime_stop')">停止预灌</button>
      <button class="btn-sm" onclick="send('menu_main')">回主菜单</button>
      <button class="btn-sm" onclick="send('preset_load',0)">方案1</button>
      <button class="btn-sm" onclick="send('preset_load',1)">方案2</button>
    </div>
  </div>
  <div class="card" style="text-align:center">
    <span style="font-size:11px;color:var(--text3)">
      累计: <span id="stTotal">--</span> mL | 管路: <span id="stTube">--</span>% | 电机: <span id="stMotor">--</span>
    </span>
  </div>
</main>
<div class="toast" id="toast"></div>
<script>
const $=s=>document.querySelector(s),$$=s=>document.querySelectorAll(s);
var currentMode='VOLUME',currentState='IDLE';

function fetchJSON(url,opts={}){return fetch(url,opts).then(r=>r.json()).catch(e=>null)}

function poll(){
  fetchJSON('/api/status').then(d=>{if(!d)return updateConn('err','断开');
    updateConn('ok','已连接');updateUI(d)});
}

function send(cmd,slot){
  var url='/api/cmd?c='+cmd;
  if(slot!==undefined) url+='&s='+slot;
  fetchJSON(url).then(r=>{if(r&&!r.ok)toast(r.error||'失败',true);else if(r&&r.ok)toast('✓')});
}

function setVal(cmd,id){var v=document.getElementById(id).value;send(cmd+'&v='+v)}

function updateConn(cls,txt){var d=$('#connDot'),t=$('#connText');d.className=cls;t.textContent=txt}

function updateUI(d){
  currentState=d.state;currentMode=d.mode;
  $('#stState').textContent=d.state;$('#stMode').textContent=d.mode;
  $('#stFlow').textContent=d.flow.toFixed(1);$('#stDisp').textContent=d.dispensed.toFixed(2);
  $('#stTarget').textContent=d.targetVol.toFixed(1);$('#progressBar').style.width=d.progress+'%';
  $('#stProgress').textContent=d.progress+'%';$('#stTotal').textContent=d.totalDispensed.toFixed(0);
  $('#stTube').textContent=d.tubePct;$('#stMotor').textContent=d.stepperEnabled?'使能':'断电';
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
  if(d.wifiMode){var info=d.wifiMode;if(d.wifiIP)info+=' | '+d.wifiIP;$('#wifiInfo').textContent=info}
}

$$('.mode-tab').forEach(t=>{t.addEventListener('click',()=>{if(t.dataset.mode!==currentMode)send('set_mode&m='+t.dataset.mode)})});

var liqNames=['水','粘稠','有机','自定义'];
function buildLiquidRow(idx){
  var r=$('#liquidRow');r.innerHTML='';
  liqNames.forEach((n,i)=>{var b=document.createElement('div');b.className='liquid-btn'+(i===idx?' active':'');b.textContent=n;b.onclick=()=>send('set_liquid&i='+i);r.appendChild(b)})
}
buildLiquidRow(0);

var tt;function toast(msg,isErr){var e=$('#toast');e.textContent=msg;e.className='toast show'+(isErr?' error':'');if(tt)clearTimeout(tt);tt=setTimeout(()=>e.classList.remove('show'),2000)}

poll();setInterval(poll,1000);
</script>
</body>
</html>
)rawliteral";

// ============================================================================
//                            HTTP 请求处理
// ============================================================================

// 发送 JSON 响应
static void sendJson(WiFiClient &client, int code, const char* json) {
  client.printf("HTTP/1.1 %d OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n%s", code, json);
}

// 发送 HTML 响应
static void sendHtml(WiFiClient &client, int code, const char* html) {
  client.printf("HTTP/1.1 %d OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n%s", code, html);
}

// 从 URL 中提取查询参数值
static String getQueryParam(const String &url, const char* key) {
  String k = String(key) + "=";
  int start = url.indexOf(k);
  if (start < 0) return "";
  start += k.length();
  int end = url.indexOf('&', start);
  if (end < 0) end = url.indexOf(' ', start);
  if (end < 0) end = url.length();
  return url.substring(start, end);
}

// 处理单个 HTTP 请求
static void handleRequest(WiFiClient &client, const String &method, const String &path) {
  // GET / 或 /index.html -> Web UI
  if (method == "GET" && (path == "/" || path.startsWith("/index.html"))) {
    sendHtml(client, 200, WEB_UI);
    return;
  }

  // GET /api/status -> 遥测 JSON
  if (method == "GET" && path.startsWith("/api/status")) {
    const char* json = buildTelemetryJson();
    String resp = "{\"type\":\"telemetry\",";
    resp += (json + 1);  // 跳过 buildTelemetryJson 的起始 '{'
    sendJson(client, 200, resp.c_str());
    return;
  }

  // GET /api/cmd?c=xxx&v=yyy&s=zzz&m=mmm&i=iii -> 命令
  if (method == "GET" && path.startsWith("/api/cmd")) {
    String cmd = getQueryParam(path, "c");
    String val = getQueryParam(path, "v");
    String slot = getQueryParam(path, "s");
    String mode = getQueryParam(path, "m");
    String idx = getQueryParam(path, "i");

    // 构建 JSON 命令
    String jsonCmd;
    if (cmd.length() == 0) {
      sendJson(client, 400, "{\"ok\":false,\"error\":\"Missing cmd\"}");
      return;
    }

    // 构建 params
    String params;
    if (val.length() > 0) params += "\"value\":" + val;
    if (slot.length() > 0) {
      if (params.length() > 0) params += ",";
      params += "\"slot\":" + slot;
    }
    if (mode.length() > 0) {
      if (params.length() > 0) params += ",";
      params += "\"mode\":\"" + mode + "\"";
    }
    if (idx.length() > 0) {
      if (params.length() > 0) params += ",";
      params += "\"index\":" + idx;
    }

    if (params.length() > 0) {
      jsonCmd = "{\"cmd\":\"" + cmd + "\",\"params\":{" + params + "}}";
    } else {
      jsonCmd = "{\"cmd\":\"" + cmd + "\",\"params\":{}}";
    }

    const char* resp = parseAndExecute(jsonCmd.c_str());
    sendJson(client, 200, resp);
    return;
  }

  // 404
  sendJson(client, 404, "{\"ok\":false,\"error\":\"Not found\"}");
}

// ============================================================================
//                            初始化 & 客户端轮询
// ============================================================================

void initWebServer() {
  server.begin();
}

void handleWebClients() {
  WiFiClient client = server.accept();
  if (!client) return;

  // 读取 HTTP 请求的第一行 (最多等 50ms)
  unsigned long timeout = millis() + 50;
  String request;
  while (client.connected() && millis() < timeout) {
    if (client.available()) {
      char c = client.read();
      request += c;
      if (request.endsWith("\r\n\r\n")) break;
      if (request.endsWith("\n\n")) break;
      timeout = millis() + 50;
    }
  }

  // 快速消费剩余数据
  while (client.available()) client.read();

  // 解析方法 & 路径
  int firstSpace = request.indexOf(' ');
  int secondSpace = request.indexOf(' ', firstSpace + 1);
  if (firstSpace < 0 || secondSpace < 0) { client.stop(); return; }
  String method = request.substring(0, firstSpace);
  String path = request.substring(firstSpace + 1, secondSpace);

  handleRequest(client, method, path);
  client.stop();
}
