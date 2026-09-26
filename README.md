# ✅ 蠕动泵控制器 — YZ1515 精密点液/喷射工作站  `DM542`

> ⚠️ **DM542 驱动版本（归档分支）** · 当前活跃开发: [`tmc2226`](https://github.com/784318839-stack/peristaltic-pump/tree/tmc2226) 分支（TMC2226 驱动, v2.4.1）

基于 ESP32-S3 的蠕动泵智能控制器，驱动 YZ1515 工业泵头，实现**体积模式、时间模式、喷射模式**三种精密流体控制。步进驱动采用 **DM542 数字驱动器 + 6N137 光耦隔离**。

> **v2.3.8** (2026-07-30) — DM542 分支归档，TMC2226 版本独立维护于 `tmc2226` 分支。
> **v2.3.6** (2026-07-21) — UART 响应缓冲区扩大，CLI DTR 修复，串口超时优化。
> **v2.3.5** (2026-07-19) — 软件打磨完成，WiFi 扫描重写，高级设置独立，校准流程修复，电机丢步已解决。
> **v2.3.4** (2026-07-18) — WiFi 全功率恢复 20dBm，6N137 光耦隔离，Web UI 修复。
> **v2.3.2** (2026-07-12) — PumpState 结构体重构，pump_machine 状态机模块提取。
> **v2.3.1** (2026-07-10) — FastAccelStepper RMT 迁移，DM542 共阴接法，400 细分。
> **v2.2** (2026-07-08) — PSRAM 全面启用，WiFi AP+STA 双模，堵转检测，密码加密。

---

## 控制方式 (v2.0)

| 接口 | 说明 |
|---|---|
| **WiFi Web UI** | 手机/PC 浏览器直连，PWA 可添加到桌面 |
| **USB Serial** | 通过 USB-CDC 串口发送 JSON 命令 (115200bps) |
| **Hardware UART** | GPIO21=RX, 47=TX, 115200bps — USB-TTL 直连 PC |

三通道共用同一套 **JSON 命令协议**，可同时使用。

---

## WiFi 网络

- **双模启动**: WIFI_AP_STA 同时运行，无需模式切换
  - SoftAP 始终可用: `PumpCtrl-XXXX`（密码 12345678）
  - IP: `http://192.168.4.1`（直连）或 `http://pump.local`（mDNS）
- **STA 连接家里 WiFi**: Web UI → WiFi 设置 → 输入 SSID/密码 → 保存
  - 连接成功后页面弹出 `✅ WiFi 已连接` 提示
  - header 实时显示可访问地址 `http://STA_IP | http://pump.local`
  - STA 连接失败不影响 SoftAP；断线重连由 esp_wifi 自身负责（`WiFi.begin()` 默认开启自动重连），固件不再轮询维护
  - 密码 XOR 加密存储 (设备 MAC 密钥)，拆机读 EEPROM 是乱码
- 配置保存在 EEPROM，掉电不丢失
- **WiFi 扫描**: 同步扫描 (~8s, 结果缓存 15s), 自动过滤自身 AP, 点击 SSID 自动填入
- **密码可见**: 👁 按钮切换明文/密文

---

## Web UI 功能

内嵌单页应用 (SPA)，手机端 PWA 支持：

- 📊 **实时仪表盘** — 状态、模式、进度、流量、体积、时间、温度
- 🎮 **运行控制** — 启动/暂停/恢复/停止
- ⚙️ **参数设置** — 流量、体积、时间、喷射参数
- 🔧 **模式切换** — 体积/时间/喷射模式一键切换
- 💧 **液体选择** — 4 种液体独立校准参数切换
- 📐 **校准向导** — 5 步引导式校准流程
- 💾 **方案预设** — 4 槽位加载/保存
- 🚿 **预灌快排** — 全速排空管路
- 📡 **WiFi 管理** — 扫描网络、配置连接、密码明文切换
- ⚙️ **高级设置** — 独立面板：回吸量 / 管路寿命设定
- 📈 **管路寿命** — 累计流量百分比显示
- 💾 **PSRAM 监控** — 页面底部实时显示 PSRAM 和堆内存使用
- 🔗 **访问地址** — header 显示当前可访问 URL (IP + mDNS)
- ✅ **WiFi 状态** — STA 连接成功自动弹出提示
- 🔐 **密码加密** — WiFi 密码 XOR 加密存储 (设备 MAC 密钥，拆机读 EEPROM 是乱码)

---

## 硬件配置

| 部件 | 型号 / 规格 |
|---|---|
| 主控 | ESP32-S3-WROOM-1-**N16R8** (16MB Flash + **8MB Octal PSRAM**) |
| 泵头 | YZ1515 (工业级, 100×80×80mm) |
| 电机 | 57 步进电机 (1.5Ω/相) |
| 驱动器 | **DM542** 数字式步进驱动器 (共阴接法, 400 pulse/rev) |
| 信号隔离 | **6N137** 高速光耦模块 (10MBd) |
| 蜂鸣器 | 无源蜂鸣器 GPIO5 |
| WS2812 LED | 状态指示灯 (待机绿/运行蓝/暂停琥珀/完成绿闪) |
| 显示屏 | SH1106 OLED 128×64 I2C *(v2.0 已停用)* |
| 键盘 | 4×4 矩阵键盘 *(v2.0 已停用)* |

---

## 功能清单 (15 项)

### 泵送模式

1. **体积模式** — 设定流量 (0.1–1600 mL/min) + 目标体积 (0.1–99999 mL)，恒速运行自动停止
2. **时间模式** — 设定体积 + 时间 (1–86400s)，运行时按 体积/时间 反算实际流量
   （存入 `activeFlowRate`，**不覆盖**用户设定的 `flowRate`）
3. **喷射模式** — 设定单次量 (0.1–10 mL) + 间隔 (1–60s) + 流量 (10–1600 mL/min) + 压力 (1–10 级)，循环喷射
   - 双参数控制：流量管远近、压力管爆发力
   - 无回吸

### 运行控制

4. **暂停/恢复** — 运行中暂停；等电机真正停稳后再记录断点，恢复不会多打
5. **防滴回吸** — 完成后反转吸回 (ANTI_DRIP 状态)。注意 `stop`/`reset` **可以**打断它
6. **预灌/快排** — 全速 1500 mL/min 排空管路
7. **速度曲线** — FastAccelStepper 匀加速启动；停止走 `forceStop()` 硬停（无减速斜坡）

### 校准 & 液体

8. **校准向导** — 5 步引导（选液体 → 设体积 → 运行 → 读取量筒 → 计算结果并保存）
9. **4 种液体独立校准** — 水 / 粘稠 / 液体1 / 液体2，每种独立 stepsPerMl

### 存储

10. **EEPROM 掉电记忆** — 全部参数 + 4 液体校准 + WiFi 配置

### 智能保护

11. **管路寿命追踪** — 累计流量统计，达到设定值提醒更换 (0=禁用)

### 反馈

12. **非阻塞蜂鸣器** — 5 种音效（确认/取消/启动/暂停/完成）
13. **WS2812 状态灯** — 颜色随泵状态变化，管路寿命告警红灯闪烁
14. **实时遥测** — `/api/status` 返回完整 JSON（状态/模式/参数/进度/WiFi 信息）

### 高级

15. **多通道命令入口** — WiFi Web UI + USB Serial + 硬件 UART 共用同一套 JSON 协议，全部在 `loop()` 单线程内串行执行

---

## XToys 集成

~~蠕动泵可通过硬件 UART 接入 [XToys](https://xtoys.app/) 平台。~~ 已放弃，串口功能保留。

### 物理连接

```
PC (USB) → USB-TTL 转接板 → ESP32
           TX ────────────→ GPIO 21 (RX)
           RX ────────────← GPIO 47 (TX)
           GND ──────────── GND
```

### 状态

⚠️ **XToys 集成已放弃开发** (2026-07-22)。硬件串口功能仍然保留，可通过 USB-TTL 或 USB CDC 使用 JSON 命令协议。现有脚本文件仅供后续参考：

| 文件 | 说明 |
|------|------|
| `xtoys_script.js` | 完整导出格式（含 blocks/controls/triggers） |
| `xtoys_js_only.js` | 纯 JS 逻辑（配合 GUI 手动配置使用） |

---

## JSON 命令协议

所有控制接口（WiFi / USB Serial / 硬件 UART）共用同一套协议。

### HTTP API

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/` | Web UI 页面 |
| GET | `/api/status` | 完整遥测 JSON |
| GET | `/api/cmd?c=<json>` | 发送命令 |
| POST | `/api/wifi` | 配置 WiFi 连接（响应发完后固件才重启网络） |
| GET | `/api/scan` | 扫描附近 WiFi 网络 (同步阻塞 ~8s, 结果缓存 15s, 泵运行时拒绝) |
| GET | `/manifest.json` | PWA 清单 |

### 命令列表

| 命令 | 参数 | 说明 |
|---|---|---|
| `start` | — | 启动泵送/喷射循环 |
| `pause` | — | 暂停运行 |
| `resume` | — | 恢复运行 |
| `stop` / `reset` | — | 停止并复位 |
| `set_mode` | `mode`: "VOLUME"/"TIME"/"JET" | 切换泵送模式 |
| `set_liquid` | `index`: 0–3 | 切换液体 |
| `set_flow` | `value`: 0.1–1600 | 设定流量 (mL/min) |
| `set_volume` | `value`: 0.1–99999 | 设定目标体积 (mL) |
| `set_time` | `value`: 1–86400 | 设定目标时间 (s) |
| `set_jet_vol` | `value`: 0.1–10 | 设定单次喷射量 (mL) |
| `set_jet_interval` | `value`: 1–60 | 设定喷射间隔 (s) |
| `set_jet_flow` | `value`: 10–1600 | 设定喷射流量 (mL/min) |
| `set_jet_pressure` | `value`: 1–10 | 设定喷射压力等级 |
| `jet_start` / `jet_stop` | — | 喷射模式启停（`start`/`stop` 在 JET 模式下等效） |
| `set_anti_drip` | `value`: 0–5 | 设定回吸量 (mL) |
| `set_tube_life` | `value`: 0–200000 | 设定管路寿命 (mL) |
| `calib_enter` → `calib_select_liquid` → `calib_set_vol` → `calib_start_run` → `calib_stop_run` → `calib_measure` → `calib_save` → `calib_settings_done` | 见说明 | 校准向导；`calib_abort` 可在任意步退出 |
| `prime_start` / `prime_stop` | — | 预灌快排 |
| `get_state` | — | 获取完整状态遥测（仅 CLI 使用） |
| `wifi_restart` | — | 重启 WiFi |
| `menu_main` | — | 返回主菜单 |

> `calib_select_liquid` 与 `get_state` 只有 `pump_cli.py` 在用，Web UI 不发；
> `menu_main` / `jet_start` / `jet_stop` / `reset` 前端和 CLI 都不发，仅作为协议兼容性保留。

### 遥测响应示例

```json
{
  "type": "telemetry",
  "ts": 123456789,
  "state": "RUNNING",
  "menu": "MAIN",
  "mode": "VOLUME",
  "liquid": "Wtr",
  "liquidIdx": 0,
  "flow": 150.0,
  "targetVol": 50.0,
  "calibTargetVol": 10.0,
  "targetTime": 30.0,
  "dispensed": 23.45,
  "elapsed": 9,
  "progress": 46,
  "totalDispensed": 12340.5,
  "tubePct": 24,
  "tubeLifeML": 50000,
  "jetCount": 0,
  "jetVolume": 1.0,
  "jetInterval": 3,
  "jetFlowRate": 200.0,
  "jetPressure": 5,
  "stepsPerMl": 250.0,
  "antiDripVol": 0.05,
  "stepperEnabled": true,
  "calibStep": 0,
  "wifiMode": "ap",
  "wifiIP": "192.168.4.1",
  "wifiClients": 1,
  "psramFree": 7936,
  "psramTotal": 8192,
  "heapFree": 265,
  "heapTotal": 320
}
```

> `state` 取值：`IDLE` / `RUNNING` / `PAUSED` / `DONE` / `ANTI_DRIP`（`STALL_ERROR` 已随堵转检测删除）。
> `menu` 取值：`MAIN` / `CALIBRATE` / `PRIME`。
> `heapTotal` 现在是 `ESP.getHeapSize()` 的实测值，不再是硬编码常量。

---

## 状态机

```
        start                         完成 (antiDripVol>0)          DONE_HOLD_MS
IDLE ─────────> RUNNING ──────────────────────────────> ANTI_DRIP ──────────> DONE ──────> IDLE
                  ⇅  pause / resume                        (antiDripVol=0 时直接 → DONE)
                PAUSED

Mode: MODE_VOLUME ⇄ MODE_TIME ⇄ MODE_JET
      JET 模式下 RUNNING 会一直循环喷射, 必须显式 stop / jet_stop 才退出
```

---

## 烧录配置 (Arduino IDE)

| 选项 | 值 |
|---|---|
| Board | **ESP32S3 Dev Module** |
| PSRAM | **OPI PSRAM** (Octal SPI, N16R8) |
| USB CDC On Boot | **Disabled** |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| Flash Size | **16MB (128Mb)** |
| CPU Frequency | **160 MHz** |
| 串口波特率 | **115200** |

### 内存布局

| 存储器 | 容量 | 用途 |
|--------|------|------|
| Flash | 16 MB | 固件约 1.00 MB（Huge APP 分区上限 3 MB，占用 33%） |
| 内部 SRAM | ~320 KB | WiFi 协议栈、FreeRTOS 任务栈、关键数据（静态占用实测 49.7 KB / 15%） |
| **PSRAM** | **8 MB** | 遥测/命令/串口缓冲区、大 JSON 解析 |

静态缓冲区（`telemetryBuf`/`responseBuf`/`serialBuffer`/`hwUartBuf`）已全部移到 PSRAM heap。

> 删除 BLE (NimBLE) 后：固件 1.20 MB → 1.00 MB（省 202 KB），内部 SRAM 静态占用 56.3 KB → 49.7 KB（省 6.7 KB）。
> **注意**：若误用默认分区方案（app 仅 1.25 MB），删除 BLE 前占用已达 95%，务必按上表选择 **Huge APP**。

---

## 关键引脚 (DM542)

```
STEP: 16    DIR: 17    ENA: 18 (未接, 方案D)
Buzzer: 5
UART1 RX: 21    TX: 47      (硬件串口, USB-TTL 直连 PC)
WS2812: 48                   (RGB 状态指示灯)
```

### DM542 接线 (共阴)
```
ESP32                  DM542
─────────────────────────────────
GPIO16 ─────────────── PUL+  (经 6N137 光耦)
ESP32 GND ──────────── PUL-
GPIO17 ─────────────── DIR+  (经 6N137 光耦)
ESP32 GND ──────────── DIR-
(ENA+/ENA- 不接, 方案D)

24V电源 + ──────────── VDC(+)
24V电源 GND ────────── GND (功率地)
```

### DM542 拨码开关: 0 1 1 0 0 0 1 1 (SW1-8)
| 功能 | 拨码 | 值 |
|------|------|-----|
| 电流 | SW1-3=OFF,ON,ON | 3.76A 峰值 |
| 待机 | SW4=OFF | 半流保持 |
| 细分 | SW5-8=OFF,OFF,ON,ON | **400 pulse/rev (8细分)** |

### 安全引脚 (已验证)

- ✅ STEP/DIR/ENA: GPIO 16/17/18
- ✅ Buzzer: GPIO 5
- ✅ 硬件 UART1: GPIO 21(RX) / 47(TX)
- ✅ WS2812: GPIO 48
- ⚠️ GPIO 21 曾同时被标为 I2C SDA（SH1106 OLED）—— OLED 已在 v2.0 停用，
  该引脚现在归 UART1 RX 独占，不要再复用
- ❌ 避开 Strapping: GPIO 0/3/45/46
- ❌ 避开 PSRAM 占用: GPIO 27/32/33/34/35/36/37
- ❌ 避开 JTAG: GPIO 14/15

---

## 项目结构

> ⚠️ **Arduino 只编译 sketch 目录内的文件**：所有参与编译的 `.ino` / `.cpp` / `.h`
> 必须全部位于 `peristaltic_pump/` 下（目录名须与 `.ino` 同名）。放在仓库根目录
> 会导致 `fatal error: xxx.h: No such file or directory`。

```
peristaltic_pump_DM542/           # 仓库根
├── peristaltic_pump/             # ← Arduino sketch 目录 (用 IDE 打开这个)
│   ├── peristaltic_pump.ino      # 主程序 (setup/loop 组装)
│   ├── pump_state.h/cpp          # PumpState 结构体 — 所有运行状态集中管理
│   ├── pump_machine.h/cpp        # 泵状态机 — transition() + per-state tick
│   ├── pump_shared.h             # 枚举 / 常量 / extern 声明
│   ├── pump_core.h/cpp           # 泵控制核心 (启停/暂停/恢复/喷射/校准/速度设置)
│   ├── eeprom_store.h/cpp        # EEPROM 持久化存储 (offset 0-63)
│   ├── wifi_manager.h/cpp        # WiFi 管理 (SoftAP/Station + EEPROM offset 184+)
│   ├── web_handlers.h/cpp        # HTTP 服务 (路由 / 内嵌页面)
│   ├── command_protocol.h/cpp    # JSON 命令协议 (解析/路由/遥测)
│   ├── serial_commands.h/cpp     # USB 串口 + 硬件 UART 命令入口
│   ├── buzzer.h/cpp              # 非阻塞蜂鸣器驱动
│   ├── led.h/cpp                 # WS2812 状态指示灯
│   ├── web_ui_gen.h              # 生成文件, 勿手改
│   ├── index.html                # Web UI 源文件 (编辑入口)
│   └── generate_web_ui.py        # index.html → web_ui_gen.h
├── pump_cli.py                   # Python CLI 控制工具 (tools/ 下有同一份副本)
├── xtoys_script.js               # XToys 集成脚本 (已放弃，仅供参考)
├── xtoys_js_only.js              # 同上, 纯 JS 逻辑
├── minimal_test/                 # 硬件最小验证 sketch
├── hardware/                     # PCB / 结构件
└── README.md
```

改完 `index.html` 后必须执行 `python peristaltic_pump/generate_web_ui.py` 重新生成
`web_ui_gen.h`，否则固件里的页面不会更新。

> ⚠️ **`index.html` 里的 JS 不要用 `//` 行注释。** 生成脚本用 `re.sub(r"\s+", " ", html)`
> 压缩空白，会把换行也变成空格 —— 一旦出现 `//`，它后面的所有代码都会被并进这行注释里，
> 生成的页面静默失效。需要注释请用 `/* ... */`。

---

## 依赖库

| 库 | 用途 |
|---|---|
| [FastAccelStepper](https://github.com/gin66/FastAccelStepper) | 步进电机驱动 (ESP32 后端为 MCPWM + PCNT, 匀加速) |
| [ArduinoJson](https://arduinojson.org/) (v7) | JSON 解析/序列化 |
| [Adafruit_NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) | WS2812 状态灯 (RMT) |
| [U8g2](https://github.com/olikraus/u8g2) | OLED 图形库 *(v2.0 已停用)* |
| [Keypad](https://github.com/Chris--A/Keypad) | 矩阵键盘 *(v2.0 已停用)* |

---

## 已知问题 (2026-09-26 代码审查)

一次性完整审查的记录。状态：✅ 已修 · ⚠️ 需产品决策 · 📝 文档已订正

### A. 会导致错误行为的缺陷

| # | 问题 | 根因 / 证据 | 状态 |
|---|---|---|---|
| A1 | 工程完全无法编译 | `.ino` 已移入 `peristaltic_pump/`，24 个 `.cpp/.h` 仍留在仓库根 → `fatal error: command_protocol.h: No such file or directory` | ✅ |
| A2 | 体积/时间模式一启动就误报堵转 | `stallCheckTime` 初值 0 且从不重置，`millis()-0 > 1500` 恒真；`getCurrentPosition()` 返回 PCNT 实发脉冲数，`moveTo()` 后首帧仍为 0 | ✅ 删除整个堵转检测 |
| A3 | **低流量时电机不动，却上报「已分液 X mL」** | `setSpeedInHz(0)` / `setAcceleration(<=0)` 返回 -1 且保持原值 → `moveTo()` 得 `MOVE_ERR_SPEED_IS_UNDEFINED` → `isRunning()` 立刻为假 → 状态机判定完成。触发条件 `pps<1`，即 250 spm 下 flowRate<0.24 mL/min（UI 与固件下限都是 0.1） | ✅ 改用 `setSpeedInMilliHz()` + 检查返回值 |
| A4 | `stepperConnectToPin()` 失败 → 启动即崩溃重启 | `.ino` 的 `if (stepper)` 只护住 3 行，紧随其后的 `updateStepperSpeed()` 无条件解引用 | ✅ setup 里直接 fatal halt |
| A5 | 暂停后恢复会多打约 0.13 mL | `pausePump()` 在 `forceStop()` 后立刻读位置，但库文档说 forceStop「约 20ms 内才停」，队列里已规划的命令仍会执行完 | ✅ 加 `blockingWaitForForceStopComplete()` |
| A6 | 校准可把 `stepsPerMl` 存成 0 | `calibCalculate()` 在 `calibStepsRun<=0` 时静默不动作，`calibNewSPM` 保持 0；`calib_measure` 仍返回 `ok:true`；`calib_save` 不校验 → 落盘。之后 `pos/0` = inf/NaN | ✅ 校验后才允许进入 RESULT |
| A7 | 管路寿命累计在首次改参数前不持久化 | `.ino` 的 `if (!loadParams()) saveParams();` —— 但 `saveParams()` 首行就是 `if (!eepromDirty) return;`，首启该标志为 false → magic 永不落地 | ✅ 补 `markDirty()` |
| A8 | EEPROM 半写导致的 NaN 穿透校验 | flash 擦除态 `0xFFFFFFFF` 即 NaN；`constrain(NaN,10,50000)` 两个比较都为假 → **原样返回 NaN** → `moveTo((int32_t)NaN)` 是 UB | ✅ 加 `isfinite()` 兜底 |
| A9 | 保存 WiFi 后 `pump.local` 失效 | `restartWiFi()` 重入 `initWiFi()`，第二次 `MDNS.begin("pump")` 因 "Service already exists" 返回 false，`addService()` 不执行 | ✅ 补 `MDNS.end()` |
| A10 | 点「保存 WiFi」无任何反应 | `restartWiFi()` 在 `sendJson()` 之前就 `softAPdisconnect(true)` 拆掉承载响应的连接；响应无 `Content-Length`，浏览器靠 FIN 判断结束 → `fetch().then()` 永不执行，且前端无 `.catch()`。成功后还会再发一次 `wifi_restart`（重启两遍） | ✅ 调整顺序 + flush/stop + 前端去重并补 catch |
| A11 | Web UI 页面偶发截断 | 所有响应只发 `Connection: close` 不发 `Content-Length`，且 `handleRequest()` 后立刻 `client.stop()` **没有 `flush()`** —— 24.5 KB 的 `WEB_UI` 有风险 | ✅ |
| A12 | 状态码理由短语错误 | `sendJson()` 对所有状态码都拼 `" OK"` → 实际发出 `HTTP/1.1 404 OK` | ✅ |
| A13 | LF-only 客户端的 POST body 被吃掉 2 字节 | `headersDone` 同时接受 `\r\n\r\n` 和 `\n\n`，但之后 `request.length()-headerEnd-4` 与 `substring(headerEnd+4,…)` 硬编码 4 字节分隔符 | ✅ |
| A14 | 长命令帧被截断 | 注释声称单帧上限 512B、`SERIAL_BUF_SIZE=1024`，但 `hwUart.begin()` 用默认 **256B** RX 环形缓冲；115200 下仅约 22ms 余量，而 `parseAndExecute()` 可能更久 | ✅ `setRxBufferSize(1024)` |
| A15 | 单段音效完全无声 / 多段音效最后一段被切 | `buzzer_tick()` 播完最后一段后，`g_seqPos >= g_seqLen` 在**同一次调用内**成立，紧接着 `noTone()`；而 core 3.3.10 的 `tone()` 是异步的（投消息给 `toneTask`），`noTone()` 会先 `xQueueReset()` 把刚投进去的 `TONE_START` 丢掉 | ✅ |
| A16 | 「三连音报警」最多响一次 | `on_entry(STALL_ERROR)` 连调三次 `beepCancel()`，而 `startSeq()` 每次都重置 `g_seq/g_seqPos/g_next` | ✅ 随堵转检测一并删除 |
| A17 | 遥测 `heapTotal` 是假数据 | `buildTelemetryJson()` 里硬编码 `327680`，不是实测值 | ✅ 改 `ESP.getHeapSize()` |
| A18 | 时间模式跑完后流量显示变成用户没设过的值 | `startPump()` 用 `targetVolume/targetTime` 反算并**覆写 `pump.flowRate`**，跑完不还原、也不 `markDirty()` → 重启后又变回去 | ✅ 拆出 `activeFlowRate` |
| A19 | 回吸中触发 WiFi 扫描会冻结状态机 8 秒 | `/api/scan` 只挡了 `RUNNING`/`PAUSED`，没挡 `ANTI_DRIP`（此时电机正在反转） | ✅ |
| A20 | DONE 的绿灯渐暗永远走不完 | `led.cpp` 按 150 帧（≈3s）算 fade，但 `tick_done()` 只保持 DONE **2s** | ✅ |
| A21 | `setRGBDim()` 的 `dim` 超出声明的 0.0–1.0 | PAUSED 可达 1.1、ANTI_DRIP 可达 1.3；`(uint8_t)(r*dim)` 当前色值下不溢出，但契约已破 | ✅ 钳位 |
| A22 | `String += c` 逐字节拼请求造成堆碎片 | Arduino `String::concat` 每次精确重分配，一个 500B 请求 = 500 次 malloc/free，长期运行加剧碎片 | ✅ 改缓冲读 |
| A23 | `getQueryParam()` 未锚定到 `?` / `&` | 直接 `url.indexOf("c=")`，`/api/cmd?xc=start` 也会命中；且不做 URL 解码 | ✅ 锚定 |

### B. 需产品决策，未擅自改动

| # | 问题 | 说明 |
|---|---|---|
| B1 | **整个 HTTP API 零鉴权** | 同网段任何人都能 start/stop/改校准；`POST /api/wifi` 能改网络配置。SoftAP 密码是全设备硬编码的 `12345678`（已收敛为 `WIFI_AP_PASSWORD` 宏，但值未变）。加鉴权会改变使用方式（且 AP 模式下手机要先连 WiFi），**需要你决定要不要加、加什么形式** |
| B2 | `/api/cmd` 拼 JSON 不转义不校验 | `cmd`/`v`/`s`/`m`/`i` 直接字符串拼接。实际危害有限（`parseAndExecute` 只 `strcmp` 已知命令，畸形 JSON 会解析失败），但属于未净化输入进 JSON 构造器。与 B1 一起决定 |
| B3 | `stepperEnabled` 现在恒为 `true` | 删掉堵转检测后没有任何地方会置 false（ENA 本来也「未接，方案D」）。仍被遥测和 `index.html` 的「使能/断电」显示消费，删除会改遥测 schema |
| B4 | EEPROM 写放大 | VOLUME 模式每 10 次分液 commit 一次全量 64B；JET `interval=1s` 时约每 10s 一次 NVS 写。24/7 运行的磨损需要评估 |
| B5 | `pump_cli.py` 与 `tools/pump_cli.py` 完全重复 | hash 一致。留哪份由你定 |
| B6 | `menu_main` / `jet_start` / `jet_stop` / `reset` 前端和 CLI 都不发 | 但都是 README 记载的协议命令，外部工具可能在用，故保留 |
| B7 | AP 密码 / STA 密码明文存 NVS（XOR 只是混淆） | 密钥是 eFuse MAC，而 BLE 名与 AP SSID 曾把其中 2 字节广播出去（BLE 已删）。注释里「拆机读 EEPROM 也是乱码」的说法言过其实 |

### C. 文档订正

| # | 原文 | 实际 | 状态 |
|---|---|---|---|
| C1 | 流量 `0.1–2000`、喷射 `10–2000` | 代码与 UI 全是 **1600** | 📝 |
| C2 | 堵转「3 秒位置不变」 | `STALL_TIMEOUT_MS` 是 1500（功能已整体删除） | 📝 |
| C3 | 功能 10「方案预设 4 槽位」+ `preset_load`/`preset_save` | v2.3.8 (2a34518) 已删除，代码里 grep 不到 `preset` | 📝 |
| C4 | 功能 12「自动关使能，待机/暂停 5s 断电」 | `IDLE_DISABLE_MS` 从未使用、`beepDisable()` 从未调用、ENA 拉 HIGH 后再没动过；引脚表自己也写「ENA: 18 (未接, 方案D)」 | 📝 |
| C5 | 功能 3「喷射间隔 >15s 自动断电，提前 2s 通电节能」 | JET 分支里没有这段逻辑 | 📝 |
| C6 | 功能 5「ANTI_DRIP 不可打断」 | `stop`/`reset` 无条件调 `resetPump()`，可以打断 | 📝 |
| C7 | 功能 7「匀加速，缓启动/**缓停止**」 | `stopPump()` 用 `forceStop()`（库文档：abruptly stop **without deceleration**），不是 `stopMove()` | 📝 |
| C8 | 功能 17「FreeRTOS 命令队列…线程安全」 | 队列是死代码（已删除） | 📝 |
| C9 | FastAccelStepper「**RMT** 硬件加速」 | 实测编入的后端是 `StepperISR_idf5_esp32_mcpwm_pcnt.cpp`，即 **MCPWM + PCNT** | 📝 |
| C10 | `command_protocol.h`「**WebSocket** 和 USB Serial 共用」 | 项目里没有 WebSocket | 📝 |
| C11 | 引脚表「I2C: GPIO **21**(SDA)/7(SCL)」 | 与 `HW_UART_RX 21` 抢同一个 GPIO21（OLED 已停用） | 📝 |
| C12 | 内存布局「固件 (1.2MB) + SPIFFS」 | 与烧录配置表的 Huge APP (3MB **No** OTA/1MB SPIFFS) 矛盾；代码里无 LittleFS/SPIFFS | 📝 |
| C13 | 遥测示例 `"liquid":"Water"` | `LIQUID_NAMES[0]` 是 `"Wtr"` | 📝 |
| C14 | `/api/scan` 「同步, ~1s」 | 两次 `scanNetworks(…,300)`，实测约 8s（WiFi 章节写的是 ~8s，自相矛盾） | 📝 |
| C15 | `LED_PIN 48` 只写在 `led.cpp` 里 | 未进 `pump_shared.h` 的引脚表 | 📝 |

### D. 源码编码损坏

`command_protocol.cpp` 与 `web_handlers.cpp` 的中文注释曾是 **有损 mojibake**：UTF-8 字节被按 GBK 解码后又存成 UTF-8，且 GBK 无法解码的尾字节已被替换成字面 `?`（例：`—` → `鈥?`），**无法靠转码还原**，只能按代码语义手工重写。

**已全部处理完毕**：`command_protocol.cpp` 25 处注释、`web_handlers.cpp` 整个文件均已重写为正确 UTF-8（保留 BOM 与 CRLF）。`/manifest.json` 的 `"name"` 字面量原本是乱码 `锠曞姩娉垫帶鍒跺櫒`（会直接显示成 PWA 安装名），已还原为 `蠕动泵控制器`。

> 校验方法：把两个文件里所有含非 ASCII 的行导出后逐行核对，已无一处乱码。
> 注意「把文本 encode('gbk') 再 decode('utf-8')」这种自动检测法**会误报**
> （例如正常的 `状态` 就能被"还原"成垃圾），别拿它当唯一判据。

---

## 更新日志

### 未发布 (2026-09-26) — 修复构建 / 低速分液 / STA-AP，删除 BLE、堵转检测与全部死代码

**构建修复:**
- 全部 `.cpp` / `.h` 移入 `peristaltic_pump/` sketch 目录（此前 `.ino` 已移入但源文件仍留在仓库根，导致 `fatal error: command_protocol.h: No such file or directory`，工程完全无法编译）
- `index.html` 与 `generate_web_ui.py` 一并移入，保持生成脚本的相对路径可用

**删除 BLE:**
- 移除 `bluetooth_manager.h/cpp`、`initBluetooth()`、`handleBluetooth()`、NimBLE 依赖
- 顺带消除了 `rxBuffer` 的跨任务数据竞争（`rxMutex` 创建后从未使用，`onWrite()` 在 NimBLE 任务里写、`handleBluetooth()` 在 loop 任务里读）
- 固件 1.20 MB → 1.00 MB（省 202 KB），内部 SRAM 静态占用 56.3 KB → 49.7 KB（省 6.7 KB）

**删除堵转检测:**
- 移除 `STALL_ERROR` 状态、`STALL_TIMEOUT_MS`、`PumpState::stallLastPosition` / `stallCheckTime`、`tick_stall_error()`
- 移除 Web UI 的 `isStall` 分支与「⚠ 堵转」按钮态，并重新生成 `web_ui_gen.h`
- 原因：`stallCheckTime` 从未在启动/恢复时初始化（全项目仅 4 处引用，初值恒为 0），
  而 `millis()` 进 loop 时已 ≥ 2400 ms，导致 `millis() - 0 > 1500` 恒真；
  同时 `getCurrentPosition()` 返回的是 PCNT 已发出的脉冲数，`moveTo()` 后第一个脉冲尚未产生，
  于是 `curPos == stallLastPosition == 0` —— 体积/时间模式一启动就误判堵转。
  （JET / 校准 / 快排在 `tick_running()` 里提前 return，绕过了检测，所以一直没暴露）

**低速分液修复:**
- 新增 `pump_core.cpp::applySpeed(pps, accel)` 作为唯一的速度设置入口，替换全部 4 处
  `setSpeedInHz()` + `setAcceleration()` 裸调用
- `FastAccelStepper::setSpeedInHz(0)` 与 `setAcceleration(<=0)` 都会 **返回 -1 且保持原值不变**，
  之后 `moveTo()` 因 `checkValidConfig()` 失败返回 `MOVE_ERR_SPEED_IS_UNDEFINED`，一个脉冲都不发；
  而状态机看到 `isRunning()==false` 就判定完成，**上报「已分液 X mL」但实际什么都没打**
- 触发条件：`pps = flowRate × stepsPerMl / 60 < 1`，即 stepsPerMl=250 时 flowRate < 0.24 mL/min
  （UI 与固件的下限都是 0.1，完全可输入）；时间模式下 `0.1 mL / 60 s` 同样触发
- 修法：改用 `setSpeedInMilliHz()`（有效下限 5 millHz，覆盖 0.0167 Hz ~ 1.33 MHz 全域且不溢出），
  加速度下限钳到 1，并检查两个调用的返回值，失败时打印 `[PUMP] applySpeed rejected`
- 顺带修掉 `updateStepperSpeed()` 在 `stepperConnectToPin()` 失败时对空指针的解引用（启动即崩溃重启）

**STA / AP 缺陷修复:**
- `initWiFi()` 里补 `MDNS.end()`。`restartWiFi()` 会重入 `initWiFi()`，第二次 `MDNS.begin("pump")`
  因 "Service already exists" 返回 false，`addService()` 不执行 → **保存 WiFi 配置后 `pump.local` 失效**，
  而 README 与 Web UI header 都在承诺这个地址
- `POST /api/wifi` 改为 **先 `sendJson()` + `client.flush()` + `client.stop()` + `delay(300)`，再 `restartWiFi()`**。
  原顺序在发响应之前就 `softAPdisconnect(true)` 拆掉了承载响应的 TCP 连接；
  又因为响应只有 `Connection: close` 没有 `Content-Length`，浏览器要靠 FIN 判断 body 结束，
  连接被硬拆 → 前端 `fetch().then()` 永不执行（且原来没有 `.catch()`，静默失败）
- 前端 `wifiSave()` 去掉多余的 `sendCmd('wifi_restart')`（固件已在 POST 处理里重启过，这是第二次），
  并补上 `.catch()` 让失败可见
- AP 密码 `"12345678"` 原本硬编码在两处（`wifi_manager.cpp` 与 `web_handlers.cpp` 的扫描恢复路径），
  收敛为 `wifi_manager.h` 的 `WIFI_AP_PASSWORD` 宏

**死代码清理:**
- **FreeRTOS 命令队列整套删除**（约 40 行）：`initCommandQueue` / `processCommandQueue` /
  `enqueueCommand` / `enqueueCommandClient` / `setCommandResponseCallback` / `CommandMsg` /
  `CommandResponseCallback` / `CMD_QUEUE_SIZE` / `CMD_JSON_MAX` —— 零调用点，
  所有通道本来就直接调 `parseAndExecute()`
- **`wifiMaintain()` 删除**：它只把 `staConnecting` 置 false，而 `staConnecting` 只被它自己读，
  对 WiFi 状态、遥测、日志都没有任何影响 —— 所谓「30s 超时自动放弃」是个空转。
  连带删除 `staConnecting` / `staConnectStart`。断线重连本来就由 esp_wifi 负责
- **`/api/info` 端点删除**：`index.html` / `pump_cli.py` / xtoys 脚本都没有调用它，
  它返回的 mode/ip/clients 已由 `/api/status` 遥测覆盖
- `getApSSID()`、`wifiReady` 删除（零调用点 / 写后从不读）
- `lastStepperActivity` 删除：5 处写入、**0 处读取**，只服务于从未实现的「自动关使能」；
  连带删除 `IDLE_DISABLE_MS`，功能清单第 12 项一并移除
- `prevState` 删除：只在 `tick_done()` 里被自己那个伪 entry-action 读写。
  改为把 `done_entry_ms = millis()` 放进真正的 `on_entry(DONE)`，行为等价且语义正确
- 数字输入缓冲 `inputClear` / `inputBackspace` / `inputAppend` / `inputToFloat` + `inputBuf` / `inputLen`
  删除（4×4 键盘已停用，`inputBuf` 永远是空的）
- `beepInput()` / `beepDisable()` 删除（零调用点）；音效从 7 种降为 5 种
- `led_update()` 空函数删除；`led.cpp` 的 `pulse`、`g_lastWifiCli`、`g_lastEnabled` 删除
  （`g_lastWifiCli` / `g_lastBleConn` 对应的「WiFi 客户端白光」「BLE 青光」叠加从未实现）
- `Menu` 枚举从 12 个值收缩到 3 个（`MAIN` / `CALIBRATE` / `PRIME`）——
  其余 9 个是 OLED 菜单遗留，`currentMenu` 从未被赋过这些值，遥测 `menu` 字段也不可能报告它们
- `build_web_ui.py` 删除：内嵌了另一份 divergent HTML，且输出路径硬编码为
  `C:\Users\xg821\peristaltic_pump\web_ui_gen.h`（另一个旧项目副本），跑它会改坏别的项目
- `data/www/index.html` 删除：代码里没有 LittleFS/SPIFFS，固件只用内嵌的 `WEB_UI` 字符串

**缺陷修复（审查清单 A4–A23，编号见上文「已知问题」）:**
- **A4** `.ino`：`stepperConnectToPin()` 失败时不再继续跑（原来紧随其后的 `updateStepperSpeed()`
  会解引用空指针 → 崩溃重启循环），改为打印 FATAL 并停在 `for(;;)`
- **A5** `pausePump()`：`forceStop()` 后轮询 `isRunning()` 到 false 再读位置。
  库文档说 forceStop「约 20ms 内停下」，队列里已规划的命令仍会走完；
  `isRunning()` 的定义含 `!isQueueEmpty()`，所以轮询它就是这个等待的公开做法
  （`blockingWaitForForceStopComplete()` 是 private，用不了）。上限 200ms 防止卡死
- **A6** `calibCalculate()` 改为返回 `bool`；`calib_measure` 算不出有效值时报错并停在 MEASURE 步，
  `calib_save` 增加 `calibNewSPM >= 10` 校验，`calibSave()` 内再兜一层 —— 彻底堵死 `stepsPerMl=0` 落盘
- **A7** `.ino`：`if (!loadParams()) { markDirty(); saveParams(); }`，首次上电真正把 magic 写进去
- **A8** `eeprom_store.cpp`：新增 `clampF()`，先过 `isfinite()` 再 `constrain()`。
  `constrain(NaN,…)` 因为两个比较都为假会**原样返回 NaN**，而 flash 擦除态 `0xFFFFFFFF` 就是 NaN
- **A11/A12** `web_handlers.cpp`：所有响应改为带 `Content-Length`，并按状态码给出正确理由短语
  （不再发 `HTTP/1.1 404 OK`）；`handleWebClients()` 末尾补 `client.flush()` 再 `stop()`
- **A13** 新增 `findHeaderEnd()`，正确区分 `\r\n\r\n`(4B) 与 `\n\n`(2B)
- **A14** `initHardwareUart()`：`begin()` 之前 `setRxBufferSize(SERIAL_BUF_SIZE)`（默认只有 256B）
- **A15** `buzzer_tick()`：播完最后一段后 `return`，把收尾的 `noTone()` 推迟到**下一个** tick。
  原来两者在同一个 tick 内，而 core 3.x 的 `noTone()` 会先 `xQueueReset(_tone_queue)`，
  把刚投进去的 `TONE_START` 直接丢掉 → 单段音效完全无声、多段音效最后一段被切
- **A17** 遥测 `heapTotal` 改用 `ESP.getHeapSize()` 实测值（原硬编码 327680）
- **A18** 新增 `PumpState::activeFlowRate` 与 `applyFlowSpeed()`。TIME 模式的反算流量写进
  `activeFlowRate`，**不再覆写** `pump.flowRate`；`resumePump()` 与回吸都改用 `activeFlowRate`
  （顺带修掉一个隐性问题：原来恢复时的速度正确性依赖于那次覆写）
- **A19** `/api/scan` 的忙碌判断补上 `ANTI_DRIP`
- **A20/A21** `led.cpp`：新增共享常量 `DONE_HOLD_MS`（`pump_machine` 与 `led` 同源，
  避免再次漂移）；DONE 渐暗按 `g_phase*20 / DONE_HOLD_MS` 计算；相位重置移到算颜色**之前**；
  PAUSED/ANTI_DRIP 的 `dim` 表达式收敛到 [0.30, 1.00]，`setRGBDim()` 再加一道钳位；
  `sin()` → `sinf()`；`LED_PIN` 移入 `pump_shared.h`
- **A22** `handleWebClients()` 改用 2KB 固定缓冲 + `client.read(buf, n)` 成块读，
  不再逐字节 `String +=`（原写法一个 500B 请求要 500 次 malloc/free）
- **A23** `getQueryParam()` 只认 `?` 之后、以 `&` 分隔的 `key=` 片段

**其它:**
- `.ino` 4 处 `Serial.printf` 的 `%d` 改 `%lu` + 显式转型，本项目代码**零编译警告**
- `command_protocol.cpp`（25 处）与 `web_handlers.cpp`（整文件）的 mojibake 注释全部重写为正确 UTF-8
- README 的 C1–C15 文档偏差全部订正
- 最终：固件 1046196 字节（33%），内部 SRAM 静态占用 52840 字节（16%，
  比上一轮 +2KB 来自 `handleWebClients()` 的请求缓冲）

---

### v2.3.6 (2026-07-21) — UART 串口通信修复

**固件修复:**
- `RESPONSE_BUF_SIZE` 512 → 1536：`get_state` 响应 JSON 超过 512 字节被 `snprintf()` 截断，导致 UART 端遥测不完整
- `SERIAL_BUF_SIZE` 512 → 1024：同步扩大串口接收缓冲区

**CLI 修复 (`pump_cli.py`):**
- 打开串口后设置 `DTR=False` + `RTS=False`，防止 CH340 复位 ESP32
- 启动等待 0.5s → 5s，匹配 ESP32-S3 完整启动时间
- `readline()` 超时 0.1s → 0.5s，确保 600+ 字节的长响应不会在行中被截断
- Emoji 改为纯文本，修复 Windows GBK 编码错误

### v2.3.5 (2026-07-19) — 软件打磨完成

**WiFi 扫描重写:**
- 异步 → 同步扫描，绕过 ESP32 Arduino `_scanStatus` 状态机 bug
- 扫描前断开正在连接的 STA，释放射频资源
- 扫描结果缓存 15 秒，防止前端 300ms 轮询堆积
- 自动过滤自身 SoftAP SSID，不在列表中显示

**WiFi 双 AP 修复:**
- `WiFi.persistent(false)` 禁用 NVS 自动加载旧 AP 配置
- `WiFi.softAPdisconnect(true)` 确保创建 AP 前清理残留

**Web UI:**
- 高级设置独立面板：回吸量 / 管路寿命，不再耦合校准流程
- 校准步骤 6→5（移除"高级设置"步骤）
- 遥测新增 `tubeLifeML` 字段

**校准修复:**
- `calibFinishRun()` 自动进入 `CALIB_MEASURE`（运行完成不再卡步骤）
- 前端 `sendCmd` 接收 `calib_measure` 响应并更新 SPM 结果显示

**电机丢步:** ✅ 已解决

### v2.3.4 (2026-07-18)

**WiFi:**
- WiFi TX 功率恢复 20dBm 全功率（热管理问题已解决，无需再限制 8dBm）

### v2.3.2 (2026-07-12)

**架构重构:**
- 引入 `PumpState` 结构体 — 所有运行参数/状态/校准数据集中管理于 `pump_state.h`
- 提取 `pump_machine` 状态机模块 — `transition()` 统一状态切换 + `on_entry()` 入口回调
- `loop()` 从 ~150 行状态处理缩减为 `pump_machine_tick()` 一行调用
- 每个状态独立 `tick_*()` 函数: `tick_running()`, `tick_anti_drip()`, `tick_done()`, `tick_stall_error()`

**优化:**
- 堵转检测从 inline loop 迁移到 `tick_running()` 内部
- DONE → IDLE 自动转换 (2s) 纳入 `tick_done()` 管理
- ANTI_DRIP 状态处理从 inline loop 迁移到 `tick_anti_drip()`
- LINK 依赖从 AccelStepper 更新为 FastAccelStepper 文档

**清理:**
- 移除 OLED 字库相关文件 (`pump_chinese_font.h`, `strip_chinese.py`, `strip_kb_oled.py` 等)

### v2.2 (2026-07-08)

**堵转检测:**
- 泵运行时 `stepper.currentPosition()` 3 秒不变 → 判定堵转
- 堵转响应: 电机立即停转 + 断电，蜂鸣器三连音报警，WS2812 红色快闪
- 新增 `STALL_ERROR` 状态，只接受 `stop`/`reset` 命令
- JET 等待间隔期自动跳过检测

**WiFi 密码加密:**
- XOR 加密存储，密钥 = ESP32 出厂熔丝 Base MAC（每台设备唯一）
- EEPROM 写入前加密，读取后解密
- 完整性校验：解密后非可打印 ASCII → 清空配置回退 SoftAP
- SSID 明文存储（不敏感）

**PSRAM 全面启用:**
- ESP32-S3-N16R8 8MB Octal PSRAM 启用 (OPI PSRAM)
- 静态缓冲区全移到 PSRAM: 遥测/响应/USB串口/HW UART (~2.2KB 内部 SRAM 节省)
- FreeRTOS 命令队列 >4KB 自动分配 PSRAM
- Web UI 底部显示 PSRAM 和堆内存实时用量

**WiFi STA 双模:**
- `initWiFi()` 重写: WIFI_AP_STA 双模直接启动，避免模式切换 LWIP 冲突
- 后台连接家里 WiFi (30s 超时)，SoftAP 始终保留作 fallback
- 新增 `POST /api/wifi` (保存 WiFi 配置到 EEPROM)
- 新增 `GET /api/scan` (同步扫描, ~1s, 泵运行时拒绝)
- 新增 `GET /api/info` (网络状态)
- STA 连接成功自动弹出 toast 提示
- 密码输入框 👁 明文切换
- Header 显示可访问地址 `http://IP | http://pump.local`
- 修复 mDNS 重复注册 'Service already exists'
- 修复 `/api/status` 返回坏 JSON 导致页面一直"未连接"

**热管理:**
- CPU 从 240MHz 降至 160MHz

**修复:**
- DONE 状态可重新启动 (不再卡 'Pump not idle')
- `set_mode` 增加 `resetPump()` 确保模式切换后状态干净
- Web UI 液体选择行从遥测同步更新 (之前始终高亮液体 0)
- 喷射模式 UI: 按钮放入 flex 容器、合并为"应用全部"、运行状态显示

---

## 项目状态

📦 **本分支 (master) 已归档** — DM542 驱动 v2.3.8，不再新增功能。

> 2026-09-26 例外做了一次**审查与维护**（非功能开发）：修复了「工程无法编译」这个阻塞级问题，
> 删除 BLE / 堵转检测 / 全部死代码，并修掉 16 处缺陷。详见上文
> [已知问题 (2026-09-26 代码审查)](#已知问题-2026-09-26-代码审查) 与更新日志。

| 版本 | 分支 | 驱动 | 状态 |
|------|------|------|------|
| **v2.4.1** | **`tmc2226`** | TMC2226 + CoolStep + StealthChop | ✅ 活跃开发 (2026-08-15) |
| v2.3.8 | `master` | DM542 + 6N137 光耦 | 📦 归档 (本分支, 2026-07-30；2026-09-26 维护一次) |

后续功能更新全部在 **`tmc2226` 分支**进行（含：移除堵转检测与 BLE、修复 AP 热点名 / WiFi 扫描断连 / 暂停恢复过冲等），详见
[tmc2226 分支 README](https://github.com/784318839-stack/peristaltic-pump/blob/tmc2226/README.md)。

> ⚠️ **待回移到 `tmc2226` 的修复**（2026-09-26 审查在 master 上修好、但经 `git grep` 确认
> `tmc2226` 分支仍缺失的四项）：
>
> | 编号 | 问题 | tmc2226 现状 |
> |---|---|---|
> | **A3** | 低速分液时电机不动却上报「已分液 X mL」 | `pump_core.cpp:15-16`、`pump_core.cpp:90-91`、`pump_machine.cpp:84-85`、`command_protocol.cpp:366-367` 四处仍是裸 `setSpeedInHz((uint32_t)pps)` / `setAcceleration((int)…)`，无返回值检查、无 millHz 兜底；`setSpeedInMilliHz` / `applySpeed` 零命中 |
> | **A8** | `constrain()` 挡不住 NaN（flash 擦除态 `0xFFFFFFFF`） | `isfinite` / `clampF` 零命中（其 `eeprom_store.cpp` 的校验方式需另行确认） |
> | **A14** | 硬件 UART RX 环形缓冲默认 256B < 单帧 1024B | `setRxBufferSize` 零命中 |
> | **A18** | TIME 模式覆写 `flowRate`，跑完后 UI 显示用户没设过的值 | `activeFlowRate` 零命中 |
>
> A3 是其中最严重的一个：0.1 mL/min（UI 与固件的合法下限）在 250 stepsPerMl 下
> `pps = 0.4167 < 1` → `setSpeedInHz(0)` 返回 −1 且不改速度 → `moveTo()` 得
> `MOVE_ERR_SPEED_IS_UNDEFINED`，一个脉冲都不发，而状态机看到 `isRunning()==false`
> 就判定完成并累加 `totalDispensed`。

以下为 DM542 版本 (v2.3.8) 的已知问题记录：

| 问题 | 状态 |
|------|------|
| UART `get_state` 响应截断 (512B 缓冲区不足) | ✅ `RESPONSE_BUF_SIZE` → 1536 |
| CLI DTR 复位导致无响应 | ✅ `DTR=False` + 5s 等待 |
| CLI GBK 编码报错 | ✅ Emoji → 纯文本 |
| WiFi 扫描扫不到网络 | ✅ 异步改同步扫描, 绕过 ESP32 Arduino bug |
| 电脑扫描看到两个 AP | ✅ `WiFi.persistent(false)` 防 NVS 自动加载旧配置 |
| 校准运行完成卡步骤 | ✅ `calibFinishRun()` 补充步进 |
| 校准结果显示「计算中」 | ✅ 前端 `sendCmd` 接收 SPM 更新 UI |
| 高级设置耦合校准流程 | ✅ 独立面板 |
| 步进电机高速丢步 | ✅ 已解决 |

---

## License

本项目 **禁止商用**。仅限个人学习、研究、非商业用途使用。未经作者许可，不得用于商业目的。

This project is **non-commercial**. Personal use, study, and research only. Commercial use requires explicit permission from the author.
