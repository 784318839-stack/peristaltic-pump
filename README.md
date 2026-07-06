# 🚧 蠕动泵控制器 — YZ1515 精密点液/喷射工作站　<sub>正在施工中</sub>

基于 ESP32-S3 的蠕动泵智能控制器，驱动 YZ1515 工业泵头，实现**体积模式、时间模式、喷射模式**三种精密流体控制。

> **v2.1** (2026-07-06) — Web UI 改用 HTTP 轮询（取代 WebSocket），修复压缩后 `//` 注释破坏 JS 的 bug。
> **v2.0** — 全面转向远程控制：WiFi Web UI + BLE UART + USB Serial，OLED/键盘已停用。

---

## 控制方式 (v2.0)

| 接口 | 说明 |
|---|---|
| **WiFi Web UI** | 手机/PC 浏览器直连，PWA 可添加到桌面 |
| **BLE UART** | Nordic UART Service，设备名 `PumpCtrl-XXXX` |
| **USB Serial** | 通过 USB-CDC 串口发送 JSON 命令 (115200bps) |
| **Hardware UART** | GPIO21=RX, 47=TX, 115200bps — USB-TTL 直连 PC，XToys 集成 |

四通道共用同一套 **JSON 命令协议**，可同时使用。

---

## WiFi 网络

- **默认模式**: SoftAP，手机/PC 直连，无需路由器
  - SSID: `PumpCtrl-XXXX`（XXXX = MAC 后 4 位）
  - IP: `192.168.4.1`
- **可选模式**: Station + SoftAP 回退
  - 可通过 Web UI 或 API 配置路由器 WiFi
  - Station 失败自动回退到 SoftAP
  - 配置保存在 EEPROM，掉电不丢失

---

## Web UI 功能

内嵌单页应用 (SPA)，手机端 PWA 支持：

- 📊 **实时仪表盘** — 状态、模式、进度、流量、体积、时间、温度
- 🎮 **运行控制** — 启动/暂停/恢复/停止
- ⚙️ **参数设置** — 流量、体积、时间、喷射参数
- 🔧 **模式切换** — 体积/时间/喷射模式一键切换
- 💧 **液体选择** — 4 种液体独立校准参数切换
- 📐 **校准向导** — 6 步引导式校准流程
- 💾 **方案预设** — 4 槽位加载/保存
- 🚿 **预灌快排** — 全速排空管路
- 📡 **WiFi 管理** — 扫描网络、配置连接
- 📈 **管路寿命** — 累计流量百分比显示

---

## 硬件配置

| 部件 | 型号 / 规格 |
|---|---|
| 主控 | ESP32-S3-WROOM-1-N16 (16MB Flash) |
| 泵头 | YZ1515 (工业级, 100×80×80mm) |
| 电机 | 42/57 步进电机 + 驱动器 |
| 蜂鸣器 | 无源蜂鸣器 GPIO5 |
| WS2812 LED | 状态指示灯 (待机绿/运行蓝/暂停琥珀/完成绿闪) |
| 显示屏 | SH1106 OLED 128×64 I2C *(v2.0 已停用)* |
| 键盘 | 4×4 矩阵键盘 *(v2.0 已停用)* |

---

## 功能清单 (17 项)

### 泵送模式

1. **体积模式** — 设定流量 (0.1–2000 mL/min) + 目标体积 (0.1–99999 mL)，恒速运行自动停止
2. **时间模式** — 设定体积 + 时间 (1–86400s)，自动计算流量，定时定量
3. **喷射模式** — 设定单次量 (0.1–10 mL) + 间隔 (1–60s) + 流量 + 压力 (1–10 级)，循环喷射
   - 双参数控制：流量管远近、压力管爆发力
   - 无回吸、柔和加速可选
   - 间隔 >15s 自动断电，提前 2s 通电节能

### 运行控制

4. **暂停/恢复** — 运行中暂停，从断点恢复继续
5. **防滴回吸** — 完成后反转吸回 (ANTI_DRIP 状态，不可打断)
6. **预灌/快排** — 全速 2000 mL/min 排空管路
7. **匀加速** — AccelStepper 缓启动/缓停止，无冲击

### 校准 & 液体

8. **校准向导** — 6 步引导（选液体 → 设体积 → 运行 → 读取量筒 → 计算结果 → 保存）
9. **4 种液体独立校准** — 水 / 粘稠 / 有机 / 自定义，每种独立 stepsPerMl

### 存储 & 预设

