# 蠕动泵控制器 — YZ1515 精密点液 / 喷射工作站（DM542 版）

> `master` = **DM542 驱动版本，v2.4.3**，本仓库唯一的分支。
>
> 2026-09-26 起对本分支做了一轮维护：**维护性审查**（修构建阻塞 + 删死代码 + 修 16 处缺陷）
> 和 **校准向导重构 + 定时模式倒计时**（= v2.4.0），详见
> [§5 代码审查记录](#5-代码审查记录) 与 [§6 更新日志](#6-更新日志)。
>
> 固件自报的版本号（串口 hello 里的 `version` 字段）以 `peristaltic_pump/version.h`
> 的 `FW_VERSION` 为**唯一来源**；发版时要同步本行、§6 新条目和 `git tag`。

基于 ESP32-S3 的蠕动泵控制器，驱动 YZ1515 工业泵头，实现**体积 / 时间 / 喷射**三种模式的
精密流体控制。步进驱动为 **DM542 数字驱动器 + 6N137 光耦隔离**，控制端为内嵌 Web UI
（手机 PWA）+ USB 串口 + 硬件 UART 三通道 JSON 协议。

> **本分支为什么没有堵转检测？** DM542 是开环驱动，没有位置 / 堵转反馈能力，
> 用「位置 N 秒不变」推断堵转本身不可靠 —— 实测会在正常启动时误报（见
> [A2](#a-组已修复的缺陷)），故 2026-09-26 整体删除。

---

## 目录

1. [硬件](#1-硬件)
2. [功能](#2-功能)
3. [控制接口](#3-控制接口)
4. [构建与烧录](#4-构建与烧录)
5. [代码审查记录](#5-代码审查记录)
6. [更新日志](#6-更新日志)

---

## 1. 硬件

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

## 2. 功能

### 泵送模式

| 模式 | 参数 | 行为 |
|---|---|---|
| **体积** `MODE_VOLUME` | 流量 0.1–1600 mL/min，目标体积 0.1–99999 mL | 恒速运行到目标步数自动停止 |
| **时间** `MODE_TIME` | 体积 + 时间 1–86400 s | 运行时反算实际流量写入 `activeFlowRate`（**不覆盖**用户设定的 `flowRate`）。反算要扣掉升速/减速段：加速度是 `ACCEL_FACTOR×pps`，所以升到全速恒需 `1/ACCEL_FACTOR ≈ 3.33 s`，减速段等长。梯形支（`T ≥ 6.67 s`）取 `v = V/(T−3.33)`，三角形支取 `v = 4·V·3.33/T²`，两支都让总耗时正好落在 `T`；被 1600 mL/min 钳住时才由时间截断兜底（+1 s 容差），**此时按实际出液量记管路寿命，不是按目标体积** |
| **喷射** `MODE_JET` | 单次量 0.1–10 mL，间隔 1–60 s，流量 10–1600 mL/min，压力 1–10 级 | 循环喷射，无回吸。`RUNNING` 会一直循环，必须显式 `stop` / `jet_stop` 才退出 |

喷射模式的双参数语义：**流量**管远近，**压力**（映射为加速度倍率）管爆发力。

### 运行控制

- **暂停 / 恢复** — 暂停时先 `forceStop()`，再轮询 `isRunning()` 到 false 才记录断点。
  `isRunning()` 的定义含 `!isQueueEmpty()`，所以轮询它就是等待硬停完成的公开做法
  （`blockingWaitForForceStopComplete()` 是 private，用不了）。这样恢复不会多打
- **定时模式倒计时** — Web UI 在 `TIME` 模式下把进度显示成 `⏳ 剩余 m:ss`（超过一小时为
  `h:mm:ss`），进度条按 `elapsed / targetTime` 走，右侧目标值显示秒数而不是毫升；
  `DONE` 显示「已完成」。暂停时遥测报冻结的 `pumpElapsed`，倒计时不会跳回满值。
  校准向导期间不显示倒计时 —— 校准那次运行始终是体积式的，与日常 `mode` 无关
- **防滴回吸** — 完成后以 0.3× 速度反转吸回 `antiDripVol`（0–5 mL），进入 `ANTI_DRIP` 状态。
  注意 `stop` / `reset` **可以**打断它
- **预灌 / 快排** — 1500 mL/min 全速排空管路，`moveTo(999999999)` 由 `forceStop()` 结束
- **速度曲线** — FastAccelStepper 匀加速启动（`ACCEL_FACTOR = 0.3`）；停止走 `forceStop()`
  **硬停，无减速斜坡**

### 校准与液体

- **4 种液体独立校准** — `Wtr` / `Thk` / `Liq1` / `Liq2`，各自独立的 `stepsPerMl`
- **校准向导** — 选液体 → 设体积与流量 → 运行 → 读量筒实际体积 → 计算并保存。
  `calib_abort` 可在任意步退出
- **校准用自己的液体 / 体积 / 流量** — 向导里选的分别写进 `calibLiquid` / `calibTargetVol` /
  `calibFlowRate`，校准运行全程只用这三个字段（步数按 `calibSPM()` = `liquidSPM[calibLiquid]`
  算），**不碰** `mode` / `flowRate` / `targetVolume` / `currentLiquid` / `stepsPerMl`。
  校准 1500 mL 时可以单独提速，日常设定一点不受影响
- **唯一的提交点是 `calib_save`** — `calibSave()` 才把 `calibLiquid` 写成 `currentLiquid`、
  把新 `stepsPerMl` 写进对应的 `liquidSPM[]` 槽位并落盘；中途 `calib_abort` / `menu_main`
  退出则日常设定一字不变
  > 旧做法是进向导时把 `mode` 强制成 `VOLUME`、退出时还原。但 `calib_save` 在还原之前就
  > `markDirty()` + `saveParams()`，而 `mode` 是落盘字段（EEPROM offset 18）——
  > 校准保存一次，用户的 `TIME` / `JET` 模式就被永久写成 `VOLUME`。现在改成完全不碰它
- **校准期间锁定主面板** — `set_mode` / `set_flow` / `set_volume` / `set_time` / `set_liquid` /
  `start` / `prime_start` 在 `CALIBRATE` 菜单下一律返回错误（它们要么改日常设定，要么会把
  `currentMenu` 打回 `MAIN` 让向导悄悄死掉）；`stop` / `reset` 转成 `calibStopRun()`
  （会清 `calibRunning`），三条退出路径统一走 `calibLeave()`。Web UI 相应把模式标签、
  液体按钮、全部日常参数输入框和主启动键置灰，`停止` / `暂停` 保留可用
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
- **实时遥测** — `/api/status` 返回完整 JSON，前端 1 s 轮询
  （`index.html` 的 `connectHTTP()` 里 `setInterval(poll, 1000)`）

### 智能保护

- **管路寿命追踪** — 累计流量统计（`totalDispensed`），超过阈值的 80% 时 LED 叠加红色闪烁
  （`tubeLifeML = 0` 表示禁用）。计数器**只增不减**：4 处写入全是 `+=`（JET / VOLUME /
  TIME / 回吸的完成分支），`stop` / `reset` 走的 `resetPump()` 也不碰它，且落盘在
  EEPROM offset 23 —— 所以**换完管子必须用 `reset_tube_life` 清零**（Web UI 高级设置
  面板的「累计清零」按钮），否则 `tubePct` 和红灯告警永远消不掉。清零只动计数器，
  不改 `tubeLifeML` 阈值，运行中也接受（本次分液结束时照常累加）

> 本分支**没有**堵转检测、没有自动关使能（ENA 未接线，`stepperEnabled` 恒为 true）、
> 没有 BLE。三者都曾在文档里出现过，实际要么从未实现、要么已删除，详见 §5。

---

## 3. 控制接口

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
> 见 [§5 B 组](#b-组需产品决策未擅自改动)。

### Web UI

内嵌单页应用（`index.html` 编译进 `web_ui_gen.h`），手机端 PWA：

实时仪表盘（状态 / 模式 / 进度 / 流量 / 体积 / 时间，**定时模式显示倒计时**）· 运行控制 ·
参数设置 · 模式切换 · 液体选择 · 校准向导（液体 / 体积 / 本次校准流量独立设定，
期间主面板日常参数与模式标签锁定）· 预灌快排 · WiFi 管理（扫描 / 配置 / 密码明文切换）·
高级设置独立面板（回吸量 / 管路寿命）· 管路寿命百分比 · PSRAM 与堆内存监控 ·
header 显示当前可访问地址 · STA 连接成功自动弹提示

### HTTP API

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/` | Web UI 页面（约 27 KB，内嵌） |
| GET | `/manifest.json` | PWA 清单 |
| GET | `/api/status` | 完整遥测 JSON |
| GET | `/api/cmd?c=<cmd>&v=&s=&m=&i=&f=` | 发送命令（`f` = 校准流量，仅 `calib_set_vol` 用） |
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
| `reset_tube_life` | — | 累计流量 `totalDispensed` 清零（换管后用）。不改 `tubeLifeML` 阈值，不校验状态 |
| `calib_enter` → `calib_select_liquid` → `calib_set_vol` → `calib_start_run` → `calib_stop_run` → `calib_measure` → `calib_save` → `calib_settings_done` | `calib_select_liquid`: `index` 0–3（写 `calibLiquid`，保存后才成为日常液体）；`calib_set_vol`: `value` 0.1–99999（校准体积 mL）+ 可选 `flow` 0.1–1600（本次校准流量 mL/min，缺省沿用当前 `flowRate`）；`calib_measure`: `value`（量筒实测 mL） | 校准向导；`calib_abort` 可在任意步退出。向导只写 `calibLiquid` / `calibTargetVol` / `calibFlowRate`，**不改任何日常设定**，唯一提交点是 `calib_save`。**向导期间主面板锁定**：`set_mode` / `set_flow` / `set_volume` / `set_time` / `set_liquid` / `start` / `prime_start` 返回错误，`stop` / `reset` 转成 `calibStopRun()`（会清 `calibRunning`），退出统一走 `calibLeave()` |
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
  "calibFlow": 50.0,
  "calibLiquid": 0,
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
- `mode`：`VOLUME` / `TIME` / `JET` —— 始终是**用户的日常设定**，校准向导不会改它
- `calibLiquid` / `calibTargetVol` / `calibFlow`：本次校准的液体与体积 / 流量，独立于日常的
  `liquidIdx` / `targetVol` / `flow`，互不覆写；只有 `calib_save` 会把它们提交成日常值。
  校准运行时 `progress` 以 `calibTargetVol` 为分母
- `elapsed`：`RUNNING` 时是实时秒数，`PAUSED` 时是冻结的 `pumpElapsed`（定时模式倒计时靠它），
  其它状态为 0
- `liquid`：`Wtr` / `Thk` / `Liq1` / `Liq2`
- `wifiMode`：`ap` 或 `sta+ap`
- `heapTotal` 是 `ESP.getHeapSize()` 实测值；`psram*` / `heap*` 单位 KB
- `stepperEnabled` 恒为 `true`（见 §5 B3）

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

## 4. 构建与烧录

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

### PlatformIO（与上面共用同一批源文件）

`platformio.ini` 放在仓库根，`src_dir` 直接指向 `peristaltic_pump/`。PlatformIO 会把
`.ino` 当 `.cpp` 编译并自动补函数原型，所以 sketch 目录不用改名、拆分或复制 ——
两条构建路径吃的是同一份代码，改哪边都生效。

```
pio run                # 编译
pio run -t upload      # 烧录
pio run -t size        # flash / RAM 占用
pio device monitor     # 115200
```

| 项 | 值 | 等价的 IDE 设置 |
|---|---|---|
| platform | pioarduino `55.03.311`（GitHub release zip，官方 registry 没有这个包） | Arduino core **3.3.11** + ESP-IDF 5.5.5 |
| board | `esp32-s3-devkitc1-n16r8` | ESP32S3 Dev Module + 16MB Flash + OPI PSRAM |
| 分区 | `board_build.partitions = huge_app.csv` | Huge APP (3MB No OTA) |
| USB CDC | `build_flags = -DARDUINO_USB_CDC_ON_BOOT=0` | USB CDC On Boot: Disabled |
| 依赖 | `lib_deps` 钉死 1.2.7 / 7.4.3 / 1.15.5 | 见下表 |

> ⚠️ **platform 只能走 pioarduino。** 官方 `platformio/espressif32`（本机 7.0.1）只带
> core 2.0.17 / IDF 4.4，这份 3.x 代码编不过。

> ⚠️ **board 别退回 `esp32-s3-devkitc-1`** —— 那颗是 **N8，无 PSRAM**。pioarduino 判断
> PSRAM 只看 board 的 `extra_flags` / sdkconfig，`board_build.arduino.memory_type` 配了
> 也不生效，结果是编译照样通过、缓冲区全部退回内部 SRAM。`…-n16r8` 自带
> `-DBOARD_HAS_PSRAM` + `psram_type: opi` + 16MB。

> ⚠️ **`peristaltic_pump.ino` 不能带 UTF-8 BOM。** Arduino IDE 有无 BOM 都无所谓，但
> PlatformIO 的 `.ino → .cpp` 转换器会把 BOM 留在生成文件的首行，使 `#include <Arduino.h>`
> 整行失效：先报 `'\ufeff' does not name a type`，再跟一串 FastAccelStepper 的莫名级联错误。
> 约束只在 `.ino` 上 —— `.cpp/.h` 带不带 BOM 两边都能编（仓库里本来就混着用：11 个带、15 个不带）。

> ⚠️ **Windows 要开长路径支持**，否则 `esp32-core-*-libs` 解包失败：包内最深的头文件
> （`…/connectedhomeip/…/camera-av-settings-user-level-management-server.h`）超过 260 字符。
> 管理员 PowerShell 执行后重启：
> `New-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name 'LongPathsEnabled' -Value 1 -Type DWord -Force`

### 依赖库

| 库 | 版本（本机实测） | 用途 |
|---|---|---|
| [FastAccelStepper](https://github.com/gin66/FastAccelStepper) | 1.2.7 | 步进电机驱动。ESP32 后端实测编入的是 `StepperISR_idf5_esp32_mcpwm_pcnt.cpp`，即 **MCPWM + PCNT**（不是旧文档写的 RMT） |
| [ArduinoJson](https://arduinojson.org/) | 7.4.3 | JSON 解析 / 序列化 |
| [Adafruit_NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) | 1.15.5 | WS2812 状态灯（RMT） |

> ❌ **PlatformIO 这条路必须停在 FastAccelStepper 1.2.7，别升 1.4.0。** 1.4.0 的 RMT 组触发
> 代码（`SUPPORT_ESP32_RMT_SYNC` 分支）在 pioarduino 55.03.311 / core 3.3.11 / IDF 5.5.5 /
> GCC 14.2 下编不过：`esp32_queue.cpp:647` 用 `rmt_new_sync_manager(&chan, n, &mgr)` 调用
> 签名是 `rmt_new_sync_manager(const rmt_sync_manager_config_t*, rmt_sync_manager_handle_t*)`
> 的接口，另外还引用了未声明的 `rmt_sync_manager_add_channel()` / `rmt_sync_manager_del_channel()`。
> 同一天用 arduino-cli + 本机 core 3.3.11 编 1.4.0 **是通过的**，所以这是 PlatformIO 这条
> 路径上才出现的差异，具体成因未查 —— 不要把「1.4.0 坏了」推广到 IDE 那边。

ESP32 Arduino core **3.3.10**。其余 `WiFi.h` / `ESPmDNS.h` / `EEPROM.h` / `esp_wifi.h` /
`esp_mac.h` / `esp_heap_caps.h` 均为 core 自带。

> U8g2 与 Keypad 已从依赖中移除 —— 代码里零引用（OLED 与键盘 v2.0 就停用了）。

### 资源占用（实测）

v2.4.3，走 Arduino IDE / arduino-cli 路径
（`esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=huge_app`，core 3.3.11 +
本机 Arduino 库目录里的 FastAccelStepper **1.4.0**，`--warnings all`）：

```
Sketch uses 1043392 bytes (33%) of program storage space. Maximum is 3145728 bytes.
Global variables use 53076 bytes (16%) of dynamic memory, leaving 274604 bytes for local variables.
```

同一份源码走 PlatformIO（pioarduino 55.03.311 / core 3.3.11 / IDF 5.5.5 / GCC 14.2，
`lib_deps` 钉 FastAccelStepper **1.2.7**，`pio run -t clean` 后重编复现）：

```
RAM:   16.2% (used 53076 bytes from 327680 bytes)
Flash: 33.8% (used 1064052 bytes from 3145728 bytes)
```

两条路径的 **RAM 完全一致（53076 B）**，flash 差 20660 B，主要来自 FastAccelStepper 的
库版本（1.4.0 vs 1.2.7）；两边工具链细节未逐项归因。历史对照可佐证「差在库不在构建系统」：
v2.4.1 时 IDE 路径（core 3.3.10 + FAS 1.2.7）是 1051716 B / 52848 B，2026-10-05 用
core 3.3.11 + FAS 1.4.0 复测**同一条 IDE 路径**降到 1042168 B / 53068 B —— 光换库版本
就少约 9.5 KB flash。

两条路径下本项目代码在 `--warnings all` 下都是**零警告**，唯一一条来自 FastAccelStepper 的
`pd_esp32/StepperISR_idf5_esp32_mcpwm_pcnt.cpp:354` `gpio_iomux_in` 弃用提示
（其余噪声全部来自 FastAccelStepper / esp32 core / tinyusb）。

| 存储器 | 容量 | 占用 |
|--------|------|------|
| Flash | 16 MB（Huge APP 分区 3 MB） | 固件 1.00 MB / **33%** |
| 内部 SRAM | ~320 KB | 静态 51.8 KB / **16%**，其余给 WiFi 协议栈与 FreeRTOS 任务栈 |
| PSRAM | 8 MB Octal | 遥测 / 响应 / 串口缓冲区优先分配在此，失败回退内部 RAM |

删除 BLE（NimBLE）带来 1.20 MB → 1.00 MB（省 202 KB）、内部 SRAM 静态占用
56.3 KB → 49.7 KB（省 6.7 KB）；之后 HTTP 请求缓冲又占回 2 KB，v2.4.2 的斜坡补偿与
返回值收口再加约 0.1 KB，即上表的 51.8 KB。

### 项目结构

> ⚠️ **Arduino 只编译 sketch 目录内的文件**，且目录名必须与 `.ino` 同名。
> 所有参与编译的 `.ino` / `.cpp` / `.h` 必须全部位于 `peristaltic_pump/` 下 ——
> 放在仓库根会导致 `fatal error: xxx.h: No such file or directory`。
> 本分支曾因此完全无法编译（见 A1）。

```
peristaltic-pump/                 # 仓库根 (master 分支)
├── peristaltic_pump/             # ← Arduino sketch 目录，用 IDE 打开这个
│   ├── peristaltic_pump.ino      # setup / loop 组装
│   ├── version.h                 # FW_VERSION —— 版本号唯一来源（两处 hello 引用）
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
├── pump_cli.py                   # Python CLI 控制工具（串口 / HTTP 发 JSON 命令）
├── tools/make_ui_preview.py      # index.html + 模板 + 模拟后端 → 单文件 UI 预览
├── tools/ui_preview_template.html# 预览外壳（四个 {{...}} 占位符）
├── tools/ui_preview_sim.js       # 预览用的模拟后端（照固件语义手写，不参与构建）
├── tools/test_ui_preview_sim.js  # node 断言：模拟后端 vs 固件语义
├── xtoys_script.js               # XToys 集成脚本（已放弃，仅供参考）
├── xtoys_js_only.js
├── minimal_test/                 # 硬件最小验证 sketch
├── platformio.ini                # PlatformIO 构建入口（src_dir 指向上面的 sketch 目录）
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

### UI 预览（模拟后端，不上板点一遍）

`tools/make_ui_preview.py` 把 `index.html` 的 CSS / 页面结构 / 前端 JS **逐字**拼进
`tools/ui_preview_template.html`，再塞入 `tools/ui_preview_sim.js`（一个照固件语义手写的
模拟后端），产出一个可以双击打开的单文件 HTML：

```
python tools/make_ui_preview.py             # 默认写到 ~/Desktop/蠕动泵UI预览-DM542.html
python tools/make_ui_preview.py --out x.html
python tools/make_ui_preview.py --check     # 只检查预览是否落后于 index.html（过期退出码 1）
node   tools/test_ui_preview_sim.js         # 111 条断言：模拟后端与固件语义是否一致
```

预览顶部的倍速（1× / 5× / 20×）和命令日志面板是预览专用，固件 UI 里没有；命令日志会把
模拟后端的收发和固件的校验报错都打出来，方便确认「这条命令为什么被拒」。产物**不入库**，
用 `--check` 提醒重建。

> 模拟后端是手写的：改了固件的命令校验 / 校准流程 / 遥测字段，就要同步改
> `tools/ui_preview_sim.js`，并让 `test_ui_preview_sim.js` 继续通过。它会无头跑完整条链路
> （TIME 模式倒计时 → 暂停冻结 → 进校准 → 主面板 7 条命令被拒 → 校准跑 100 mL →
> 保存 → 放弃 → `menu_main`），逐字段断言日常设定没有被校准碰过。

### 命令行工具

`pump_cli.py` 通过串口或 HTTP 发送 JSON 命令，可用于无浏览器的调试。
注意它会打开串口，脚本里已设 `DTR=False` / `RTS=False` 防止 CH340 复位 ESP32。

---

## 5. 代码审查记录

两轮独立审查。**第一轮（2026-09-26，A / B / C / D 组）** 是对本分支的一次完整审查，三个 commit：

| commit | 内容 | 规模 |
|---|---|---|
| `fa4cbee` | 把全部源文件移入 `peristaltic_pump/` sketch 目录 | 27 files，纯 rename（numstat 全为 `0 0`） |
| `523a773` | 删除 BLE、堵转检测与全部死代码 | 22 files，+84 / −1472 |
| `b024a6e` | 修复 16 处缺陷 + STA/AP 两处 + 清理乱码 + 本 README | 17 files，+673 / −312 |

每个 commit 都单独编译验证过。完整的根因分析写在 `b024a6e` 的 commit message 里。

**第二轮（2026-11-04，N 组）** 是在 v2.4.1 基线（`.ino` 与 tag 逐字节相同，仅去 BOM）上的
复审：A1–A23 全部核对过、均已落地且描述准确，另发现 9 条档案之外的新问题，见本节末的
「N 组」表格。N1–N6 + N8 的修复落在 v2.4.2，N7 + N9 落在 v2.4.3。

### A 组：已修复的缺陷

| # | 问题 | 修法 |
|---|---|---|
| **A1** | **工程完全无法编译** —— `.ino` 已移入子目录而 24 个 `.cpp/.h` 仍在仓库根 | 全部移入 `peristaltic_pump/`（`fa4cbee`） |
| **A2** | 体积/时间模式一启动就误报堵转：`stallCheckTime` 初值 0 且从不重置，进 loop 时 `millis() ≥ 2400` 使 `millis()-0 > 1500` 恒真；而 `getCurrentPosition()` 返回 PCNT 实发脉冲数，`moveTo()` 后首帧仍为 0 | 整体删除堵转检测（DM542 无反馈能力，见 `523a773`） |
| **A3** | **低速时电机不动却上报「已分液 X mL」** | `applySpeed()` 统一入口 + `setSpeedInMilliHz()` + 检查返回值。完整机理见[下文](#a3-的完整机理最严重的一个) |
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

修法（见 `pump_core.cpp::applySpeed()`）：改用 `setSpeedInMilliHz()` 而非抬高流量下限，
这样 0.1 mL/min 的慢速分液仍然可用。有效区间已核对：库下限 5 millHz
（`1000 × TICKS_PER_S / 0xffffffff + 1`），本项目最低 `pps = 0.1 × 10 / 60 = 0.0167 Hz = 16.7 millHz`；
上限 `250 × TICKS_PER_S = 4e9` 不溢出 `uint32`，最高 `pps` 1.33 MHz → 1.33e9 millHz。
加速度下限钳到 1，两个调用的返回值都检查，失败时打印 `[PUMP] applySpeed rejected`。

### B 组：需产品决策，未擅自改动

| # | 问题 | 说明 |
|---|---|---|
| **B1** | **整个 HTTP API 零鉴权** | 同网段任何人都能启停泵、改校准；`POST /api/wifi` 能改网络配置。SoftAP 密码是全设备硬编码的 `12345678`（已收敛为 `WIFI_AP_PASSWORD` 宏，但值未变）。加鉴权会改变使用方式（AP 模式下手机要先连 WiFi），形式也需定（Basic Auth？固定 token？仅 AP 模式放开？） |
| **B2** | `/api/cmd` 拼 JSON 不转义不校验 | `cmd` / `v` / `s` / `m` / `i` 直接字符串拼接。实际危害有限（`parseAndExecute` 只 `strcmp` 已知命令，畸形 JSON 会解析失败），但属于未净化输入进 JSON 构造器。与 B1 一起决定 |
| **B3** | `stepperEnabled` 恒为 `true` | 删除堵转检测后没有任何地方会置 false（ENA 本来也「未接，方案D」）。仍被遥测与 `index.html` 的「使能/断电」显示消费，删除会改遥测 schema |
| **B4** | EEPROM 写放大 | 每 10 次分液 commit 一次全量 64 B；JET `interval=1s` 时约每 10 s 一次 NVS 写。24/7 运行的磨损需评估 |
| **B5** | `pump_cli.py` 两份完全相同的副本 | 根目录与 `tools/` 各一份（`fc /b` 逐字节一致）。**已于 v2.4.3 决定：留根目录、`git rm tools/pump_cli.py`**（README §4 的项目结构树与命令行工具一节都只提根目录那份，无需再改） |
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

### N 组：2026-11-04 复审新发现（v2.4.2）

对全仓库做的一轮独立复审，编号与上面的 A/B/C/D 并列。基线是 v2.4.1（`.ino` 与 tag
逐字节相同，仅去 BOM），A1–A23 全部核对过、**均已落地且描述准确**；下面是档案之外的新发现。

落地版本：**N1–N6 + N8 在 v2.4.2**（详细机理与量化见 §6 的 v2.4.2 条目）；
**N7 + N9 在 v2.4.2 之后**（见 §6 的 v2.4.3 条目）。9 条现已全部处理完，
剩下的开放项都在 B 组（产品决策）里。

| # | 问题 | 修法 / 现状 |
|---|---|---|
| **N1** | **定时模式系统性少出液，却按满额记管路寿命** —— 加速度是 `ACCEL_FACTOR × pps`（与速度成正比），升速段恒为 `1/ACCEL_FACTOR ≈ 3.33 s`，减速段等长；旧代码按匀速反算流量，电机要到 `targetTime+3.33 s` 才跑完，而截断在 `targetTime+1 s` 就触发，**永远抢在自然完成之前**。60 s 档 −1.36%、10 s 档 −8.17%、3 s 档 −27.0% | ✅ `startPump()` 改为反解总耗时正好等于 `targetTime` 的流量，梯形 / 三角形两支；截断路径改按实际出液量记账，读数前先等真停 |
| **N2** | **A3 只修了一半** —— `applySpeed()` 内部确实检查了库的返回值，但它自己返回的 `bool` 被全部 6 个调用方丢弃，`moveTo()` 的 `MoveResultCode` 更是 6 处全丢 ⇒ A3 描述的「一个脉冲不发却上报分液完成」仍然可达，差别只是多一行日志 | ✅ 六个函数改 `bool`，新增 `moveOrLog()` 收口，五条命令翻译成 `ok:false`，失败时不进 RUNNING |
| **N3** | `calibStopRun()` 在 `forceStop()` **之前**读 `getCurrentPosition()`，队列还会再走约 20 ms ⇒ `calibStepsRun` 偏小而量到的体积包含那段液量 ⇒ `spm` 系统性偏低、落盘后之后每次分液都少（校准流量 300/800/1600 mL/min 约 −1.0%/−2.7%/−5.3%）。A5 在 `pausePump()` 修过同一个坑，这里没修 | ✅ 改成 `forceStop()` → 轮询 `isRunning()` 到真停（上限 200 ms）→ 再读位置 |
| **N4** | `handleWebClients()` 只有一个「每收到数据就续 200 ms」的静默超时 ⇒ 一个每 <200 ms 吐 1 字节的客户端能把 `loop()` 无限期占住。`delay(1)` 会喂狗所以不是 WDT 复位而是**活锁**：状态机 / 蜂鸣器 / LED / EEPROM 落盘全停摆，而电机靠 MCPWM+PCNT 硬件继续发脉冲 | ✅ 加 `REQ_HARD_TIMEOUT_MS = 2000` 绝对上限，用 `millis()-start` 的写法免回绕 |
| **N5** | `(int32_t)(targetVolume × stepsPerMl)` 溢出：上界 `99999 × 50000 = 5e9 > INT32_MAX`，而超范围的 float→int32 是 **UB 不是回绕**。可达链条：`calib_measure` 只校验 `val > 0`，「0.1 mL / 3000 步」就能把 spm 标到 30000，再配 `targetVolume ≥ 71583` | ✅ `startPump()` / `calibStartRun()` 在动电机之前判界并拒绝。喷射与回吸经算不会越界，未加 |
| **N6** | `stop` / `reset` 里 `!pump.calibRunning` 是**死守卫**：`calibStopRun()` 已经把它清成 false，守卫在其后才求值，从校准分支过来时恒为真，与它自己的注释相反。今天实际危害为零，但意图与行为不符 | ✅ 先把标志存进 `const bool wasCalib` |
| **N7** | `prime_start` 不设 `pump.pumpStartMs`，而 `pause` 在 PRIME 期间是放行的（只校验 `state == RUNNING`）⇒ TIME 模式下 `pausePump()` 会用陈旧的 `pumpStartMs` 算 `pausedElapsedSec`，`resumePump()` 再据此回绕 `pumpStartMs`；`pausedRemainingSteps` 也会变成约 1e9（来自 `moveTo(999999999)`）。不影响出液量，但遥测 `elapsed`/`progress` 显示垃圾。另：排气速度写死 `flowRateToPPS(1500.0)`，忽略用户 `flowRate` | ✅ `prime_start` 补 `pump.pumpStartMs = millis()`。**没有**改成「排气期间拒绝 pause」—— UI 在 PRIME 时暂停键是亮的（`index.html` 只看 `d.state==RUNNING`、不看 menu），拒绝会让按钮亮着却报错。补上起始时刻后 pause/resume 在排气期间语义正确，而 `pausedRemainingSteps ≈ 1e9` 恰恰是想要的（resume 后继续无限跑）。排气速度写死 1500 属设计意图，未改（DM542 200 kHz 上限反推 spm ≤ 8000，现实标定 160–800 对应 4–20 kHz，够不着） |
| **N8** | 三处一致性：① `wifi_restart` 在 `parseAndExecute()` 里直接调 `restartWiFi()`，违反 `wifi_manager.h` 自己的契约，经 HTTP 下发时响应必丢（A10 只修了 `/api/wifi`）；② `okResponse`/`errResponse` 等 4 处 `snprintf(responseBuf,…)` 无 nullptr 保护，而 `buildTelemetryJson()` 有；③ `EEPROM_MAGIC`/`EEPROM_ADDR` 双份定义，`eeprom_store.h` 指向一个不存在的布局注释，offset 2 的 `stepsPerMl` 是永被覆盖的死字段 | ✅ ①改延迟执行（`requestWifiRestart()` + `loop()` 末尾 `serviceWifiRestart()`）②补 4 处保护 ③去重 + 补完整 0–282 偏移表 + 标注遗留字段 |
| **N9** | 两个诊断遗留物：① `serial_commands.cpp` 里 `while(!Serial && …<2000)` 与 `while(!hwUart && …<1000)` 是**死等** —— `ARDUINO_USB_CDC_ON_BOOT=0` 时 `Serial` 就是 `Serial0`（`HardwareSerial.h:449-451`），而 `HardwareSerial::operator bool()` = `uartIsDriverInstalled()`（`HardwareSerial.cpp:594-596`），`begin()` 之后恒真，两个循环第一次判断就退出。注释里的「初始化 USB CDC 串口」也与本构建不符；② `minimal_test/minimal_test.ino` 的头注释说「Serial (USB CDC) 输出到串口监视器」，同样问题 —— 下次拿它做开机诊断前必须先改，否则会误判成「代码没跑」 | ✅ ①两处**分别处理**：`Serial` 那处**保留**（CDC=0 下是死等，但若将来打开 `ARDUINO_USB_CDC_ON_BOOT`，`Serial` 变 HWCDC、`operator bool` 表示「主机已连接」，那时它就有用了）；`hwUart` 那处**删除**（它永远是 `HardwareSerial`，任何配置下都是纯死代码，连 `begin()` 失败都救不了）。两处都写明理由，免得被当成疏漏又加回来。`.h`/`.cpp` 里全部「USB CDC」措辞订正为 UART0 (GPIO43/44)。②头注释重写，并修掉其中**更危险的第二处错误**：原文说「GPIO 48 如果是普通 LED 会亮」，而本板 GPIO48 是 **WS2812 可寻址灯珠**（`pump_shared.h` 的 `LED_PIN` + `led.cpp` 用 Adafruit_NeoPixel），`digitalWrite(48, HIGH)` 点不亮 —— 排查开机循环时据此判断「代码没跑」会完全误判。现在明确以 GPIO5 + 万用表为最可靠路径 |

**复审时撤回的一条疑虑**：按 DM542 的接口语义（ENA 光耦导通 = 脱机、悬空 = 默认使能），
`peristaltic_pump.ino` 里 `digitalWrite(ENA_PIN, HIGH)` 配「ENA 始终使能」的注释在语义上可疑
（HIGH 究竟是使能还是脱机，取决于共阳/推挽接法与放大器是否反相）。但 B3 已写明
**ENA 未接（方案 D）**，GPIO18 的驱动对驱动器没有电气影响，故不作为缺陷。

原先记的两个残留项现已处理：源码里 3 处「ENA 始终使能 / 始终 HIGH」的误导性注释
（`pump_shared.h` 的 `ENA_PIN`、`peristaltic_pump.ino` 的 `digitalWrite`、
`pump_core.cpp` 的 `ensureStepperOn()`）已全部按「有驱动但未接线」重写，并把
**接线前必须先量什么**写进了 `.ino` 的注释里：用万用表确认「MCU 输出 HIGH 时 DM542 侧
光耦是否导通」，再决定该写 HIGH 还是 LOW，不得沿用旧措辞。
`ensureStepperOn()` 只置一个 flag 这件事本身仍属 B3（涉及遥测 schema，待产品决策）。

---

## 6. 更新日志

> 2026-09-26 之前的条目记录的是**当时**的代码状态。其中若干机制此后已被移除或改正
> （FreeRTOS 命令队列、`/api/info`、STA 30 s 超时、3 秒堵转检测、"RMT 迁移"），
> 阅读时请以 §2–§4 的当前描述为准。

### v2.4.3 (2026-11-05) — 复审收尾：N7 / N9 + ENA 注释订正 + 仓库清理

> v2.4.2 的收尾。§5「N 组」9 条到此全部处理完，剩下的开放项都在 B 组（产品决策）。

**N7 — `prime_start` 不设 `pumpStartMs`，排气期间 pause/resume 会算出垃圾**

- UI 在 PRIME 期间暂停键是**亮的**（`index.html` 的按钮逻辑只看 `d.state == 'RUNNING'`、
  不看 `menu`），而 `pause` 的守卫也只有 `state != RUNNING` ⇒ 排气时按暂停是合法操作。
  但 `prime_start` 从没设过 `pump.pumpStartMs`，于是 TIME 模式下 `pausePump()` 会拿
  上一次普通运行（或开机）留下的陈旧值算 `pausedElapsedSec`，`resumePump()` 再据此把
  `pumpStartMs` 回绕到接近 0；遥测 `elapsed` 全程显示的是开机以来的秒数
- 修法是补一行 `pump.pumpStartMs = millis()`，**不是**改成「排气期间拒绝 pause」——
  那会让一个亮着的按钮点了报错。补上起始时刻后 pause/resume 在排气期间语义正确，
  而 `pausedRemainingSteps ≈ 1e9`（来自 `moveTo(999999999)`）恰恰是想要的：
  resume 后继续无限跑，正是排气该有的行为，所以刻意不动它
- 排气速度写死 `flowRateToPPS(1500.0)`、忽略用户 `flowRate`，属设计意图，未改。
  顺带把边界算清并写进注释：DM542 手册 200 kHz 上限反推 `stepsPerMl ≤ 8000`，
  现实标定值 160–800 对应 4–20 kHz，够不着

**N9 — 两个诊断遗留物**

- `serial_commands.cpp` 里两处 `while (!Serial && … < 2000)` / `while (!hwUart && … < 1000)`
  在本构建下都是**死等**：`ARDUINO_USB_CDC_ON_BOOT=0` 时 core 3.3.11 的
  `HardwareSerial.h:449-451` 走 `#define Serial Serial0`，而
  `HardwareSerial::operator bool()` 的实现是 `uartIsDriverInstalled(_uart)`
  （`HardwareSerial.cpp:594-596`），`begin()` 之后恒为 true，循环第一次判断就退出
- **两处分别处理，不是一刀切**：
  - `Serial` 那处**保留** —— 若将来把 `ARDUINO_USB_CDC_ON_BOOT` 打开，`Serial` 变成 HWCDC、
    `operator bool` 表示「主机已连接」，这个最多 2 s 的等待就真的有用了（否则 hello 帧会在
    枚举完成前发出去直接丢掉）。删掉省 0 ms，代价是将来切 CDC 时静默丢 hello，不划算
  - `hwUart` 那处**删除** —— 它永远是 `HardwareSerial(1)`，任何配置下都不会变成 HWCDC，
    所以那段等待在本构建和任何未来构建里都是纯死代码，连 `begin()` 失败都救不了
    （真失败的话等 1 s 之后照样往死端口打 hello）。理由写进了注释，免得被当成疏漏又加回来
- `.h` / `.cpp` 里全部「USB CDC」措辞订正为 **UART0 (GPIO43 TX / 44 RX)**，并写明
  「插 USB 看不到输出，必须接 USB-TTL」。原先 `serial_commands.h` 还说
  `initSerialCommands()` 会「打印 READY 横幅」，实际打的是一帧含 `FW_VERSION` 的 hello JSON
- `minimal_test/minimal_test.ino` 头注释重写，并修掉其中**比 CDC 措辞更危险的第二处错误**：
  原文说「GPIO 48 如果是普通 LED 会亮」，而本板（ESP32-S3-DevKitC-1 N16R8）GPIO48 是
  **单颗 WS2812 可寻址灯珠**（`pump_shared.h` 的 `LED_PIN` + `led.cpp` 用 Adafruit_NeoPixel），
  `digitalWrite(48, HIGH)` 点不亮它，只会往数据线发一个恒高电平。这个文件是 2026-10-16
  排查开机循环时留下的诊断工具，**据此判断「代码没跑」会完全误判**。现在明确以
  GPIO5 + 万用表直流档为最可靠路径（不依赖串口、不依赖 USB 枚举、不依赖任何库），
  并刻意不给它引入 NeoPixel 依赖（那就不是「最简」测试了）

**ENA 三处误导注释订正**（§5「复审时撤回的一条疑虑」的收尾）

- `pump_shared.h` 的 `ENA_PIN`、`peristaltic_pump.ino` 的 `digitalWrite(ENA_PIN, HIGH)`、
  `pump_core.cpp` 的 `ensureStepperOn()` 原先都写「ENA 始终使能 / 始终 HIGH」。
  但 B3 已写明 **ENA 未接线（方案 D）**，GPIO18 的驱动对驱动器没有任何电气作用
- 按 DM542 的接口语义（ENA 光耦导通 = 脱机、悬空 = 默认使能），HIGH 究竟是使能还是脱机
  完全取决于共阳/推挽接法与 3.3V→5V 放大器是否反相 —— 没接线就无从谈起，旧措辞属误导
- 三处全部按「有驱动但未接线」重写，并把**接线前必须先量什么**写进 `.ino`：
  用万用表确认「MCU 输出 HIGH 时 DM542 侧光耦是否导通」，再决定该写 HIGH 还是 LOW。
  `ensureStepperOn()` 只置一个 flag 这件事本身仍属 B3（涉及遥测 schema，待产品决策）

**仓库清理**

- **B5 已决**：`git rm tools/pump_cli.py`，保留根目录那份（`fc /b` 确认两者逐字节相同）。
  README §4 的项目结构树删掉 `tools/pump_cli.py` 一行、根目录那行的「tools/ 下有一份
  完全相同的副本」注释也去掉；「命令行工具」一节本来就只写 `pump_cli.py`，无需改
- `peristaltic_pump/idf_component.yml` 加进 `.gitignore`（内容是 ESP-IDF 组件管理器生成的
  2 行 `idf: '>=5.1'`，每次 `pio run` 重新生成，不是源码）。此前它既未跟踪也未被忽略，
  `git status` 里会一直挂着一条 `??`
- `hardware/pcb/` 那 4 个未暂存删除**仍未处理**（用户自己的改动，`git restore hardware/pcb`
  可完全恢复，blob 还在 index 里）。本次提交按显式路径 add，没有用 `git add -u`

编译验证：`pio run`（FastAccelStepper 1.2.7）**SUCCESS** — Flash 1064052 B (33.8%)、
RAM 53076 B (16.2%)；`arduino-cli --warnings all`（FastAccelStepper 1.4.0）exitCode 0 —
Sketch 1043392 B (33%)、RAM 53076 B (16%)，**本项目代码零警告**（唯一一条仍是
`FastAccelStepper/src/pd_esp32/StepperISR_idf5_esp32_mcpwm_pcnt.cpp:354` 的
`gpio_iomux_in` 弃用提示）。比 v2.4.2 少 28 B（pio）—— 正是删掉 `hwUart` 那段死等的量。
`node tools/test_ui_preview_sim.js` 111 passed / 0 failed（本版无固件语义变更，断言数不变）。

**未上板验证**：本版除 N7 外全是注释 / 文档 / 仓库清理，无运动学或计量语义变更，
v2.4.2 那份清单继续适用。N7 单独一条：排气中按暂停再继续，遥测 `elapsed` 应显示
**本次排气的秒数**而不是开机以来的秒数，且 resume 后电机继续转。

### v2.4.2 (2026-11-05) — 定时模式斜坡补偿 + 计量正确性 + 健壮性

> 本版来自 2026-11-04 的一轮全仓库代码审查（新发现编号 N1–N9，与 §5 的 A/B/C/D 档案
> 并列）。只做了「计量正确性」与「健壮性」两组，产品决策类的条目（B1 无鉴权、
> B3 `stepperEnabled` 恒真、B5 重复的 `pump_cli.py`）仍未动。

**N1 — 定时模式系统性少出液，却按满额记管路寿命（本版最主要的修复）**

- 根因：`applyFlowSpeed()` 给的加速度是 `A = ACCEL_FACTOR × pps`，**与目标速度成正比**，
  于是升到全速的时间 `v/A = 1/ACCEL_FACTOR ≈ 3.33 s` 是个与速度无关的常量，减速段等长。
  而旧代码按 `activeFlowRate = targetVolume/(targetTime/60)` 反算流量，等于假设匀速跑满
  全程，电机实际要到 `targetTime + 3.33 s` 才跑完 —— `tick_running()` 的时间截断在
  `targetTime + 1 s` 就触发，**永远抢在自然完成之前**
- 修复：`startPump()` 改为反解「总耗时正好等于 `targetTime`」需要的匀速段流量，分两支
  - 梯形（到得了全速，`targetTime ≥ 2/ACCEL_FACTOR ≈ 6.67 s`）：`v = V/(T − 3.33)`
  - 三角形（到不了全速）：峰值 `vp = √(V·A)`、`T = 2√(V·t_ramp/v)` ⇒ `v = 4·V·t_ramp/T²`
  - 两支在 `T = 2·t_ramp` 处连续（都给 `v = V/t_ramp`）
- 截断路径（`pump_machine.cpp`）改为 `totalDispensed += 实际出液量`，不再 `+= targetVolume`。
  读数前先 `forceStop()` 再轮询 `isRunning()` 到真停，否则少算约 20 ms 行程
- 少出液量（用同步后的模拟后端运动模型算出，不是手算）：

  | targetTime | 修复前 | 修复后 |
  |---|---|---|
  | 60 s | −1.36% | 0% |
  | 30 s | −2.72% | 0% |
  | 10 s | −8.17% | 0% |
  | 5 s | −16.33% | 0% |
  | 3 s | −27.02% | 0% |
  | 500 mL / 10 s（需 4500 mL/min，被 1600 钳住） | 记 500，实出 248.9 | 记 **248.9** |

- **写错过一次，记在这里免得重犯**：第一版把 `2/ACCEL_FACTOR ≈ 6.67 s` 当成「三角形曲线的
  物理下限」，不可达档位就顶到 `V×60×ACCEL_FACTOR`。这是错的 —— 因为 `A` 与 `v` 成正比，
  提速的同时也提加速度，`T(v) = 2√(V·t_ramp/v)` 在三角形区**仍然单调递减，没有下限**。
  那个偏慢的速度让 3 s 档从 −27% 恶化到 −32%。真正的限制只有 `constrain` 的 1600 mL/min
- 同理，报告里最初给的少出液百分比（60 s 档 −2.8%）也**夸大了约 2 倍** —— 漏算了截断条件
  `pumpElapsed ≥ pumpDuration + 1` 里那 1 秒宽限，它把大部分斜坡亏空还回来了。
  正确系数是 `≈0.817/targetTime`，上表是修正后的值

**N2 — A3 只修了一半：`applySpeed()` / `moveTo()` 的返回值被所有调用方丢弃**

- §5 的 A3 说「两个调用的返回值都检查」，那指的是 `applySpeed()` **内部**对
  `setSpeedInMilliHz` / `setAcceleration` 的检查。但它自己返回的 `bool` 当时无人使用，
  `moveTo()` 的 `MoveResultCode` 更是 6 处全丢 ⇒ A3 描述的失效场景仍然可达：
  速度设失败 → 只打一行日志 → 照样 `moveTo()` → 一个脉冲不发 → `tick_running()` 看到
  `isRunning()==false` 判定跑完 → 上报「分液完成」并把 `targetVolume` 记进管路寿命
- 修复：`applyFlowSpeed` / `startPump` / `resumePump` / `startJetSquirt` / `startJetCycle` /
  `calibStartRun` 全部改 `bool`；新增 `moveOrLog()` 统一收口 6 处 `moveTo()`；
  `start` / `resume` / `jet_start` / `calib_start_run` / `prime_start` 五条命令把失败翻译成
  `ok:false`。失败时**不进 RUNNING**，`prime_start` 还会把 `currentMenu` 退回原值
- `startJetSquirt()` 只在真的动起来之后才置 `jetSquirting`，于是 `tick_running()` 的等待
  分支下一个 interval 会自动重试（喷射是周期动作，没有「命令发起方」可回报）
- 回吸：设速失败就不再进 `ANTI_DRIP` —— 否则 `tick_anti_drip()` 一看 `isRunning()==false`
  会立刻判定回吸完成，等于静默跳过这一步

**N3 — `calibStopRun()` 在 `forceStop()` 之前读位置，A5 修好的坑这里没修**

- 旧代码 `calibStepsRun = getCurrentPosition(); forceStop();` —— 队列里已规划的还会走约
  20 ms，于是 `calibStepsRun` 偏小，而用户量到的实际体积**包含**那 20 ms 的液量
  ⇒ `spm = 步数/体积` 系统性偏低，`calibSave()` 落盘后之后每次分液都少
- 改成 `forceStop()` → 轮询 `isRunning()` 到真停（上限 200 ms）→ 再读位置，与
  `pausePump()` 同一套。误差量级：校准流量 300/800/1600 mL/min 分别约 −1.0%/−2.7%/−5.3%
- 正常跑完那条路径本来就没问题（`calibFinishRun()` 由 `tick_running()` 在 `!isRunning()`
  时才调），**只有中途 `calib_stop_run` / `stop` / `menu_main` 会踩**

**N4 — `handleWebClients()` 的超时会被无限续期（slowloris 活锁）**

- 原来只有一个「每收到数据就续 200 ms」的静默超时。一个每 <200 ms 吐 1 字节的客户端可以把
  `loop()` 无限期占住：循环里的 `delay(1)` 会喂狗所以**不会** WDT 复位，而是活锁 ——
  `pump_machine_tick()` / `buzzer_tick()` / `led_tick()` / `saveParams()` 全部停摆，
  而电机靠 MCPWM+PCNT 硬件继续发脉冲，变成「泵在跑但没人看着」
- 加 `REQ_HARD_TIMEOUT_MS = 2000` 绝对上限（`REQ_IDLE_TIMEOUT_MS` 仍是 200）。
  用 `millis() - start < HARD` 而不是 `millis() < hardDeadline`，对 49.7 天的回绕免疫
- 合法请求 body 最大约 150 B，2 s 是十几个数量级的余量；`/api/scan` 那 8 s 阻塞发生在读完
  之后的 `handleRequest()` 里，不受影响

**N5 — `(int32_t)(float × float)` 溢出（UB，不是回绕）**

- `targetVolume ≤ 99999` 且 `stepsPerMl ≤ 50000` ⇒ 乘积最大 5e9 > `INT32_MAX`。
  够得着的路径：`calib_measure` 只校验 `val > 0`，所以「0.1 mL / 3000 步」就能把 spm 标到
  30000（落在 `calibCalculate()` 的 `[10, 50000]` 内），再配 `targetVolume ≥ 71583` 就越界
- `startPump()` 与 `calibStartRun()` 都在**动电机之前**判界并拒绝（`!(x <= INT32_MAX)` 的
  写法同时挡住 NaN 与 inf）。喷射（`10 × 50000`）与回吸（`5 × 50000`）经算不会越界，未加

**N6 — `stop` / `reset` 里 `!pump.calibRunning` 是死守卫，与它自己的注释相反**

- `calibStopRun()` 内部会把 `calibRunning` 清成 `false`，而那句守卫在它**之后**才求值，
  于是从校准分支走过来时恒为真，一次也拦不住。今天实际危害为零（`calibStopRun()` 已经
  `forceStop()` + 转 IDLE，后面的 `stopJetCycle()` 只是重复动作），但注释描述的意图与行为
  不符 ⇒ 先把标志存进 `const bool wasCalib`

**N8 — 三处一致性**

- `wifi_restart` 改为**延迟执行**：`requestWifiRestart()` 只置标志，`loop()` 末尾的
  `serviceWifiRestart()` 才真正重启（排在 `handleWebClients()` 之后，那时响应已经
  `flush()` + `stop()` 过）。原来在 `parseAndExecute()` 里直接调 `restartWiFi()`，
  违反了 `wifi_manager.h` 自己写的契约 —— 经 HTTP 下发时响应必然丢失。A10 只修了
  `/api/wifi` 那条路径。UI 不发这个命令（`index.html` 有注释说明），所以只影响手搓 URL / CLI
- `okResponse` / `errResponse` / JSON 解析失败 / `get_state` 四个 `snprintf(responseBuf, …)`
  站点补上 `nullptr` 保护。`buildTelemetryJson()` 早就有 `if (!telemetryBuf) return "{}"`，
  两边标准现在一致。用字符串常量而不是静态兜底缓冲（后者要白占 1536 B BSS）
- `EEPROM_MAGIC` / `EEPROM_ADDR` 在 `pump_shared.h` 与 `eeprom_store.cpp` 各定义一份
  （值相同所以合法不报警，但双份维护）⇒ 删掉后者。`eeprom_store.h` 声称「布局见
  `eeprom_store.cpp` 头部注释」而那里根本没有 ⇒ 补上完整的 0–282 偏移表，并写明
  offset 2 的 `stepsPerMl` 是**遗留字段**（`loadParams()` 末尾必被 `liquidSPM[currentLiquid]`
  覆盖，照写只是为了降级兼容）

**模拟后端与断言同步**：`node tools/test_ui_preview_sim.js` 92 → **111** 条

- `tools/ui_preview_sim.js` 原来是**理想积分器**（`dispensed += flow·dt`），根本没有斜坡，
  所以复现不出 N1。改为梯形 / 三角形运动模型（`rampDispensed()` / `runDurationSec()`，
  与固件公式同源），`finishRun(charged)` 现在区分「跑完全程记满额」与「被截断记实际量」
- 新增 I 组断言：I-0 回归护栏（把补偿公式改回 `V×60/T` 就立刻红）、I-1 梯形支、
  I-2 三角形支、I-3 钳位截断记账
- 修正 2 条基于旧理想模型的时序断言（校准 3 s 的出液量 40 → 18 mL；跑完 100 mL @
  800 mL/min 的等待 8 s → 11.5 s）

**文档**：§3 模式表的 TIME 行重写（原文「按 体积/时间 反算实际流量」已过期）；
§4 的断言数 80 → 111（那个 80 在 v2.4.1 时就已过期）。§5 的 A/B/C/D 档案与旧更新日志
一律未改 —— 它们是历史记录。

编译验证：`pio run`（FastAccelStepper 1.2.7）**SUCCESS** — Flash 1064080 B (33.8%)、
RAM 53076 B (16.2%)；`arduino-cli --warnings all`（FastAccelStepper 1.4.0）exitCode 0 —
Sketch 1043416 B (33%)、RAM 53076 B (16%)，**本项目代码零警告**（唯一一条来自
`FastAccelStepper/src/pd_esp32/StepperISR_idf5_esp32_mcpwm_pcnt.cpp:354` 的
`gpio_iomux_in` 弃用提示）。比 v2.4.1 的 1062908 B 多 1172 B。

**未上板验证**（本版只做了编译 + 模拟后端断言）：

- **N1 的核心前提**：FastAccelStepper 规划的减速段 = 加速段 = `ACCEL_FACTOR × pps`。
  这是从 `applySpeed()` 的实参和库的 `RampCalculator` 读出来的，没有实测。
  整套补偿公式都建立在这个对称性上；若实测不等，`startPump()` 两支公式的常数要重算，
  其余改动不受影响
- **N1 实出液量**：TIME 模式 60 s / 10 s / 3 s 各称重一次，期望偏差 <1%
  （改前分别 −1.36% / −8.17% / −27.0%）。不上天平的替代验证：盯遥测 `progress` 在
  `state` 转 `DONE` 那一刻的值，改前应恒 <100，改后应为 100
- **N3**：校准中途 `calib_stop_run` 后量体积算 spm，期望与连续跑完的校准一致
- **N4**：用 `nc` 每 150 ms 发 1 字节连到 `:80`，改后应在 2 s 内被断开、遥测继续刷新
- **N2 / N5** 的失败路径需要人为构造非法速度或离谱标定才能触发，正常档位走不到，
  只能确认日志文本
- `forceStop()` 后 20 ms 排空、200 ms 轮询上限是否够 —— 库文档值，未在 DM542 + 本机构上量过

### v2.4.1 (2026-09-27) — 管路寿命累计可清零

- 新增命令 **`reset_tube_life`**：把 `totalDispensed` 归零 + `markDirty()`。此前这个计数器
  **只增不减** —— 4 处写入全是 `+=`（`pump_machine.cpp` 的 JET / VOLUME / TIME / 回吸完成
  分支），`resetPump()` 不碰它，又落盘在 EEPROM offset 23，于是换完管子 `tubePct` 与
  LED 的 >80% 红灯告警永远消不掉，唯一的绕法是把 `tubeLifeML` 调大或设 0 关掉整个功能
- 刻意**不校验状态**：运行中清零也安全，本次分液结束时照常 `+= targetVolume`。
  `markDirty()` 只置标志，落盘仍由 `loop()` 在 `IDLE` / `DONE` 时统一做，不会在运行中写 flash
- `loadParams()` 补上 `totalDispensed` 的 `clampF()`（0–1e9，默认 0）。它是唯一漏网的落盘
  浮点，§2「加载时每个浮点都过 `clampF()`」在 v2.4.0 并不成立；半写产生的 NaN 会让
  `tubePct` 变成 `(int)NaN`（UB）、遥测打印 `nan`，且寿命告警永远判不出来
- Web UI 高级设置面板加「累计清零」按钮（`btn-sm` + 专属 toast「✓ 累计已清零」）；
  `pump_cli.py` 两份副本各加同名子命令，走已有的无参 `else` 分支
- 模拟后端与断言同步：`node tools/test_ui_preview_sim.js` 80 → **92** 条。新增 H 组覆盖
  「完成后累加 / 清零 / 阈值不动 / 日常设定不动 / 运行中清零本次仍计数」
- 版本号 `2.4.0` → `2.4.1`（不用 `v2.5.0`：那个号曾属于已删除的 tmc2226 线，避免混淆）

**未上板验证**（本版只做了编译 + 模拟后端断言）：高级设置面板点「累计清零」后，
「已用」应变 0 mL、「寿命」变 0%、LED 红色闪烁停止；断电重启后仍应是 0
（`markDirty()` 要等 `IDLE` / `DONE` 才落盘，正常点按后必然经过 `IDLE`）。

编译验证：固件 1051716 bytes (33%)、静态内存 52848 bytes (16%)、本项目代码零警告
（比 v2.4.0 多 112 bytes）。

### v2.4.0 (2026-09-27) — 校准向导独立参数 + 定时模式倒计时

> 本版本涵盖自 v2.3.8 (2026-07-30) 以来的全部改动，包括下面那条
> `2026-09-26 — 维护性审查`（当时没有单独打版本号）。

**校准不再碰日常设定：**

- 向导第 1 / 2 步设定的液体、体积、流量分别写 `calibLiquid` / `calibTargetVol` /
  `calibFlowRate`，新增 `calibSPM()`（= `liquidSPM[calibLiquid]`）供校准运行算步数；
  `calibEnter()` **不再**把 `pump.mode` 强制成 `VOLUME`，`calibLeave()` 也就不必还原模式
  （`savedMode` / `calibCtxSaved` 两个字段随之删除）
- 由此修掉一个会永久改模式的坑：`calib_save` → `calibSave()` 在「还原模式」之前就
  `markDirty()` + `saveParams()`，而 `mode` 是落盘字段（EEPROM offset 18）——
  校准保存一次，用户的 `TIME` / `JET` 模式就被写成 `VOLUME`
- `calib_select_liquid` 原先直接改 `pump.currentLiquid`（同样落盘，offset 59）且不同步
  `stepsPerMl`；现在只写 `calibLiquid`，提交推迟到 `calibSave()`，中途放弃则日常液体
  选择和它的 `stepsPerMl` 一字不变
- `calibStartRun()` 不覆写 `targetVolume`（会落盘，`calibSave()` 会把校准体积误存成用户的
  目标体积）；遥测 `progress` 与前端进度文本改为以 `calibTargetVol` 为分母
- `calibStartRun()` 同步 `activeFlowRate = calibFlowRate` 并补设 `pumpStartMs`：校准中途
  暂停再继续不会退回上次的流量，「已运行秒数」也不再是上次普通运行 / 开机以来的计时
- 校准运行与日常 `mode` 解耦：`pausePump()` / `resumePump()` 的 TIME 分支、`stop` / `reset`
  的 JET 分支都加 `!calibRunning` 条件 —— 用户日常是 TIME / JET 时校准也照体积式跑
- `calib_measure` 响应里的 `oldSPM` 改用 `calibSPM()`（原来报的是日常液体的值）
- `calib_set_vol` 改成**先校验体积和流量、再一起写入**：原来流量非法时虽然返回错误，
  `calibTargetVol` 却已经被改了一半

**校准期间锁定主面板：**

- `set_mode` / `set_flow` / `set_volume` / `set_time` / `set_liquid` / `start` /
  `prime_start` 在 `CALIBRATE` 菜单下返回错误（前四个结尾都会把 `currentMenu` 打回
  `MAIN`，等于悄悄退出向导）；`stop` / `reset` 转走 `calibStopRun()`，不再留下
  「电机已停但 `calibRunning` 仍为 true」的死状态；`menu_main` 同样统一走 `calibLeave()`
- Web UI：模式标签与液体按钮 `pointer-events:none`，全部日常参数输入框与主启动键
  `disabled`（`停止` / `暂停` 保留可用），第 1 / 2 步加提示说明这些设定不影响日常值，
  第 3 步显示本次校准的液体与流量
- 顺带修掉一处误导：`/api/scan` 在泵忙时返回 `{ok:false,error:"Pump busy",done:true}`，
  前端原来一律显示「未发现 WiFi 网络」，现在会把 `error` 显示出来

**定时模式倒计时：**

- Web UI 在 `TIME` 模式把进度显示成 `⏳ 剩余 m:ss`（超过一小时为 `h:mm:ss`），进度条按
  `elapsed / targetTime` 走，右侧目标值显示秒数；`DONE` 显示「已完成」。校准向导期间
  仍按校准体积显示百分比
- 遥测 `elapsed` 在 `PAUSED` 时报冻结的 `pumpElapsed`（原来报 0，一按暂停倒计时就跳回满值）

**接口：** `calib_set_vol` 新增可选 `flow` 参数（HTTP `&f=`、CLI `--flow`）；遥测新增
`calibFlow` / `calibLiquid`；`calib_set_vol` 与 `calib_select_liquid` 的成功响应回带设定值

**工具：** UI 预览的拼装脚本与模拟后端收进 `tools/`（`make_ui_preview.py` +
`ui_preview_template.html` + `ui_preview_sim.js` + `test_ui_preview_sim.js`）。
`--check` 判断桌面预览是否落后于 `index.html`，`node tools/test_ui_preview_sim.js`
用 80 条断言盯着模拟后端与固件语义的一致性（详见 §4）

**版本号：**

- 新增 `peristaltic_pump/version.h`（`FW_VERSION "2.4.0"`）作为**唯一来源**，两处 hello
  应答改成字符串拼接引用它。此前 hello 里的 `2.3.2` 自 v2.3.2 起就没跟着 README 声明的
  v2.3.8 走过，`pump_cli.py` 连上去会打印「设备: PeristalticPump v2.3.2」
- 发版流程写在 `version.h` 的注释里：改 `FW_VERSION` → 改 README 第 3 行 → 加 §6 条目
  → `git tag v<版本号>`

编译验证：固件 1051604 bytes (33%)、静态内存 52848 bytes (16%)、本项目代码零警告。

### 2026-09-26 — 维护性审查

修复构建阻塞（A1）、低速分液静默失败（A3）、启动空指针（A4）等 16 处缺陷；
删除 BLE、堵转检测与全部死代码（−1472 行）；清理两个文件的有损 mojibake；
README 全面重写。三个 commit：`fa4cbee` / `523a773` / `b024a6e`，
每个都单独编译验证，最终固件 1046196 bytes (33%)、内部 SRAM 静态 52840 bytes (16%)、
本项目代码零警告。详见 [§5](#5-代码审查记录)。

### v2.3.8 (2026-07-30)

- DM542 分支标记归档
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
