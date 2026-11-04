/******************************************************************************
 * eeprom_store.cpp - EEPROM 布局 / 存 / 取
 *
 * EEPROM.begin(512) (见 peristaltic_pump.ino)。ESP32 的 EEPROM 库是 NVS 上的一整块
 * 512 B 镜像: put/write 只改 RAM 镜像, commit() 才整块落盘 —— 所以 saveParams() 与
 * saveWiFiConfig() 虽然各管一段, 落盘是同一块, 不会互相覆盖。
 *
 * 布局 (偏移 = EEPROM_ADDR + n, EEPROM_ADDR 为 0):
 *
 *   泵参数 —— 本文件, 共 64 B
 *     0    2  uint16   EEPROM_MAGIC (0x5061, 定义在 pump_shared.h)
 *     2    4  float    stepsPerMl        ← 遗留字段, 见下方说明
 *     6    4  float    flowRate
 *    10    4  float    targetVolume
 *    14    4  float    targetTime
 *    18    1  uint8    mode (PumpMode)
 *    19    4  float    antiDripVol
 *    23    4  float    totalDispensed
 *    27    4  float    tubeLifeML
 *    31    4  float    jetVolume
 *    35    4  float    jetInterval
 *    39    4  float    jetFlowRate
 *    43   16  float[4] liquidSPM[NUM_LIQUIDS]   (43 / 47 / 51 / 55)
 *    59    1  uint8    currentLiquid
 *    60    4  float    jetPressure
 *    64  ---           泵参数到此结束
 *
 *   WiFi 配置 —— wifi_manager.cpp, 共 99 B (WIFI_EEPROM_BASE = 184)
 *   184    2  uint16   WIFI_EEPROM_MAGIC (0x5746 "WF")
 *   186   32  char[]   ssid   (明文)
 *   218   64  char[]   pass   (按本机 MAC 做 XOR; 是混淆不是加密, 见 README 审查档案 B7)
 *   282    1  uint8    mode   (0=仅AP / 1=STA+AP回退)
 *   283  ---           WiFi 到此结束
 *
 *   64–183 与 283–511 是未用空洞。改布局前先确认没有跨段重叠。
 *
 * offset 2 的 stepsPerMl 是**遗留字段**: loadParams() 末尾一定会用
 * pump.stepsPerMl = pump.liquidSPM[pump.currentLiquid] 覆盖它, 所以读出来的值永远用不上。
 * 仍然照写, 是为了让降级回旧固件 (那时还没有 liquidSPM[]) 的板子能读到正确的 spm。
 * 不要拿它当「当前 spm」的权威来源。
 ******************************************************************************/
#include "eeprom_store.h"
#include "pump_shared.h"
#include "pump_state.h"
#include <EEPROM.h>

// EEPROM_MAGIC / EEPROM_ADDR 由 pump_shared.h 统一提供, 这里不再重复定义
// (原先两处各写一份, 值相同所以合法不报警, 但是双份维护)

void markDirty() { pump.eepromDirty = true; }

// constrain() 是宏 ((amt)<(low)?(low):((amt)>(high)?(high):(amt))), 两个比较对 NaN
// 都为假 → 原样返回 NaN。而 EEPROM 半写 (magic 有效但某个 float 还是擦除态
// 0xFFFFFFFF) 恰好就是 NaN, 之后 (int32_t)NaN 是未定义行为。所以先过 isfinite,
// 不合法就回退到与 PumpState 初值一致的默认值。
static float clampF(float v, float lo, float hi, float def) {
  return isfinite(v) ? constrain(v, lo, hi) : def;
}

