// ============================================================
// 蠕动泵 XToys 脚本 v2.3.6
// 连接: ESP32 UART1 (GPIO21=RX, GPIO47=TX) -> USB-TTL -> PC
// 串口: 115200 baud / 8N1 / None
// ============================================================

({
    // ==========================================================
    // 区块
    // ==========================================================
    blocks: [
        {
            id: "pump",
            type: "serial",
            name: "蠕动泵",
            config: {
                baudRate: 115200,
                dataBits: 8,
                stopBits: 1,
                parity: "none"
            }
        }
    ],

    // ==========================================================
    // 控件
    // ==========================================================
    controls: [
        // ---- 模式 & 液体 ----
        { id:"mode", type:"select", name:"模式", variable:"mode",
          options:[
            {label:"体积模式",value:"VOLUME"},
            {label:"定时模式",value:"TIME"},
            {label:"喷射模式",value:"JET"}
          ], default:"VOLUME" },

        { id:"liquid", type:"select", name:"液体", variable:"liquidIdx",
          options:[
            {label:"水",value:0},{label:"粘稠",value:1},
            {label:"液体1",value:2},{label:"液体2",value:3}
          ], default:0 },

        // ---- 体积/时间模式参数 ----
        { id:"flow",   type:"slider", name:"流量 mL/min", variable:"flowRate",
          min:0.1, max:2000, step:1, default:50 },

        { id:"volume", type:"slider", name:"目标体积 mL", variable:"targetVol",
          min:0.1, max:99999, step:1, default:10 },

        { id:"timeSec", type:"slider", name:"目标时间 s", variable:"targetTime",
          min:1, max:86400, step:1, default:30 },

        // ---- 喷射参数 ----
        { id:"jetVol",  type:"slider", name:"单次喷射量 mL", variable:"jetVolume",
          min:0.1, max:10, step:0.1, default:1 },

        { id:"jetInt",  type:"slider", name:"喷射间隔 s", variable:"jetInterval",
          min:1, max:60, step:1, default:3 },

        { id:"jetFlow", type:"slider", name:"喷射流量 mL/min", variable:"jetFlowRate",
          min:10, max:2000, step:10, default:200 },

        { id:"jetPres", type:"slider", name:"喷射压力 (1柔-10猛)", variable:"jetPressure",
          min:1, max:10, step:1, default:5 },

        // ---- 高级设置 ----
        { id:"antiDrip", type:"slider", name:"回吸量 mL", variable:"antiDripVol",
          min:0, max:5, step:0.05, default:0.05 },

        { id:"tubeLife", type:"slider", name:"管路寿命 mL (0=不限)", variable:"tubeLifeML",
          min:0, max:200000, step:1000, default:50000 },

        // ---- 按钮 ----
        { id:"btnStart",  type:"button", name:"▶ 启动",    action:"start" },
        { id:"btnPause",  type:"button", name:"⏸ 暂停",    action:"pause" },
        { id:"btnStop",   type:"button", name:"■ 停止",    action:"stop" },
        { id:"btnPrime",  type:"button", name:"预灌/快排", action:"prime" },

        // ---- 方案预设 ----
        { id:"btnLoad1", type:"button", name:"加载方案1", action:"presetLoad1" },
        { id:"btnLoad2", type:"button", name:"加载方案2", action:"presetLoad2" },
        { id:"btnSave1", type:"button", name:"存入方案1", action:"presetSave1" },

        // ---- 校准 ----
        { id:"btnCalib", type:"button", name:"校准向导", action:"calibEnter" },

        // ---- 状态显示 ----
        { id:"labelStatus", type:"display", name:"状态", variable:"statusText" },
        { id:"labelMode",   type:"display", name:"当前模式", variable:"currentMode" },
        { id:"labelDisp",   type:"display", name:"已出 mL", variable:"dispText" },
        { id:"labelProg",   type:"display", name:"进度", variable:"progText" },
        { id:"labelTotal",  type:"display", name:"累计 mL", variable:"totalText" },
        { id:"labelTube",   type:"display", name:"管路寿命", variable:"tubeText" }
    ],

    // ==========================================================
    // 内部状态
    // ==========================================================
    variables: {
        connected: false,
        serialBuf: "",
        pollTimer: null,
        statusText: "未连接",
        currentMode: "VOLUME",
        dispText: "--",
        progText: "--",
        totalText: "--",
        tubeText: "--",
        priming: false  // 预灌状态追踪
    },

    // ==========================================================
    // 辅助: 发送 JSON 命令
    // ==========================================================
    _send: function(pump, cmd, params) {
        if (!pump || !pump.isOpen) return;
        var msg = JSON.stringify({ cmd: cmd, params: params || {} });
        pump.write(msg + "\n");
    },

    // 启动时全量同步所有参数到泵
    _applyAll: function(blocks, vars) {
        var p = blocks.pump;
        if (!p || !p.isOpen) return;
        this._send(p, "set_mode",       { mode: vars.mode });
        this._send(p, "set_liquid",     { index: parseInt(vars.liquidIdx) || 0 });
        this._send(p, "set_flow",       { value: parseFloat(vars.flowRate) || 50 });
        this._send(p, "set_volume",     { value: parseFloat(vars.targetVol) || 10 });
        this._send(p, "set_time",       { value: parseFloat(vars.targetTime) || 30 });
        this._send(p, "set_jet_vol",    { value: parseFloat(vars.jetVolume) || 1 });
        this._send(p, "set_jet_interval",{ value: parseFloat(vars.jetInterval) || 3 });
        this._send(p, "set_jet_flow",   { value: parseFloat(vars.jetFlowRate) || 200 });
        this._send(p, "set_jet_pressure",{ value: parseInt(vars.jetPressure) || 5 });
        this._send(p, "set_anti_drip",  { value: parseFloat(vars.antiDripVol) || 0.05 });
        this._send(p, "set_tube_life",  { value: parseFloat(vars.tubeLifeML) || 50000 });
    },

    // 解析一行 JSON 响应
    _parseLine: function(line, vars) {
        line = line.trim();
        if (!line) return;
        try {
            var msg = JSON.parse(line);
            if (msg.type === "hello") {
                vars.connected = true;
                vars.statusText = "已连接";
            } else if (msg.type === "telemetry" || (msg.type === "response" && msg.ok && msg.data)) {
                // get_state 返回的 data 包含完整遥测
                var d = msg.data || msg;
                vars.connected = true;
                vars.statusText = d.state || "?";
                vars.currentMode = d.mode || "?";
                vars.dispText   = (d.dispensed || 0).toFixed(1);
                vars.progText   = (d.progress || 0) + "%";
                vars.totalText  = (d.totalDispensed || 0).toFixed(0);
                vars.tubeText   = (d.tubePct || 0) + "%";
                // 追踪预灌状态
                if (d.state === "IDLE") vars.priming = false;
            } else if (msg.type === "response" && !msg.ok) {
                vars.statusText = "ERR: " + (msg.error || "?");
            }
        } catch(e) { /* skip malformed JSON */ }
    },

    // ==========================================================
    // 脚本启动
    // ==========================================================
    start: function(blocks, vars, controls, triggers, jobs) {
        var self = this;
        var pump = blocks.pump;

        if (!pump) {
            vars.statusText = "未找到串口区块";
            return;
        }

        // 串口数据回调
        pump.onData = function(data) {
            vars.serialBuf += data;
            // 按换行切分完整 JSON 行
            while (vars.serialBuf.indexOf("\n") !== -1) {
                var idx = vars.serialBuf.indexOf("\n");
                var line = vars.serialBuf.substring(0, idx);
                vars.serialBuf = vars.serialBuf.substring(idx + 1);
                self._parseLine(line, vars);
            }
        };

        // 等待串口就绪后全量同步参数
        setTimeout(function() {
            if (pump && pump.isOpen) {
                self._applyAll(blocks, vars);
                self._send(pump, "get_state");
            }
        }, 800);  // 给 ESP32 足够时间发送 hello

        // 每秒轮询遥测
        vars.pollTimer = setInterval(function() {
            if (pump && pump.isOpen) {
                self._send(pump, "get_state");
            }
        }, 1000);

        vars.priming = false;
    },

    // ==========================================================
    // 脚本停止
    // ==========================================================
    stop: function(blocks, vars) {
        this._send(blocks.pump, "stop");
        if (vars.pollTimer) { clearInterval(vars.pollTimer); vars.pollTimer = null; }
        vars.priming = false;
        vars.statusText = "已停止";
    },

    // ==========================================================
    // 全局触发器
    // ==========================================================
    globalTriggers: [
        // ---- 控件变化 → 实时同步到泵 ----
        { variable:"mode",       on:"change", action:function(b,v){this._send(b.pump,"set_mode",{mode:v.mode})} },
        { variable:"liquidIdx",  on:"change", action:function(b,v){this._send(b.pump,"set_liquid",{index:parseInt(v.liquidIdx)})} },
        { variable:"flowRate",   on:"change", action:function(b,v){this._send(b.pump,"set_flow",{value:parseFloat(v.flowRate)})} },
        { variable:"targetVol",  on:"change", action:function(b,v){this._send(b.pump,"set_volume",{value:parseFloat(v.targetVol)})} },
        { variable:"targetTime", on:"change", action:function(b,v){this._send(b.pump,"set_time",{value:parseFloat(v.targetTime)})} },
        { variable:"jetVolume",  on:"change", action:function(b,v){this._send(b.pump,"set_jet_vol",{value:parseFloat(v.jetVolume)})} },
        { variable:"jetInterval",on:"change", action:function(b,v){this._send(b.pump,"set_jet_interval",{value:parseFloat(v.jetInterval)})} },
        { variable:"jetFlowRate",on:"change", action:function(b,v){this._send(b.pump,"set_jet_flow",{value:parseFloat(v.jetFlowRate)})} },
        { variable:"jetPressure",on:"change", action:function(b,v){this._send(b.pump,"set_jet_pressure",{value:parseInt(v.jetPressure)})} },
        { variable:"antiDripVol",on:"change", action:function(b,v){this._send(b.pump,"set_anti_drip",{value:parseFloat(v.antiDripVol)})} },
        { variable:"tubeLifeML", on:"change", action:function(b,v){this._send(b.pump,"set_tube_life",{value:parseFloat(v.tubeLifeML)})} },

        // ---- 按钮动作 ----
        { action:"start",   on:"action", handler:function(b,v){this._send(b.pump,"start")} },
        { action:"pause",   on:"action", handler:function(b,v){this._send(b.pump,"pause")} },
        { action:"stop",    on:"action", handler:function(b,v){v.priming=false; this._send(b.pump,"stop")} },
        { action:"prime",   on:"action", handler:function(b,v){
            if (v.priming) { this._send(b.pump,"prime_stop"); v.priming = false; v.statusText = "预灌已停"; }
            else           { this._send(b.pump,"prime_start"); v.priming = true;  v.statusText = "预灌中..."; }
        }},
        { action:"calibEnter",  on:"action", handler:function(b,v){this._send(b.pump,"calib_enter")} },
        { action:"presetLoad1", on:"action", handler:function(b,v){this._send(b.pump,"preset_load",{slot:0})} },
        { action:"presetLoad2", on:"action", handler:function(b,v){this._send(b.pump,"preset_load",{slot:1})} },
        { action:"presetSave1", on:"action", handler:function(b,v){this._send(b.pump,"preset_save",{slot:0})} }
    ]
})
