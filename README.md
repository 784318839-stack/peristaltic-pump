# 📦 蠕动泵控制器 — YZ1515 精密点液 / 喷射工作站（DM542 版）

> ## 本分支已归档
>
> `master` = **DM542 驱动版本，v2.3.8，不再新增功能。**
> 活跃开发在 **[`tmc2226`](https://github.com/784318839-stack/peristaltic-pump/tree/tmc2226)** 分支
> （TMC2226 + CoolStep + StealthChop，v2.5.2）。
>
> 2026-09-26 对本分支做过一次**维护性审查**（修构建阻塞 + 删死代码 + 修 16 处缺陷），
> 不是功能开发。详见 [§6 代码审查记录](#6-代码审查记录-2026-09-26) 与 [§7 更新日志](#7-更新日志)。

基于 ESP32-S3 的蠕动泵控制器，驱动 YZ1515 工业泵头，实现**体积 / 时间 / 喷射**三种模式的
精密流体控制。步进驱动为 **DM542 数字驱动器 + 6N137 光耦隔离**，控制端为内嵌 Web UI
（手机 PWA）+ USB 串口 + 硬件 UART 三通道 JSON 协议。

| 版本 | 分支 | 驱动 | 状态 |
|------|------|------|------|
| **v2.5.2** | **[`tmc2226`](https://github.com/784318839-stack/peristaltic-pump/tree/tmc2226)** | TMC2226 + CoolStep + StealthChop | ✅ 活跃开发 |
| v2.3.8 | `master`（本分支） | DM542 + 6N137 光耦 | 📦 归档（2026-07-30；2026-09-26 维护一次） |

> **为什么两边对"堵转检测"的处理相反？** DM542 是开环驱动，没有位置/堵转反馈能力，
> 本分支用「位置 N 秒不变」推断堵转本身就不可靠（见 [A2](#a-组已修复的缺陷)），故整体删除。
> `tmc2226` 分支用 TMC2226 的**硬件 StallGuard** 重新实现了堵转保护（`6cbcf8b` v2.5.0，
> `27ce8eb` 修通信失败误判）。两者不矛盾，是硬件能力不同。

---

## 目录

1. [待办：回移到 tmc2226 的修复](#1-待办回移到-tmc2226-的修复)
2. [硬件](#2-硬件)
3. [功能](#3-功能)
4. [控制接口](#4-控制接口)
5. [构建与烧录](#5-构建与烧录)
6. [代码审查记录 (2026-09-26)](#6-代码审查记录-2026-09-26)
7. [更新日志](#7-更新日志)

---

## 1. 待办：回移到 tmc2226 的修复

2026-09-26 的审查在本分支修好了 16 处缺陷，其中 **4 项经 `git grep` 确认 `tmc2226`
（截至 `b337b3b`）仍然缺失**。这是本 README 里唯一还有行动价值的部分。

| 编号 | 问题 | `tmc2226` 现状 |
|---|---|---|
| **A3** | 低速分液时电机不动，却上报「已分液 X mL」 | `pump_core.cpp:15-16`、`pump_core.cpp:90-91`、`pump_machine.cpp:84-85`、`command_protocol.cpp:366-367` 四处仍是裸 `setSpeedInHz((uint32_t)pps)` / `setAcceleration((int)…)`，无返回值检查、无 millHz 兜底。`setSpeedInMilliHz` / `applySpeed` 零命中 |
| **A8** | `constrain()` 挡不住 NaN | `isfinite` / `clampF` 零命中（该分支 `eeprom_store.cpp` 的校验方式需另行确认） |
| **A14** | 硬件 UART RX 环形缓冲默认 256 B < 单帧 1024 B | `setRxBufferSize` 零命中 |
| **A18** | TIME 模式覆写 `flowRate`，跑完后 UI 显示用户没设过的值 | `activeFlowRate` 零命中 |

### A3 的完整机理（最严重的一个）

FastAccelStepper 1.2.7，均已在其源码中核实：

```
setSpeedInHz(0)          -> return -1，速度保持原值      (FastAccelStepper.cpp:657-660)
setAcceleration(<= 0)    -> return -1，加速度保持原值    (fas_ramp/RampGenerator.cpp:19-22)
RampCalculator::init()   -> memset(this, 0, ...)，故 valid_speed / valid_acceleration = false
                                                          (fas_ramp/RampCalculator.h:63)
checkValidConfig()       -> MOVE_ERR_SPEED_IS_UNDEFINED  (fas_ramp/RampCalculator.h:136-144)
```

于是 `moveTo()` 直接失败，**一个脉冲都不发**；而状态机看到 `isRunning() == false`
就判定运行完成，执行 `dispensedVolume = targetVolume` 并把 `totalDispensed` 累加进管路寿命。
UI 显示「已分液 10.0 mL」，实际一滴未出。

触发条件 `pps = flowRate × stepsPerMl / 60 < 1`：

- 250 stepsPerMl 下即 `flowRate < 0.24 mL/min`
- 而 Web UI 的 `min`、固件 `set_flow` 的下限、`loadParams()` 的 `constrain` **全都是 0.1**，完全可输入
- TIME 模式更容易踩：`0.1 mL / 60 s` 反算即 0.1 mL/min

本分支的修法（见 `pump_core.cpp::applySpeed()`）：改用 `setSpeedInMilliHz()` 而非抬高流量下限，
这样 0.1 mL/min 的慢速分液仍然可用。有效区间已核对：库下限 5 millHz
（`1000 × TICKS_PER_S / 0xffffffff + 1`），本项目最低 `pps = 0.1 × 10 / 60 = 0.0167 Hz = 16.7 millHz`；
上限 `250 × TICKS_PER_S = 4e9` 不溢出 `uint32`，最高 `pps` 1.33 MHz → 1.33e9 millHz。
加速度下限钳到 1，两个调用的返回值都检查，失败时打印 `[PUMP] applySpeed rejected`。

### 已在 tmc2226 上做过的，不要重复劳动

| commit | 内容 | 对应本分支编号 |
|---|---|---|
| `d136764` | v2.4.1 移除堵转检测与 BLE，修 WiFi 热点名与扫描断连 | A2 / A19 |
| `16d4c47` | 暂停恢复过冲、预灌停止卡死、参数保护、WiFi 保存双重重启 | A5 / A10 |
| `8a0514e` | v2.5.1 SoftAP 不广播 / STA 永不重连 / mDNS（`MDNS.end` 已有） | A9 |
| `8e4bcb1` | 校准 / 时间模式 / HTTP 六项修复（`Content-Length` 已有） | A6 / A11 部分 |

---

## 2. 硬件

### 部件

| 部件 | 型号 / 规格 |
|---|---|
| 主控 | ESP32-S3-WROOM-1-**N16R8**（16 MB Flash + **8 MB Octal PSRAM**） |
| 泵头 | YZ1515 工业级（100 × 80 × 80 mm） |
| 电机 | 57 步进电机（1.5 Ω/相） |
| 驱动器 | **DM542** 数字式步进驱动器（共阴接法，400 pulse/rev） |
| 信号隔离 | **6N137** 高速光耦模块（10 MBd） |
| 蜂鸣器 | 无源蜂鸣器，GPIO5 |
| 状态灯 | WS2812 单颗，GPIO48 |

> SH1106 OLED（I2C）与 4×4 矩阵键盘在 v2.0 已停用并已从固件移除，
> 相关代码残留也在 2026-09-26 清理完毕（`Menu` 枚举、数字输入缓冲等）。

### 引脚

```
STEP  16      DIR   17      ENA   18 (未接, 方案D)
BUZZER 5      WS2812 48
UART1 RX 21   TX 47         (硬件串口, USB-TTL 直连 PC)
```

全部定义集中在 `pump_shared.h`。

**避让**（已验证）：

- ❌ Strapping：GPIO 0 / 3 / 45 / 46
- ❌ PSRAM 占用：GPIO 27 / 32 / 33 / 34 / 35 / 36 / 37
- ❌ JTAG：GPIO 14 / 15
- ⚠️ GPIO 21 早期文档里同时标为 I2C SDA（OLED）。OLED 已停用，该引脚现归
  UART1 RX 独占，**不要再复用**

### DM542 接线（共阴）

```
ESP32                  DM542
─────────────────────────────────
GPIO16 ─────────────── PUL+  (经 6N137 光耦)
ESP32 GND ──────────── PUL-
GPIO17 ─────────────── DIR+  (经 6N137 光耦)
ESP32 GND ──────────── DIR-
(ENA+ / ENA- 不接, 方案D)

24V 电源 +  ─────────── VDC(+)
24V 电源 GND ────────── GND (功率地)
```

### DM542 拨码开关：`0 1 1 0 0 0 1 1`（SW1-8）

| 功能 | 拨码 | 值 |
|------|------|-----|
| 电流 | SW1-3 = OFF, ON, ON | 3.76 A 峰值 |
| 待机 | SW4 = OFF | 半流保持 |
| 细分 | SW5-8 = OFF, OFF, ON, ON | **400 pulse/rev（8 细分）** |

---

## 3. 功能

### 泵送模式

| 模式 | 参数 | 行为 |
|---|---|---|
| **体积** `MODE_VOLUME` | 流量 0.1–1600 mL/min，目标体积 0.1–99999 mL | 恒速运行到目标步数自动停止 |
| **时间** `MODE_TIME` | 体积 + 时间 1–86400 s | 运行时按 体积/时间 反算实际流量，写入 `activeFlowRate`（**不覆盖**用户设定的 `flowRate`）；到时间或到步数任一先到即停（时间判定有 +1 s 容差） |
| **喷射** `MODE_JET` | 单次量 0.1–10 mL，间隔 1–60 s，流量 10–1600 mL/min，压力 1–10 级 | 循环喷射，无回吸。`RUNNING` 会一直循环，必须显式 `stop` / `jet_stop` 才退出 |

喷射模式的双参数语义：**流量**管远近，**压力**（映射为加速度倍率）管爆发力。

### 运行控制

- **暂停 / 恢复** — 暂停时先 `forceStop()`，再轮询 `isRunning()` 到 false 才记录断点。
  `isRunning()` 的定义含 `!isQueueEmpty()`，所以轮询它就是等待硬停完成的公开做法
  （`blockingWaitForForceStopComplete()` 是 private，用不了）。这样恢复不会多打
- **防滴回吸** — 完成后以 0.3× 速度反转吸回 `antiDripVol`（0–5 mL），进入 `ANTI_DRIP` 状态。
  注意 `stop` / `reset` **可以**打断它
- **预灌 / 快排** — 1500 mL/min 全速排空管路，`moveTo(999999999)` 由 `forceStop()` 结束
- **速度曲线** — FastAccelStepper 匀加速启动（`ACCEL_FACTOR = 0.3`）；停止走 `forceStop()`
  **硬停，无减速斜坡**

### 校准与液体

- **4 种液体独立校准** — `Wtr` / `Thk` / `Liq1` / `Liq2`，各自独立的 `stepsPerMl`
- **校准向导** — 选液体 → 设体积（默认 10 mL）→ 运行 → 读量筒实际体积 → 计算并保存。
  `calib_abort` 可在任意步退出
- 算出的 `stepsPerMl` 会被钳到 10–50000，且 `calib_measure` / `calib_save` /
  `calibSave()` 三层校验，杜绝把 0 或 NaN 写进 EEPROM

### 存储

- **EEPROM 掉电记忆** — 泵参数占 offset 0–63，WiFi 配置占 184–282（各自带 magic：
  `0x5061` / `0x5746`），`EEPROM.begin(512)`
- 加载时每个浮点都过 `clampF()`（先 `isfinite()` 再 `constrain()`），
  防止半写产生的 NaN 穿透校验
- 每完成 `COMPLETIONS_PER_SAVE = 10` 次分液才 `markDirty()`，且只在
  `IDLE` / `DONE` 状态下落盘，避免运行中写 flash

### 反馈

- **蜂鸣器** — 非阻塞音序状态机，5 种音效（确认 / 取消 / 启动 / 暂停 / 完成）。
  `buzzer_tick()` 每帧推进，绝不 `delay()`
- **WS2812 状态灯** — 待机暗绿呼吸 / 运行蓝（TIME 深蓝、JET 品红）/ 暂停琥珀脉动 /
  完成亮绿在 `DONE_HOLD_MS` 内渐暗 / 回吸青色脉动；管路寿命 > 80% 时叠加红色闪烁
- **实时遥测** — `/api/status` 返回完整 JSON，前端 1 s 轮询（`index.html:272`）

### 智能保护

- **管路寿命追踪** — 累计流量统计，达到设定值时 LED 红灯告警（`tubeLifeML = 0` 表示禁用）

> 本分支**没有**堵转检测、没有自动关使能（ENA 未接线，`stepperEnabled` 恒为 true）、
> 没有 BLE。三者都曾在文档里出现过，实际要么从未实现、要么已删除，详见 §6。

---

## 4. 控制接口

三个通道共用同一套 JSON 命令协议，全部在 `loop()` 单线程内**串行**执行
（`processSerialCommands` → `processHardwareUart` → `handleWebClients` → `pump_machine_tick`），
因此不需要互斥锁。

| 通道 | 说明 |
|---|---|
| **WiFi Web UI** | 手机 / PC 浏览器直连，PWA 可添加到桌面 |
| **USB Serial** | USB-CDC，115200 bps |
| **Hardware UART** | GPIO21 = RX，GPIO47 = TX，115200 bps，USB-TTL 直连 PC |

### WiFi

- **双模启动** `WIFI_AP_STA`，不做运行时模式切换（避免 LWIP 冲突）
- **SoftAP 始终可用**：SSID `PumpCtrl-XXXX`（`XXXX` = MAC 后两字节），密码见
  `wifi_manager.h` 的 `WIFI_AP_PASSWORD`，IP `192.168.4.1`
- **mDNS**：`http://pump.local`（`initWiFi()` 里先 `MDNS.end()` 再 `begin()`，
  否则 `restartWiFi()` 重入时会因 "Service already exists" 失败）
- **STA 连接家里 WiFi**：Web UI → WiFi 设置 → 扫描 / 填入 SSID 与密码 → 保存。
  连接失败不影响 SoftAP；断线重连由 esp_wifi 自身负责（`WiFi.begin()` 默认开启自动重连），
  固件不做轮询维护
- **配置持久化**：存 EEPROM，STA 密码用设备 MAC 做 XOR 混淆
- **扫描**：同步阻塞约 8 s，结果缓存 15 s，自动过滤自身 AP，点击 SSID 自动填入。
  泵处于 `RUNNING` / `PAUSED` / `ANTI_DRIP` 时拒绝扫描
- TX 功率 20 dBm 全功率，`WIFI_PS_NONE` 关闭省电（避免唤醒延迟影响步进）

> ⚠️ **整个 HTTP API 没有鉴权**，同网段任何人都能启停泵、改校准、改 WiFi 配置；
> SoftAP 密码是全设备统一的硬编码值。XOR 只是混淆不是加密（密钥是 eFuse MAC）。
> 见 [§6 B 组](#b-组需产品决策未擅自改动)。

### Web UI

内嵌单页应用（`index.html` 编译进 `web_ui_gen.h`），手机端 PWA：

实时仪表盘（状态 / 模式 / 进度 / 流量 / 体积 / 时间）· 运行控制 · 参数设置 ·
模式切换 · 液体选择 · 校准向导 · 预灌快排 · WiFi 管理（扫描 / 配置 / 密码明文切换）·
高级设置独立面板（回吸量 / 管路寿命）· 管路寿命百分比 · PSRAM 与堆内存监控 ·
header 显示当前可访问地址 · STA 连接成功自动弹提示

### HTTP API

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/` | Web UI 页面（约 24 KB，内嵌） |
| GET | `/manifest.json` | PWA 清单 |
| GET | `/api/status` | 完整遥测 JSON |
| GET | `/api/cmd?c=<cmd>&v=&s=&m=&i=` | 发送命令 |
| POST | `/api/wifi` | 保存 WiFi 配置（**响应发完后**固件才重启网络） |
| GET | `/api/scan` | 扫描附近 WiFi（同步阻塞 ~8 s，结果缓存 15 s，泵忙时拒绝） |

所有响应都带 `Content-Length`，状态码配正确的理由短语，`handleWebClients()`
在 `stop()` 前先 `flush()`。请求用 2 KB 固定缓冲成块读取。

### 命令列表

| 命令 | 参数 | 说明 |
|---|---|---|
| `start` | — | 启动泵送 / 喷射循环 |
| `pause` / `resume` | — | 暂停 / 恢复 |
| `stop` / `reset` | — | 停止并复位 |
| `set_mode` | `mode`: `VOLUME` / `TIME` / `JET` | 切换模式（会 `resetPump()`） |
| `set_liquid` | `index`: 0–3 | 切换液体 |
| `set_flow` | `value`: 0.1–1600 | 流量 mL/min |
| `set_volume` | `value`: 0.1–99999 | 目标体积 mL |
| `set_time` | `value`: 1–86400 | 目标时间 s |
| `set_jet_vol` | `value`: 0.1–10 | 单次喷射量 mL |
| `set_jet_interval` | `value`: 1–60 | 喷射间隔 s |
| `set_jet_flow` | `value`: 10–1600 | 喷射流量 mL/min |
| `set_jet_pressure` | `value`: 1–10 | 喷射压力等级 |
| `jet_start` / `jet_stop` | — | 喷射启停（JET 模式下与 `start` / `stop` 等效） |
| `set_anti_drip` | `value`: 0–5 | 回吸量 mL |
| `set_tube_life` | `value`: 0–200000 | 管路寿命 mL（0 = 禁用） |
| `calib_enter` → `calib_select_liquid` → `calib_set_vol` → `calib_start_run` → `calib_stop_run` → `calib_measure` → `calib_save` → `calib_settings_done` | 见说明 | 校准向导；`calib_abort` 可在任意步退出 |
| `prime_start` / `prime_stop` | — | 预灌快排 |
| `get_state` | — | 获取完整遥测 |
| `wifi_restart` | — | 重启 WiFi |
| `menu_main` | — | 返回主菜单 |

> **调用方分布**：`calib_select_liquid` 与 `get_state` 只有 `pump_cli.py` 在用，Web UI 不发；
> `menu_main` / `jet_start` / `jet_stop` / `reset` 前端和 CLI 都不发，仅作为协议兼容性保留。

### 遥测响应

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

- `state`：`IDLE` / `RUNNING` / `PAUSED` / `DONE` / `ANTI_DRIP`
- `menu`：`MAIN` / `CALIBRATE` / `PRIME`
- `mode`：`VOLUME` / `TIME` / `JET`
- `liquid`：`Wtr` / `Thk` / `Liq1` / `Liq2`
- `wifiMode`：`ap` 或 `sta+ap`
- `heapTotal` 是 `ESP.getHeapSize()` 实测值；`psram*` / `heap*` 单位 KB
- `stepperEnabled` 恒为 `true`（见 §6 B3）

### 状态机

```
        start                         完成 (antiDripVol>0)          DONE_HOLD_MS
IDLE ─────────> RUNNING ──────────────────────────────> ANTI_DRIP ──────────> DONE ──────> IDLE
                  ⇅  pause / resume                     (antiDripVol=0 时直接 → DONE)
                PAUSED

Mode: MODE_VOLUME ⇄ MODE_TIME ⇄ MODE_JET
      JET 模式下 RUNNING 持续循环喷射, 必须显式 stop / jet_stop 才退出
```

`pump_machine.cpp` 提供 `pump_machine_transition()`（统一切换 + `on_entry()` 入口动作）
和 `pump_machine_tick()`（每帧按状态分发到 `tick_running` / `tick_anti_drip` / `tick_done`）。

### XToys

~~可通过硬件 UART 接入 [XToys](https://xtoys.app/)。~~ **已于 2026-07-22 放弃**，
串口功能保留。`xtoys_script.js` / `xtoys_js_only.js` 仅供参考。

---

## 5. 构建与烧录

### Arduino IDE 配置

| 选项 | 值 |
|---|---|
| Board | **ESP32S3 Dev Module** |
| Flash Size | **16MB (128Mb)** |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| PSRAM | **OPI PSRAM**（Octal SPI，N16R8） |
| USB CDC On Boot | **Disabled** |
| CPU Frequency | **160 MHz** |
| 串口波特率 | **115200** |

> ⚠️ **Partition Scheme 必须选 Huge APP。** 用默认方案（app 仅 1.25 MB）时占用会到 80%，
> 删除 BLE 之前更是高达 95%，再加一点功能就链接不过。

命令行等价（本机实测）：

```
arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=huge_app" peristaltic_pump
```

### 依赖库

| 库 | 版本（本机实测） | 用途 |
|---|---|---|
| [FastAccelStepper](https://github.com/gin66/FastAccelStepper) | 1.2.7 | 步进电机驱动。ESP32 后端实测编入的是 `StepperISR_idf5_esp32_mcpwm_pcnt.cpp`，即 **MCPWM + PCNT**（不是旧文档写的 RMT） |
| [ArduinoJson](https://arduinojson.org/) | 7.4.3 | JSON 解析 / 序列化 |
| [Adafruit_NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) | 1.15.5 | WS2812 状态灯（RMT） |

ESP32 Arduino core **3.3.10**。其余 `WiFi.h` / `ESPmDNS.h` / `EEPROM.h` / `esp_wifi.h` /
`esp_mac.h` / `esp_heap_caps.h` 均为 core 自带。

> U8g2 与 Keypad 已从依赖中移除 —— 代码里零引用（OLED 与键盘 v2.0 就停用了）。

### 资源占用（实测）

```
Sketch uses 1046196 bytes (33%) of program storage space. Maximum is 3145728 bytes.
Global variables use 52840 bytes (16%) of dynamic memory, leaving 274840 bytes for local variables.
```

本项目代码在 `--warnings all` 下**零警告**（剩余警告全部来自 FastAccelStepper /
esp32 core / tinyusb）。

| 存储器 | 容量 | 占用 |
|--------|------|------|
| Flash | 16 MB（Huge APP 分区 3 MB） | 固件 1.00 MB / **33%** |
| 内部 SRAM | ~320 KB | 静态 51.6 KB / **16%**，其余给 WiFi 协议栈与 FreeRTOS 任务栈 |
| PSRAM | 8 MB Octal | 遥测 / 响应 / 串口缓冲区优先分配在此，失败回退内部 RAM |

删除 BLE（NimBLE）带来 1.20 MB → 1.00 MB（省 202 KB）、内部 SRAM 静态占用
56.3 KB → 49.7 KB（省 6.7 KB）；之后 HTTP 请求缓冲又占回 2 KB，即上表的 51.6 KB。

### 项目结构

> ⚠️ **Arduino 只编译 sketch 目录内的文件**，且目录名必须与 `.ino` 同名。
> 所有参与编译的 `.ino` / `.cpp` / `.h` 必须全部位于 `peristaltic_pump/` 下 ——
> 放在仓库根会导致 `fatal error: xxx.h: No such file or directory`。
> 本分支曾因此完全无法编译（见 A1）。

```
peristaltic-pump/                 # 仓库根 (master 分支)
├── peristaltic_pump/             # ← Arduino sketch 目录，用 IDE 打开这个
│   ├── peristaltic_pump.ino      # setup / loop 组装
│   ├── pump_shared.h             # 枚举 / 常量 / 引脚 / extern 声明
│   ├── pump_state.h/.cpp         # PumpState 结构体，所有运行状态集中管理
│   ├── pump_machine.h/.cpp       # 状态机：transition() + on_entry() + per-state tick
│   ├── pump_core.h/.cpp          # 泵控制核心：启停 / 暂停恢复 / 喷射 / 校准 / applySpeed
│   ├── command_protocol.h/.cpp   # JSON 命令解析、路由、遥测构造
│   ├── web_handlers.h/.cpp       # HTTP 服务（WiFiServer，无 AsyncTCP 依赖）
│   ├── wifi_manager.h/.cpp       # SoftAP + STA 双模，WiFi 配置持久化
│   ├── serial_commands.h/.cpp    # USB CDC 与硬件 UART 命令入口
│   ├── eeprom_store.h/.cpp       # 泵参数持久化（offset 0-63）
│   ├── buzzer.h/.cpp             # 非阻塞蜂鸣器音序状态机
│   ├── led.h/.cpp                # WS2812 状态灯动画
│   ├── index.html                # Web UI 源文件（编辑入口）
│   ├── generate_web_ui.py        # index.html → web_ui_gen.h
│   └── web_ui_gen.h              # 生成文件，勿手改
├── pump_cli.py                   # Python CLI 控制工具（tools/ 下有一份完全相同的副本）
├── tools/pump_cli.py
├── xtoys_script.js               # XToys 集成脚本（已放弃，仅供参考）
├── xtoys_js_only.js
├── minimal_test/                 # 硬件最小验证 sketch
├── hardware/                     # PCB / 结构件
├── LICENSE
└── README.md
```

### Web UI 生成流程

改完 `index.html` 后**必须**重新生成，否则固件里的页面不会更新：

```
python peristaltic_pump/generate_web_ui.py
```

> ⚠️ **`index.html` 里的 JS 不要用 `//` 行注释。** 生成脚本用 `re.sub(r"\s+", " ", html)`
> 压缩空白，会把换行也变成空格 —— 一旦出现 `//`，它后面的所有代码都会被并进这一行注释里，
> 生成的页面静默失效。需要注释请用 `/* ... */`。
>
> 这不是假想风险：commit `e02b43c`（"fix: Web UI 从 WebSocket 改为 HTTP 轮询，
> 修复 `//` 注释被压缩破坏的问题"）就是踩了这个坑之后修的。

校验是否同步：重跑一次生成脚本后 `git diff -- peristaltic_pump/web_ui_gen.h` 应为空。

### 命令行工具

`pump_cli.py` 通过串口或 HTTP 发送 JSON 命令，可用于无浏览器的调试。
注意它会打开串口，脚本里已设 `DTR=False` / `RTS=False` 防止 CH340 复位 ESP32。

---

## 6. 代码审查记录 (2026-09-26)

对本分支做的一次完整审查。三个 commit：

| commit | 内容 | 规模 |
|---|---|---|
| `fa4cbee` | 把全部源文件移入 `peristaltic_pump/` sketch 目录 | 27 files，纯 rename（numstat 全为 `0 0`） |
| `523a773` | 删除 BLE、堵转检测与全部死代码 | 22 files，+84 / −1472 |
| `b024a6e` | 修复 16 处缺陷 + STA/AP 两处 + 清理乱码 + 本 README | 17 files，+673 / −312 |

每个 commit 都单独编译验证过。完整的根因分析写在 `b024a6e` 的 commit message 里。

### A 组：已修复的缺陷

| # | 问题 | 修法 |
|---|---|---|
| **A1** | **工程完全无法编译** —— `.ino` 已移入子目录而 24 个 `.cpp/.h` 仍在仓库根 | 全部移入 `peristaltic_pump/`（`fa4cbee`） |
| **A2** | 体积/时间模式一启动就误报堵转：`stallCheckTime` 初值 0 且从不重置，进 loop 时 `millis() ≥ 2400` 使 `millis()-0 > 1500` 恒真；而 `getCurrentPosition()` 返回 PCNT 实发脉冲数，`moveTo()` 后首帧仍为 0 | 整体删除堵转检测（DM542 无反馈能力，见 `523a773`） |
| **A3** | **低速时电机不动却上报「已分液 X mL」** | `applySpeed()` 统一入口 + `setSpeedInMilliHz()` + 检查返回值。详见 [§1](#1-待办回移到-tmc2226-的修复) |
| **A4** | `stepperConnectToPin()` 失败 → 紧随其后的 `updateStepperSpeed()` 解引用空指针 → 崩溃重启循环 | setup 里打印 FATAL 并停在 `for(;;)` |
| **A5** | 暂停后恢复多打约 0.13 mL（1600 Hz 下 `forceStop()` 需约 20 ms 才真停，队列内命令仍会走完） | `forceStop()` 后轮询 `isRunning()` 到 false，上限 200 ms |
| **A6** | 校准可把 `stepsPerMl` 存成 0（`calibCalculate()` 静默失败但 `calib_measure` 仍返回 `ok:true`） | `calibCalculate()` 返回 `bool`，三层校验 |
| **A7** | 首次上电 EEPROM 初始化是空操作（`saveParams()` 首行就 `if (!eepromDirty) return`）→ 管路寿命在用户首次改参数前不持久化 | `if (!loadParams()) { markDirty(); saveParams(); }` |
| **A8** | `constrain(NaN,…)` 原样返回 NaN（两个比较都为假），而 flash 擦除态 `0xFFFFFFFF` 就是 NaN → `moveTo((int32_t)NaN)` 是 UB | 新增 `clampF()`，先 `isfinite()` 再 `constrain()` |
| **A9** | 保存 WiFi 后 `pump.local` 失效（`restartWiFi()` 重入 `initWiFi()`，第二次 `MDNS.begin()` 报 "Service already exists"） | 补 `MDNS.end()` |
| **A10** | 点「保存 WiFi」彻底静默失败（`restartWiFi()` 在 `sendJson()` 之前就拆掉承载响应的连接；前端无 `.catch()`；成功后还会再发一次 `wifi_restart` 等于重启两遍） | 调整为 `sendJson` → `flush` → `stop` → `delay(300)` → `restartWiFi()`；前端去掉重复调用并补 `.catch()` |
| **A11** | Web UI 页面偶发截断（响应无 `Content-Length`，且 `stop()` 前没 `flush()`，24 KB 的 `WEB_UI` 有风险） | 全部响应带 `Content-Length` + `flush()` |
| **A12** | 发出 `HTTP/1.1 404 OK`（`sendJson` 对所有状态码都拼 `" OK"`） | 新增 `reasonPhrase()` |
| **A13** | LF-only 客户端的 POST body 被吃掉前 2 字节（接受 `\n\n` 但一律按 4 字节算） | 新增 `findHeaderEnd()` |
| **A14** | 长命令帧被截断（RX 环形缓冲默认 256 B < 单帧 1024 B） | `begin()` 前 `setRxBufferSize(SERIAL_BUF_SIZE)` |
| **A15** | 单段音效完全无声、多段音效最后一段被切（`noTone()` 与最后一次 `tone()` 在同一 tick，而 core 3.x 的 `noTone()` 会 `xQueueReset()` 丢掉刚投进队列的 `TONE_START`） | 播完最后一段后 `return`，收尾 `noTone()` 推迟到下一个 tick |
| **A16** | 「三连音报警」最多响一次（连调三次 `beepCancel()`，而 `startSeq()` 每次重置音序） | 随堵转检测一并删除 |
| **A17** | 遥测 `heapTotal` 是硬编码 `327680`，不是实测值 | 改用 `ESP.getHeapSize()` |
| **A18** | TIME 模式覆写 `pump.flowRate`，跑完后 UI 显示用户没设过的值，重启又变回去 | 拆出 `activeFlowRate`；`resumePump()` 与回吸也改用它 |
| **A19** | 回吸中触发 WiFi 扫描会冻结状态机 8 秒 | 忙碌判断补上 `ANTI_DRIP` |
| **A20** | DONE 的绿灯渐暗永远走不完（按 150 帧 ≈3 s 算，但 DONE 只保持 2 s） | 新增共享常量 `DONE_HOLD_MS`，两处同源；相位重置移到算颜色之前 |
| **A21** | `setRGBDim()` 的 `dim` 超出声明的 0.0–1.0（PAUSED 达 1.1、ANTI_DRIP 达 1.3） | 表达式收敛到 [0.30, 1.00] + 函数内钳位；`sin()` → `sinf()` |
| **A22** | 逐字节 `String += c` 拼请求造成堆碎片（500 B 请求 = 500 次 malloc/free） | 改 2 KB 固定缓冲 + 成块 `read()` |
| **A23** | `getQueryParam()` 未锚定（`/api/cmd?xc=start` 也会命中 `c=`） | 只认 `?` 之后、以 `&` 分隔的 `key=` 片段 |

一并删除的死代码：FreeRTOS 命令队列整套（40 行，零调用点）、`wifiMaintain()` +
`staConnecting` + `staConnectStart`（只改自己读的 flag，无任何可观察效果）、
`/api/info`（仓库内零消费者）、`getApSSID()`、`wifiReady`、`lastStepperActivity`
（5 处写入 0 处读取）、`IDLE_DISABLE_MS`、`prevState`、数字输入缓冲四函数、
`beepInput()` / `beepDisable()`、`led_update()` 空函数、`led.cpp` 的 `pulse` /
`g_lastWifiCli` / `g_lastBleConn` / `g_lastEnabled`、`Menu` 枚举 9 个不可达值、
`build_web_ui.py`（输出路径硬编码指向另一个旧项目副本）、`data/www/index.html`
（代码里无 LittleFS/SPIFFS）。

### B 组：需产品决策，未擅自改动

| # | 问题 | 说明 |
|---|---|---|
| **B1** | **整个 HTTP API 零鉴权** | 同网段任何人都能启停泵、改校准；`POST /api/wifi` 能改网络配置。SoftAP 密码是全设备硬编码的 `12345678`（已收敛为 `WIFI_AP_PASSWORD` 宏，但值未变）。加鉴权会改变使用方式（AP 模式下手机要先连 WiFi），形式也需定（Basic Auth？固定 token？仅 AP 模式放开？） |
| **B2** | `/api/cmd` 拼 JSON 不转义不校验 | `cmd` / `v` / `s` / `m` / `i` 直接字符串拼接。实际危害有限（`parseAndExecute` 只 `strcmp` 已知命令，畸形 JSON 会解析失败），但属于未净化输入进 JSON 构造器。与 B1 一起决定 |
| **B3** | `stepperEnabled` 恒为 `true` | 删除堵转检测后没有任何地方会置 false（ENA 本来也「未接，方案D」）。仍被遥测与 `index.html` 的「使能/断电」显示消费，删除会改遥测 schema |
| **B4** | EEPROM 写放大 | 每 10 次分液 commit 一次全量 64 B；JET `interval=1s` 时约每 10 s 一次 NVS 写。24/7 运行的磨损需评估 |
| **B5** | `pump_cli.py` 两份完全相同的副本 | 根目录与 `tools/` 各一份（hash 一致），留哪份待定 |
| **B6** | 四个无人调用的协议命令 | `menu_main` / `jet_start` / `jet_stop` / `reset`，前端和 CLI 都不发，但属 README 记载的协议命令，外部工具可能在用，故保留 |
| **B7** | XOR「加密」只是混淆 | 密钥是 eFuse MAC，而 AP SSID 会把其中 2 字节广播出去。旧注释「拆机读 EEPROM 也是乱码」言过其实 |

### C 组：已订正的文档偏差

原 README 与代码不符、本次已改正的 15 处：流量上限 `2000` → **1600**；堵转「3 秒」→
实际 `STALL_TIMEOUT_MS` 是 1500（功能已整体删除）；「方案预设 4 槽位」与
`preset_load` / `preset_save`（v2.3.8 `2a34518` 已删，代码里 grep 不到 `preset`）；
「自动关使能，待机 5s 断电」（`IDLE_DISABLE_MS` 从未使用、`beepDisable()` 从未调用、
ENA 拉 HIGH 后再没动过）；「喷射间隔 >15s 自动断电，提前 2s 通电」（JET 分支无此逻辑）；
「ANTI_DRIP 不可打断」（`stop`/`reset` 可打断）；「匀加速，缓启动/缓停止」（停止是
`forceStop()` 硬停）；「FreeRTOS 命令队列，线程安全」（队列是死代码）；
「FastAccelStepper RMT 硬件加速」（实为 MCPWM + PCNT）；「WebSocket 和 USB Serial 共用」
（项目里没有 WebSocket）；「I2C GPIO 21(SDA)/7(SCL)」（与 `HW_UART_RX 21` 冲突）；
「固件 (1.2MB) + SPIFFS」（与 Huge APP 配置矛盾，且代码不用文件系统）；
遥测示例 `"liquid":"Water"`（实为 `"Wtr"`）；`/api/scan` 「~1s」（实为 ~8s）；
Web UI 的「方案预设」与仪表盘「温度」（UI 无预设，硬件无温度传感器）。

### D 组：源码编码损坏

`command_protocol.cpp`（25 处）与 `web_handlers.cpp`（整文件）的中文注释曾是**有损
mojibake**：UTF-8 字节被按 GBK 解码后又存成 UTF-8，且 GBK 无法解码的尾字节已被替换成
字面 `?`（例：`—` → `鈥?`），**无法靠转码还原**，只能按代码语义手工重写。现已全部重写为
正确 UTF-8（保留 BOM 与 CRLF）。`/manifest.json` 的 `"name"` 字面量原本是乱码
`锠曞姩娉垫帶鍒跺櫒`（会直接显示成 PWA 安装名），已还原为 `蠕动泵控制器`。

> 校验方法：把文件里所有含非 ASCII 的行导出后逐行核对。
> 注意「把文本 `encode('gbk')` 再 `decode('utf-8')`」这种自动检测法**会误报**
> （例如正常的 `状态` 就能被"还原"成垃圾），别拿它当唯一判据。

### 未上板验证

以上修复**只做了编译验证**。需要实测的项目：

- **A15 蜂鸣器** —— 结论来自读 `Tone.cpp`（`tone()` 异步投队列、`noTone()` 会
  `xQueueReset`）。「单段音效原来完全无声」需上板听一下确认；修复后
  `beepConfirm` / `beepCancel` 应是干净单音，`beepDone` 应是完整三连音
- **A3 + A18 低速** —— 设 0.1 mL/min 打 1 mL，电机应真的转
- **A5 暂停/恢复** —— 10 mL @ 500 mL/min，运行中暂停再恢复，出液量应 ≈10 mL
- **A10 / A11 Web 层** —— `web_handlers.cpp` 是整文件重写，6 个端点都要点一遍
  （`/` 页面完整不截断、`/api/status`、`/api/cmd`、`POST /api/wifi` 应出现「已保存」
  toast、`/api/scan`、`/manifest.json` 的 PWA 名应为中文）
- **A7** —— 全新板子首次上电后立刻断电重启，`totalDispensed` 应保持而非归零

---

## 7. 更新日志

> 2026-09-26 之前的条目记录的是**当时**的代码状态。其中若干机制此后已被移除或改正
> （FreeRTOS 命令队列、`/api/info`、STA 30 s 超时、3 秒堵转检测、"RMT 迁移"），
> 阅读时请以 §3–§5 的当前描述为准。

### 2026-09-26 — 维护性审查（归档后例外）

修复构建阻塞（A1）、低速分液静默失败（A3）、启动空指针（A4）等 16 处缺陷；
删除 BLE、堵转检测与全部死代码（−1472 行）；清理两个文件的有损 mojibake；
README 全面重写。三个 commit：`fa4cbee` / `523a773` / `b024a6e`，
每个都单独编译验证，最终固件 1046196 bytes (33%)、内部 SRAM 静态 52840 bytes (16%)、
本项目代码零警告。详见 [§6](#6-代码审查记录-2026-09-26)。

### v2.3.8 (2026-07-30)

- DM542 分支归档，TMC2226 版本独立维护于 `tmc2226` 分支
- 删除方案预设功能（净减 212 行）

### v2.3.6 (2026-07-21) — UART 串口通信修复

**固件：**

- `RESPONSE_BUF_SIZE` 512 → 1536：`get_state` 响应 JSON 超过 512 字节被 `snprintf()`
  截断，导致 UART 端遥测不完整
- `SERIAL_BUF_SIZE` 512 → 1024：同步扩大串口接收缓冲区

**CLI (`pump_cli.py`)：**

- 打开串口后设置 `DTR=False` + `RTS=False`，防止 CH340 复位 ESP32
- 启动等待 0.5 s → 5 s，匹配 ESP32-S3 完整启动时间
- `readline()` 超时 0.1 s → 0.5 s，确保 600+ 字节的长响应不会在行中被截断
- Emoji 改为纯文本，修复 Windows GBK 编码错误

### v2.3.5 (2026-07-19) — 软件打磨完成

**WiFi 扫描重写：**

- 异步 → 同步扫描，绕过 ESP32 Arduino `_scanStatus` 状态机 bug
  （`scanDelete()` 后 `scanNetworks()` 仍返回 −1）
- 扫描前断开正在连接的 STA，释放射频资源
- 结果缓存 15 秒，防止前端轮询（1 s 一次）堆积
- 自动过滤自身 SoftAP SSID

**WiFi 双 AP 修复：**

- `WiFi.persistent(false)` 禁用 NVS 自动加载旧 AP 配置
- `WiFi.softAPdisconnect(true)` 确保创建 AP 前清理残留

**Web UI：** 高级设置独立面板（回吸量 / 管路寿命），不再耦合校准流程；
校准步骤 6 → 5；遥测新增 `tubeLifeML`

**校准修复：** `calibFinishRun()` 自动进入 `CALIB_MEASURE`（运行完成不再卡步骤）；
前端 `sendCmd` 接收 `calib_measure` 响应并更新 SPM 结果

**电机丢步：** ✅ 已解决

### v2.3.4 (2026-07-18)

- WiFi TX 功率恢复 20 dBm 全功率（热管理问题已解决，无需再限制 8 dBm）
- 6N137 光耦隔离
- Web UI 修复

### v2.3.2 (2026-07-12) — 架构重构

- 引入 `PumpState` 结构体，所有运行参数 / 状态 / 校准数据集中于 `pump_state.h`
- 提取 `pump_machine` 状态机模块：`transition()` 统一切换 + `on_entry()` 入口回调
- `loop()` 从约 150 行状态处理缩减为 `pump_machine_tick()` 一行调用
- 每个状态独立 `tick_*()` 函数
- 移除 OLED 字库相关文件（`pump_chinese_font.h`、`strip_chinese.py` 等）

### v2.3.1 (2026-07-10)

- 步进驱动从 AccelStepper 迁移到 FastAccelStepper
  （当时记为「RMT 迁移」，实测 1.2.7 在 ESP32 上编入的是 MCPWM + PCNT 后端）
- DM542 共阴接法，400 细分

### v2.2 (2026-07-08)

- **PSRAM 全面启用**：N16R8 的 8 MB Octal PSRAM（OPI）；遥测 / 响应 / USB 串口 /
  HW UART 静态缓冲区移到 PSRAM，节省约 2.2 KB 内部 SRAM；Web UI 底部显示实时用量
- **WiFi STA 双模**：`initWiFi()` 重写为 `WIFI_AP_STA` 直接启动，避免模式切换的
  LWIP 冲突；SoftAP 始终保留作 fallback；新增 `POST /api/wifi`、`GET /api/scan`、
  `GET /api/info`；STA 连接成功弹 toast；密码框 👁 明文切换；header 显示可访问地址
- **WiFi 密码 XOR 加密存储**，密钥为 ESP32 出厂熔丝 Base MAC；解密后非可打印 ASCII
  则清空配置回退 SoftAP
- **堵转检测**（*已于 2026-09-26 删除，DM542 无位置反馈，误报率高*）
- **热管理**：CPU 240 MHz → 160 MHz
- 修复：DONE 状态可重新启动；`set_mode` 增加 `resetPump()`；液体选择行从遥测同步；
  喷射模式 UI 布局

---

## License

见 [LICENSE](LICENSE)。