10. **方案预设存储** — 4 槽位，一键加载/保存整套运行参数
11. **EEPROM 掉电记忆** — 全部参数 + 4 液体校准 + 4 方案 + WiFi 配置

### 智能保护

12. **自动关使能** — 待机/暂停 5s 后步进电机自动断电，节能降热
13. **管路寿命追踪** — 累计流量统计，达到设定值提醒更换 (0=禁用)

### 反馈

14. **非阻塞蜂鸣器** — 7 种音效（按键/确认/取消/启动/暂停/完成/断电）
15. **WS2812 状态灯** — 颜色随泵状态变化，管路寿命告警红灯闪烁
16. **实时遥测** — `/api/status` 返回完整 JSON（状态/模式/参数/进度/WiFi 信息）

### 高级

17. **多客户端并发** — FreeRTOS 命令队列，WiFi + BLE + USB 三通道同时工作，线程安全

---

## XToys 集成

蠕动泵可通过硬件 UART 接入 [XToys](https://xtoys.app/) 平台，作为串口玩具区块被脚本/模式/挑逗控制。

### 物理连接

```
PC (USB) → USB-TTL 转接板 → ESP32
           TX ────────────→ GPIO 21 (RX)
           RX ────────────← GPIO 47 (TX)
           GND ──────────── GND
```

### 使用步骤

1. 用 USB-TTL 转接板连接 ESP32 和电脑
2. 在 XToys 中新建脚本，添加 **Serial 区块**（115200 / 8N1 / None）
3. 将 `蠕动泵_XToys脚本.js` 的内容粘贴到脚本编辑器
4. 启动脚本 — 自动连接并同步所有参数到泵

### XToys 脚本功能

| 类型 | 内容 |
|------|------|
| 控件 | 模式下拉框、液体下拉框、流量/体积/时间/喷射/回吸滑块 |
| 按钮 | 启动、暂停、停止、预灌、校准、加载方案 1-2、存入方案 1 |
| 显示 | 实时状态、已出 mL、进度%、累计 mL、管路寿命% |
| 自动化 | 滑块拖动即时同步参数、每秒轮询遥测、停止时自动停泵 |

> 脚本文件: `xtoys_script.js`

---

## JSON 命令协议

所有控制接口（WiFi / BLE / USB Serial）共用同一套协议。

### HTTP API

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/` | Web UI 页面 |
| GET | `/api/status` | 完整遥测 JSON |
| GET | `/api/cmd?c=<json>` | 发送命令 |
| POST | `/api/wifi` | 配置 WiFi 连接 |
| GET | `/api/scan` | 扫描附近 WiFi 网络 |

### 命令列表

| 命令 | 参数 | 说明 |
|---|---|---|
| `start` | — | 启动泵送/喷射循环 |
| `pause` | — | 暂停运行 |
| `resume` | — | 恢复运行 |
| `stop` / `reset` | — | 停止并复位 |
| `set_mode` | `mode`: "VOLUME"/"TIME"/"JET" | 切换泵送模式 |
| `set_liquid` | `index`: 0–3 | 切换液体 |
| `set_flow` | `value`: 0.1–2000 | 设定流量 (mL/min) |
| `set_volume` | `value`: 0.1–99999 | 设定目标体积 (mL) |
| `set_time` | `value`: 1–86400 | 设定目标时间 (s) |
| `set_jet_vol` | `value`: 0.1–10 | 设定单次喷射量 (mL) |
| `set_jet_interval` | `value`: 1–60 | 设定喷射间隔 (s) |
| `set_jet_flow` | `value`: 10–2000 | 设定喷射流量 (mL/min) |
| `set_jet_pressure` | `value`: 1–10 | 设定喷射压力等级 |
| `jet_start` / `jet_stop` | — | 喷射模式启停 |
| `set_anti_drip` | `value`: 0–5 | 设定回吸量 (mL) |
| `set_tube_life` | `value`: 0–200000 | 设定管路寿命 (mL) |
| `preset_load` / `preset_save` | `slot`: 0–3 | 方案预设加载/保存 |
| `calib_enter` → … → `calib_save` | (6 步流程) | 校准向导 |
| `prime_start` / `prime_stop` | — | 预灌快排 |
| `get_state` | — | 获取完整状态遥测 |
| `wifi_restart` | — | 重启 WiFi |
| `menu_main` | — | 返回主菜单 |

### 遥测响应示例

```json
{
  "type": "telemetry",
  "ts": 123456789,
  "state": "RUNNING",
  "mode": "VOLUME",
  "liquid": "Water",
  "flow": 150.0,
  "targetVol": 50.0,
  "dispensed": 23.45,
  "progress": 46,
  "elapsed": 9,
  "totalDispensed": 12340.5,
  "tubePct": 24,
  "stepsPerMl": 1860.5,
  "antiDripVol": 0.30,
  "stepperEnabled": true,
  "calibStep": 0,
  "wifiMode": "ap",
  "wifiIP": "192.168.4.1",
  "wifiClients": 1
}
```

---

## 状态机

```
State:  IDLE → RUNNING → PAUSED (⇄ RUNNING) → ANTI_DRIP → DONE
Mode:   MODE_VOLUME ⇄ MODE_TIME ⇄ MODE_JET
```

---

## 烧录配置 (Arduino IDE)

| 选项 | 值 |
|---|---|
| Board | **ESP32S3 Dev Module** |
| PSRAM | **Disabled** |
| USB CDC On Boot | **Disabled** |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| Flash Size | **16MB (128Mb)** |
| 串口波特率 | **115200** |

---

## 关键引脚

```
STEP: 16    DIR: 17    ENA: 18
Buzzer: 5
I2C SDA: 21  I2C SCL: 7    (OLED — v2.0 已停用)
Keypad Rows: 4, 5, 13, 42   (键盘 — v2.0 已停用)
Keypad Cols: 38, 39, 40, 47
UART1 RX: 21    TX: 47      (硬件串口 — XToys / PC 直连)
WS2812: 48                   (RGB 状态指示灯)
```

### 安全引脚 (已验证)

- ✅ STEP/DIR/ENA: GPIO 16/17/18
- ✅ Buzzer: GPIO 5
- ✅ I2C: GPIO 21(SDA)/7(SCL)
- ❌ 避开 Strapping: GPIO 0/3/45/46
- ❌ 避开 PSRAM 占用: GPIO 27/32/33/34/35/36/37
- ❌ 避开 JTAG: GPIO 14/15

---

## 项目结构

```
peristaltic_pump/
├── peristaltic_pump.ino      # 主程序
├── pump_shared.h             # 共享类型 & extern 声明
├── pump_core.h/cpp           # 泵控制核心 (启停/暂停/恢复/喷射/校准)
├── eeprom_store.h/cpp        # EEPROM 持久化存储
├── wifi_manager.h/cpp        # WiFi 管理 (SoftAP/Station + EEPROM)
├── bluetooth_manager.h/cpp   # BLE UART (NimBLE / Nordic UART Service)
├── web_handlers.h/cpp        # HTTP 服务 (路由 / 内嵌页面)
├── index.html                # Web UI 源文件 (编辑入口)
├── web_ui_gen.h              # Web UI 生成文件 (由 generate_web_ui.py 自动生成)
├── command_protocol.h/cpp    # JSON 命令协议 (解析/路由/遥测)
├── serial_commands.h/cpp     # USB 串口 + 硬件 UART 命令入口
├── buzzer.h/cpp              # 非阻塞蜂鸣器驱动
├── led.h/cpp                 # WS2812 状态指示灯
├── pump_chinese_font.h       # 中文字库 (SimHei 14px, 124 字 — OLED 已停用)
├── pump_cli.py               # Python CLI 控制工具
├── generate_web_ui.py        # Web UI 构建脚本: index.html → web_ui_gen.h
├── build_web_ui.py           # Web UI 备选构建脚本
├── xtoys_script.js           # XToys 平台集成脚本
├── strip_chinese.py          # 中文字符串提取脚本
├── strip_kb_oled.py          # 剥离键盘/OLED 代码脚本
└── README.md
```

---

## 依赖库

| 库 | 用途 |
|---|---|
| [AccelStepper](https://github.com/waspinator/AccelStepper) | 步进电机驱动 (匀加速) |
| [ArduinoJson](https://arduinojson.org/) (v7) | JSON 解析/序列化 |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | BLE UART |
| [U8g2](https://github.com/olikraus/u8g2) | OLED 图形库 *(v2.0 已停用)* |
| [Keypad](https://github.com/Chris--A/Keypad) | 矩阵键盘 *(v2.0 已停用)* |

---

## License

本项目 **禁止商用**。仅限个人学习、研究、非商业用途使用。未经作者许可，不得用于商业目的。

This project is **non-commercial**. Personal use, study, and research only. Commercial use requires explicit permission from the author.
