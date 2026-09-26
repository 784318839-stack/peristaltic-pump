// eeprom_store.cpp - EEPROM layout, save/load
#include "eeprom_store.h"
#include "pump_shared.h"
#include "pump_state.h"
#include <EEPROM.h>

#define EEPROM_MAGIC  0x5061  // v4.2: 400 pulse/rev 细分 (revert from 1600)
#define EEPROM_ADDR   0

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

