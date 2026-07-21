// ============================================================
// 蠕动泵 XToys JavaScript v2.3.6
// 点 XToys 脚本编辑器顶部 JS 按钮，粘贴此文件全部内容
// ============================================================

var pump = null;
var serialBuf = "";
var pollTimer = null;
var priming = false;

// ---- 工具函数 ----
function sendCmd(cmd, params) {
    if (!pump || !pump.isOpen) return;
    pump.write(JSON.stringify({ cmd: cmd, params: params || {} }) + "\n");
}

function parseLine(line) {
    line = line.trim();
    if (!line) return;
    try {
        var msg = JSON.parse(line);
        if (msg.type === "hello") {
            setVariable("statusText", "已连接");
        } else if (msg.type === "telemetry" || (msg.ok && msg.data)) {
            var d = msg.data || msg;
            setVariable("statusText", d.state || "?");
            setVariable("dispText", (d.dispensed || 0).toFixed(1));
            setVariable("progText", (d.progress || 0) + "%");
            setVariable("totalText", (d.totalDispensed || 0).toFixed(0));
            setVariable("tubeText", (d.tubePct || 0) + "%");
            if (d.state === "IDLE") priming = false;
        }
    } catch(e) {}
}

// ---- 预灌按钮切换逻辑 (在 prime 按钮的 Trigger 中调用) ----
function togglePrime() {
    if (priming) {
        sendCmd("prime_stop");
        priming = false;
        setVariable("statusText", "预灌已停");
    } else {
        sendCmd("prime_start");
        priming = true;
        setVariable("statusText", "预灌中...");
    }
}

// ---- 脚本生命周期 ----
function start() {
    var blocks = getConnectedBlocks();
    for (var i = 0; i < blocks.length; i++) {
        if (blocks[i].type === "serial") { pump = blocks[i]; break; }
    }
    if (!pump) { setVariable("statusText", "无串口"); return; }

    pump.onData = function(data) {
        serialBuf += data;
        while (serialBuf.indexOf("\n") !== -1) {
            var idx = serialBuf.indexOf("\n");
            parseLine(serialBuf.substring(0, idx));
            serialBuf = serialBuf.substring(idx + 1);
        }
    };

    // 等待串口就绪后全量同步
    setTimeout(function() {
        if (pump && pump.isOpen) {
            // 发送当前所有控件的值
            sendCmd("set_mode",    { mode: getVariable("mode") });
            sendCmd("set_liquid",  { index: parseInt(getVariable("liquidIdx")) || 0 });
            sendCmd("set_flow",    { value: parseFloat(getVariable("flowRate")) || 50 });
            sendCmd("set_volume",  { value: parseFloat(getVariable("targetVol")) || 10 });
            sendCmd("set_time",    { value: parseFloat(getVariable("targetTime")) || 30 });
            sendCmd("set_jet_vol", { value: parseFloat(getVariable("jetVolume")) || 1 });
            sendCmd("set_jet_interval", { value: parseFloat(getVariable("jetInterval")) || 3 });
            sendCmd("set_jet_flow",{ value: parseFloat(getVariable("jetFlowRate")) || 200 });
            sendCmd("set_jet_pressure", { value: parseInt(getVariable("jetPressure")) || 5 });
            sendCmd("set_anti_drip", { value: parseFloat(getVariable("antiDripVol")) || 0.05 });
            sendCmd("set_tube_life", { value: parseFloat(getVariable("tubeLifeML")) || 50000 });
            sendCmd("get_state");
        }
    }, 1000);

    pollTimer = setInterval(function() {
        if (pump && pump.isOpen) sendCmd("get_state");
    }, 1000);

    priming = false;
    setVariable("statusText", "连接中...");
}

function stop() {
    sendCmd("stop");
    if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
    priming = false;
    setVariable("statusText", "已停止");
}
