# ✅ 蠕动泵控制器 — YZ1515 精密点液/喷射工作站

基于 ESP32-S3 的蠕动泵智能控制器，驱动 YZ1515 工业泵头，实现**体积模式、时间模式、喷射模式**三种精密流体控制。

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
| **BLE UART** | Nordic UART Service，设备名 `PumpCtrl-XXXX` |
| **USB Serial** | 通过 USB-CDC 串口发送 JSON 命令 (115200bps) |
| **Hardware UART** | GPIO21=RX, 47=TX, 115200bps — USB-TTL 直连 PC，XToys 集成 |

四通道共用同一套 **JSON 命令协议**，可同时使用。

---

## WiFi 网络

- **双模启动**: WIFI_AP_STA 同时运行，无需模式切换
  - SoftAP 始终可用: `PumpCtrl-XXXX`（密码 12345678）
  - IP: `http://192.168.4.1`（直连）或 `http://pump.local`（mDNS）
- **STA 连接家里 WiFi**: Web UI → WiFi 设置 → 输入 SSID/密码 → 保存
  - 连接成功后页面弹出 `✅ WiFi 已连接` 提示
  - header 实时显示可访问地址 `http://STA_IP | http://pump.local`
  - STA 失败不影响 SoftAP，30s 超时自动放弃
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
- 🛡️ **堵转保护** — 步进电机 3 秒位置不变自动停机报警 (STALL_ERROR)
- 🔐 **密码加密** — WiFi 密码 XOR 加密存储 (设备 MAC 密钥，拆机读 EEPROM 是乱码)

---

## 硬件配置

| 部件 | 型号 / 规格 |
|---|---|
| 主控 | ESP32-S3-WROOM-1-**N16R8** (16MB Flash + **8MB Octal PSRAM**) |
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

8. **校准向导** — 5 步引导（选液体 → 设体积 → 运行 → 读取量筒 → 计算结果并保存）
9. **4 种液体独立校准** — 水 / 粘稠 / 液体1 / 液体2，每种独立 stepsPerMl

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
| GET | `/api/scan` | 扫描附近 WiFi 网络 (同步, ~1s) |
| GET | `/api/info` | 网络状态 (IP/模式/MAC/mDNS) |

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
| `calib_enter` → … → `calib_save` | (5 步流程) | 校准向导 |
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
  "wifiClients": 1,
  "psramFree": 7936,
  "psramTotal": 8192,
  "heapFree": 265,
  "heapTotal": 320
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
| PSRAM | **OPI PSRAM** (Octal SPI, N16R8) |
| USB CDC On Boot | **Disabled** |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| Flash Size | **16MB (128Mb)** |
| CPU Frequency | **160 MHz** |
| 串口波特率 | **115200** |

### 内存布局

| 存储器 | 容量 | 用途 |
|--------|------|------|
| Flash | 16 MB | 固件 (1.2MB) + SPIFFS |
| 内部 SRAM | ~320 KB | WiFi/BLE 协议栈、FreeRTOS 任务栈、关键数据 |
| **PSRAM** | **8 MB** | 遥测/命令/串口缓冲区、FreeRTOS 队列、大 JSON 解析 |

静态缓冲区（`telemetryBuf`/`responseBuf`/`serialBuffer`/`hwUartBuf`）已全部移到 PSRAM heap，内部 SRAM 从 58.3KB 降至 56.0KB。

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
├── peristaltic_pump.ino      # 主程序 (setup/loop 组装)
├── pump_state.h/cpp           # PumpState 结构体 — 所有运行状态集中管理
├── pump_machine.h/cpp         # 泵状态机 — transition() + per-state tick
├── pump_shared.h              # 枚举 / 常量 / extern 声明
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
├── pump_cli.py               # Python CLI 控制工具
├── generate_web_ui.py        # Web UI 构建脚本: index.html → web_ui_gen.h
├── build_web_ui.py           # Web UI 备选构建脚本
├── xtoys_script.js           # XToys 平台集成脚本
└── README.md
```

---

## 依赖库

| 库 | 用途 |
|---|---|
| [FastAccelStepper](https://github.com/gin66/FastAccelStepper) | 步进电机驱动 (RMT 硬件加速, 匀加速) |
| [ArduinoJson](https://arduinojson.org/) (v7) | JSON 解析/序列化 |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | BLE UART |
| [U8g2](https://github.com/olikraus/u8g2) | OLED 图形库 *(v2.0 已停用)* |
| [Keypad](https://github.com/Chris--A/Keypad) | 矩阵键盘 *(v2.0 已停用)* |

---

## 更新日志

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

✅ **软件开发完成** (v2.3.6, 2026-07-21)

所有已知问题已解决：

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