void saveParams() {
  if (!pump.eepromDirty) return;
  EEPROM.put(EEPROM_ADDR,     (uint16_t)EEPROM_MAGIC);
  EEPROM.put(EEPROM_ADDR + 2, pump.stepsPerMl);
  EEPROM.put(EEPROM_ADDR + 6, pump.flowRate);
  EEPROM.put(EEPROM_ADDR + 10, pump.targetVolume);
  EEPROM.put(EEPROM_ADDR + 14, pump.targetTime);
  EEPROM.put(EEPROM_ADDR + 18, (uint8_t)pump.mode);
  EEPROM.put(EEPROM_ADDR + 19, pump.antiDripVol);
  EEPROM.put(EEPROM_ADDR + 23, pump.totalDispensed);
  EEPROM.put(EEPROM_ADDR + 27, pump.tubeLifeML);
  EEPROM.put(EEPROM_ADDR + 31, pump.jetVolume);
  EEPROM.put(EEPROM_ADDR + 35, pump.jetInterval);
  EEPROM.put(EEPROM_ADDR + 39, pump.jetFlowRate);
  for (int i = 0; i < NUM_LIQUIDS; i++)
    EEPROM.put(EEPROM_ADDR + 43 + i * 4, pump.liquidSPM[i]);
  EEPROM.put(EEPROM_ADDR + 59, (uint8_t)pump.currentLiquid);
  EEPROM.put(EEPROM_ADDR + 60, pump.jetPressure);
  EEPROM.commit();
  pump.eepromDirty = false;
}

bool loadParams() {
  uint16_t magic;
  EEPROM.get(EEPROM_ADDR, magic);
  if (magic != EEPROM_MAGIC) return false;

  EEPROM.get(EEPROM_ADDR + 2,  pump.stepsPerMl);
  EEPROM.get(EEPROM_ADDR + 6,  pump.flowRate);
  EEPROM.get(EEPROM_ADDR + 10, pump.targetVolume);
  EEPROM.get(EEPROM_ADDR + 14, pump.targetTime);
  pump.mode = (PumpMode)EEPROM.read(EEPROM_ADDR + 18);
  if (pump.mode > MODE_JET) pump.mode = MODE_VOLUME;
  EEPROM.get(EEPROM_ADDR + 19, pump.antiDripVol);
  EEPROM.get(EEPROM_ADDR + 23, pump.totalDispensed);
  EEPROM.get(EEPROM_ADDR + 27, pump.tubeLifeML);
  EEPROM.get(EEPROM_ADDR + 31, pump.jetVolume);
  EEPROM.get(EEPROM_ADDR + 35, pump.jetInterval);
  EEPROM.get(EEPROM_ADDR + 39, pump.jetFlowRate);
  for (int i = 0; i < NUM_LIQUIDS; i++)
    EEPROM.get(EEPROM_ADDR + 43 + i * 4, pump.liquidSPM[i]);
  pump.currentLiquid = EEPROM.read(EEPROM_ADDR + 59);
  if (pump.currentLiquid >= NUM_LIQUIDS) pump.currentLiquid = 0;
  EEPROM.get(EEPROM_ADDR + 60, pump.jetPressure);

  pump.stepsPerMl    = clampF(pump.stepsPerMl,    10, 50000,  250.0);
  pump.flowRate      = clampF(pump.flowRate,      0.1, 1600.0, 50.0);
  pump.targetVolume  = clampF(pump.targetVolume,  0.1, 99999,  10.0);
  pump.targetTime    = clampF(pump.targetTime,    1, 86400,    30.0);
  pump.antiDripVol   = clampF(pump.antiDripVol,   0, 5.0,       0.05);
  // totalDispensed 原先是唯一没过 clampF 的落盘浮点 ( README §2 的「每个浮点都过
  // clampF」因此并不成立 )。它在 EEPROM offset 23, 半写产生的 NaN 会让 tubePct
  // 变成 (int)NaN ( UB ) 、遥测打印 nan, 且 LED 的寿命告警永远判不出来。
  // 上限取 1e9: float32 在这个量级已分辨不出单次分液, 再大的累计值没有意义。
  pump.totalDispensed = clampF(pump.totalDispensed, 0, 1e9, 0);
  pump.tubeLifeML    = clampF(pump.tubeLifeML,    0, 200000,  50000);
  pump.jetVolume     = clampF(pump.jetVolume,     0.1, 10.0,    1.0);
  pump.jetInterval   = clampF(pump.jetInterval,   1, 60,        3.0);
  pump.jetFlowRate   = clampF(pump.jetFlowRate,   10, 1600.0, 200.0);
  pump.jetPressure   = clampF(pump.jetPressure,   1, 10,        5.0);
  for (int i = 0; i < NUM_LIQUIDS; i++)
    pump.liquidSPM[i] = clampF(pump.liquidSPM[i], 10, 50000, 250.0);
  pump.stepsPerMl = pump.liquidSPM[pump.currentLiquid];
  return true;
}

