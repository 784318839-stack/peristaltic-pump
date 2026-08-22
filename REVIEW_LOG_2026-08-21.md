# Peristaltic Pump TMC2226 - 代码审查与修复记录

## 项目概况

- **硬件**: ESP32-S3-WROOM-1-N16R8 (16MB Flash + 8MB PSRAM)
- **驱动**: TMC2226 步进电机驱动, 单线 UART (GPIO15, 9600bps)
- **固件版本**: v2.5.0
- **编译配置**: ESP32 core 3.3.10, fqbn `esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=huge_app,FlashSize=16M,CPUFreq=160,USBMode=hwcdc`
- **编译结果**: Flash 1,059,152 bytes (33%), RAM 51,504 bytes (15%), 零错误零警告

---

## 发现并修复的 Bug (共 6 个)

### Bug 1 (严重) — calibRunning 未被 stop/reset 清除

**问题**: 用户在校准运行中发送 `stop` (而非 `calib_abort`), `calibRunning` 保持 true。下次正常泵送时, `tick_running()` 会进入校准分支, 导致行为异常。

**修复**: `pump_core.cpp` `resetPump()` 添加:
```cpp
if (pump.calibRunning && pump.calibSavedTargetVol > 0)
  pump.targetVolume = pump.calibSavedTargetVol;
pump.calibRunning = false;
```

---

### Bug 2 (严重) — HTTP 解析器无界 String 增长

**问题**: `web_handlers.cpp` `handleWebClients()` 逐字节读取 HTTP 请求到 `String request`, 无大小限制。恶意 Content-Length 可导致 OOM。

**修复**: 添加 4096 字节限制:
```cpp
char c = client.read();
if (request.length() < 4096) request += c;
```

---

### Bug 3 (中等) — TIME 模式计时偏差 (~1-2 秒)

**问题**: `pump_machine.cpp` 使用 `pumpElapsed` (整数秒, 截断 millis/1000) 与 `pumpDuration + 1` 比较。整数截断丢失最多 999ms, `+1` 再加 1 秒。

**修复**: 改为毫秒直接比较:
```cpp
if (pump.mode == MODE_TIME && (millis() - pump.pumpStartMs) >= pump.pumpDuration * 1000UL)
```

---

### Bug 4 (中等) — calibStartRun 覆盖 targetVolume

**问题**: `pump_core.cpp` `calibStartRun()` 执行 `pump.targetVolume = pump.calibTargetVol`, 破坏用户设置。如果校准被 `stop` 中断 (非 `calib_abort`), 原值丢失。

**修复**: 
1. `pump_state.h` 添加字段 `float calibSavedTargetVol = 0;`
2. `calibStartRun()` 保存: `calibSavedTargetVol = pump.targetVolume;`
3. `calibStopRun()`, `calibFinishRun()`, `resetPump()`, `calib_abort` 恢复

---

### Bug 5 (中等) — calib_abort 不处理 ANTI_DRIP 状态

**问题**: 校准完成后若 `antiDripVol > 0`, 状态机进入 ANTI_DRIP (防滴漏回抽)。此时 `calib_abort` 的条件 `if (pump.state == RUNNING)` 不匹配, 电机继续反转。

**修复**: `command_protocol.cpp` `calib_abort` 无条件调用 `stopPump()`:
```cpp
pump.calibRunning = false;
stopPump();  // 旧代码: if (pump.state == RUNNING) stopPump();
beepCancel();
```

---

### Bug 6 (低) — pausePump() 时间模式精度丢失

**问题**: `pump_core.cpp` `pausePump()` 保存已用时间为秒 (截断):
```cpp
pump.pausedElapsedSec = (millis() - pump.pumpStartMs) / 1000;  // 丢最多 999ms
```
恢复时乘回 1000, 每次暂停/恢复累积误差。

**修复**: 
1. `pump_state.h` 字段改名: `pausedElapsedSec` → `pausedElapsedMs`
2. `pausePump()`: `pausedElapsedMs = millis() - pump.pumpStartMs;` (毫秒)
3. `resumePump()`: `pump.pumpStartMs = millis() - pausedElapsedMs;` (直接减)

---

## 修改的文件清单

| 文件 | 修改内容 |
|------|----------|
| `pump_state.h` | 添加 `calibSavedTargetVol` 字段; `pausedElapsedSec` → `pausedElapsedMs` |
| `pump_core.cpp` | `resetPump()` 清除 calibRunning; `calibStartRun/StopRun/FinishRun()` 保存恢复 targetVolume; `pausePump/resumePump` 毫秒精度 |
| `pump_machine.cpp` | TIME 模式改为毫秒比较 |
| `command_protocol.cpp` | `calib_abort` 恢复 targetVolume + 无条件 stopPump() |
| `web_handlers.cpp` | HTTP 请求 4096 字节限制 |

---

## 全功能模拟验证

逐功能追踪所有执行路径, 确认无新引入 bug:

- 启动序列 (EEPROM 加载 / GPIO / WiFi / TMC2226)
- VOLUME 模式 (启动 → 运行 → 完成 → 防滴漏 → IDLE)
- TIME 模式 (暂停/恢复毫秒精度验证)
- JET 模式 (喷射 → 等待 → 循环 → 停止)
- 校准 8 步向导 (含 calib_abort ANTI_DRIP 场景)
- 预灌/快排
- StallGuard 堵转检测
- Web 控制 + PIN 校验
- WiFi 扫描 + 15s 缓存
- 遥测 (双缓冲区隔离)
- EEPROM 持久化 (NaN/Inf 防护 + dirty 延迟写)
- 自动断电 (5s 待机)

**所有路径验证通过。**

---

## 架构要点

### 状态机

```
IDLE → RUNNING → PAUSED ⇄ RUNNING
              → ANTI_DRIP → DONE → IDLE
              → STALL_ERROR (需 stop/reset 恢复)
```

### 三通道统一命令协议

- WiFi HTTP: `GET /api/cmd?c=xxx&v=yyy&p=pin`
- USB CDC Serial: JSON 换行分隔
- Hardware UART1: JSON 换行分隔

所有通道汇入 `parseAndExecute(json)`, 单线程 loop() 串行化, 无需互斥锁。

### 内存策略

- PSRAM 优先: `heap_caps_malloc(MALLOC_CAP_SPIRAM)` 用于响应/遥测缓冲区
- 回退内部 RAM: `malloc()` 作为 fallback
- 遥测 1024B + 响应 1536B, 双缓冲区独立

### EEPROM 布局 (512B)

- 0-63: 泵参数 (magic + stepsPerMl + flowRate + ... + liquidSPM[4])
- 100-108: 网络控制 PIN (magic + 8 字符)
- 184-282: WiFi 配置 (SSID + 密码 XOR 加密 + 模式)

### 安全设计

- 上电 ENA=HIGH (电机禁用), 首次启动才使能
- HTTP 请求 4096 字节限制
- PIN 校验 (GET 参数 / POST JSON body)
- WiFi 密码 XOR 加密存储 (设备 MAC 为密钥)
- EEPROM NaN/Inf 防护
- StallGuard 仅 StealthChop 区间检测 (高速跳过, 防误报)

---

## 编译命令

```bash
cd /c/Users/xg821/Desktop/peristaltic_pump_TMC2226
/c/Users/xg821/AppData/Local/arduino-cli.exe compile \
  --fqbn esp32:esp32:esp32s3:PSRAM=opi,PartitionScheme=huge_app,FlashSize=16M,CPUFreq=160,USBMode=hwcdc \
  --warnings default
```

---

## 日期

2026-08-21
