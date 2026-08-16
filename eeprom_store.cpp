// eeprom_store.cpp - EEPROM layout, save/load
// 布局 (512B): 0-63 参数 | 100-108 网络控制 PIN | 184-282 WiFi 配置
#include "eeprom_store.h"
#include "pump_shared.h"
#include "pump_state.h"
#include <EEPROM.h>

#define EEPROM_MAGIC  0x5062  // v2.4.0: TMC2226 16 细分 (3200 pulse/rev)
#define EEPROM_ADDR   0

// NaN/Inf 校验: Flash 位翻转可能产生非法浮点值, 放行会导致未定义行为
static float sanitizeFloat(float v, float lo, float hi, float def) {
  return (isnan(v) || isinf(v)) ? def : constrain(v, lo, hi);
}

void markDirty() { pump.eepromDirty = true; }

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

  pump.stepsPerMl    = sanitizeFloat(pump.stepsPerMl,    10, 50000, 2000.0);
  pump.flowRate      = sanitizeFloat(pump.flowRate,      0.1, 1600.0, 50.0);
  pump.targetVolume  = sanitizeFloat(pump.targetVolume,  0.1, 99999, 10.0);
  pump.targetTime    = sanitizeFloat(pump.targetTime,    1, 86400, 30.0);
  pump.antiDripVol   = sanitizeFloat(pump.antiDripVol,   0, 5.0, 0.05);
  pump.tubeLifeML    = sanitizeFloat(pump.tubeLifeML,    0, 200000, 50000.0);
  pump.jetVolume     = sanitizeFloat(pump.jetVolume,     0.1, 10.0, 1.0);
  pump.jetInterval   = sanitizeFloat(pump.jetInterval,   1, 60, 3.0);
  pump.jetFlowRate   = sanitizeFloat(pump.jetFlowRate,   10, 1600.0, 200.0);
  pump.jetPressure   = sanitizeFloat(pump.jetPressure,   1, 10, 5.0);
  pump.totalDispensed = sanitizeFloat(pump.totalDispensed, 0, 100000000, 0);
  for (int i = 0; i < NUM_LIQUIDS; i++)
    pump.liquidSPM[i] = sanitizeFloat(pump.liquidSPM[i], 10, 50000, 2000.0);
  pump.stepsPerMl = pump.liquidSPM[pump.currentLiquid];
  return true;
}

// ============================================================================
//                        网络控制 PIN (100-108)
// ============================================================================
bool loadPin(char* buf, size_t len) {
  if (!buf || len < PIN_MAX_LEN + 1) return false;
  if (EEPROM.read(PIN_EEPROM_BASE) != PIN_EEPROM_MAGIC) { buf[0] = '\0'; return false; }
  for (int i = 0; i < PIN_MAX_LEN; i++)
    buf[i] = (char)EEPROM.read(PIN_EEPROM_BASE + 1 + i);
  buf[PIN_MAX_LEN] = '\0';
  return buf[0] != '\0';
}

void savePin(const char* pin) {
  if (!pin || strlen(pin) == 0) { clearPin(); return; }
  size_t n = min(strlen(pin), (size_t)PIN_MAX_LEN);
  EEPROM.write(PIN_EEPROM_BASE, PIN_EEPROM_MAGIC);
  for (size_t i = 0; i < PIN_MAX_LEN; i++)
    EEPROM.write(PIN_EEPROM_BASE + 1 + i, (i < n) ? pin[i] : 0);
  EEPROM.commit();
}

void clearPin() {
  EEPROM.write(PIN_EEPROM_BASE, 0);
  for (int i = 0; i < PIN_MAX_LEN; i++)
    EEPROM.write(PIN_EEPROM_BASE + 1 + i, 0);
  EEPROM.commit();
}

