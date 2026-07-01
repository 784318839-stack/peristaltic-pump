# 蠕动泵控制器 — YZ1515 精密点液/喷射工作站

基于 ESP32-S3 的蠕动泵智能控制器，驱动 YZ1515 工业泵头，实现体积模式、时间模式、喷射模式三种精密流体控制。

## 硬件配置

| 部件 | 型号 / 规格 |
|---|---|
| 主控 | ESP32-S3-WROOM-1-N16 (16MB Flash) |
| 泵头 | YZ1515 (工业级) |
| 显示屏 | SH1106 OLED 128×64 I2C (GPIO21/22) |
| 键盘 | 4×4 矩阵键盘 |
| 电机 | 42/57 步进电机 + 驱动器 (GPIO16/17/18) |
| 蜂鸣器 | 有源蜂鸣器 GPIO1 |

## 功能清单

1. **体积模式** — 设定流量 + 目标体积，恒速运行自动停止
2. **时间模式** — 设定体积 + 时间，自动计算流量，定时定量
3. **喷射模式** — 设定单次量 + 间隔 + 流量 + 压力，循环喷射
   - 双参数控制：流量管远近、压力管爆发力
   - 无回吸、柔和加速可调
   - 间隔 >15s 自动断电，提前 2s 通电
4. **暂停/恢复** — 运行中暂停，从断点继续
5. **防滴回吸** — 完成后反转吸回（ANTI_DRIP 状态，不可打断）
6. **校准向导** — 5 步引导（选液体 → 设体积 → 运行 → 读量筒 → 结果）
7. **4 种液体独立校准** — 水/粘稠/有机/自定义
8. **方案预设存储** — 4 槽位，一键加载/保存整套参数
9. **高级设置** — 回吸量/管路寿命/选液体
10. **预灌/快排** — 全速 2000 mL/min 排空
11. **匀加速** — AccelStepper 缓启动/缓停止
12. **自动关使能** — 待机 5s 后断电
13. **OLED 屏保** — 2 分钟无操作休眠
14. **蜂鸣器** — 7 种音效反馈
15. **EEPROM 掉电记忆** — 全部参数 + 4 液体校准 + 4 方案 + WiFi

## 状态机

```
State:  IDLE → RUNNING → PAUSED (⇄ RUNNING) → ANTI_DRIP → DONE
Mode:   MODE_VOLUME ⇄ MODE_TIME ⇄ MODE_JET
```

## 按键操作

| 键 | 主菜单 |
|---|---|
| A | 设流量/体积/单次量 |
| B | 设体积/时间/间隔 |
| C | 启停/暂停/恢复 |
| D | 切换模式 → 选液体 |
| * | 复位 |
| # | 校准/喷射设置 |
| 0 | 预灌 |
| 1-4 | 方案预设 |

## 烧录配置 (Arduino IDE)

- Board: **ESP32S3 Dev Module**
- PSRAM: **Disabled**
- USB CDC On Boot: **Disabled**
- Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**
- 串口波特率: 115200

## 关键引脚

```
STEP: 16    DIR: 17    ENA: 18
I2C SDA: 21  I2C SCL: 22
Buzzer: 1
Keypad Rows: 4, 5, 13, 42
Keypad Cols: 38, 39, 40, 47
```

## 安全引脚（已验证）

- STEP/DIR/ENA: GPIO 16/17/18 ✓
- Buzzer: GPIO 1 ✓
- I2C: GPIO 21(SDA)/22(SCL) ✓
- 避开 Strapping 引脚: GPIO 0/3/45/46
- 避开 PSRAM 占用: GPIO 27/32/33/34/35/36/37
- 避开 JTAG: GPIO 14/15

## 依赖库

- [U8g2](https://github.com/olikraus/u8g2) — OLED 图形库
- [Keypad](https://github.com/Chris--A/Keypad) — 矩阵键盘
- [AccelStepper](https://github.com/waspinator/AccelStepper) — 步进电机驱动

## License

本项目 **禁止商用**。仅限个人学习、研究、非商业用途使用。未经作者许可，不得用于商业目的。

This project is **non-commercial**. Personal use, study, and research only. Commercial use requires explicit permission from the author.
