// eeprom_store.cpp - EEPROM layout, save/load, presets
#include "eeprom_store.h"
#include "pump_shared.h"
#include "pump_state.h"
#include <EEPROM.h>

#define EEPROM_MAGIC  0x5061  // v4.2: 400 pulse/rev 细分 (revert from 1600)
#define EEPROM_ADDR   0
#define PRESET_BASE   64
#define PRESET_SIZE   30

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

  pump.stepsPerMl    = constrain(pump.stepsPerMl,    10, 50000);
  pump.flowRate      = constrain(pump.flowRate,      0.1, 1600.0);
  pump.targetVolume  = constrain(pump.targetVolume,  0.1, 99999);
  pump.targetTime    = constrain(pump.targetTime,    1, 86400);
  pump.antiDripVol   = constrain(pump.antiDripVol,   0, 5.0);
  pump.tubeLifeML    = constrain(pump.tubeLifeML,    0, 200000);
  pump.jetVolume     = constrain(pump.jetVolume,     0.1, 10.0);
  pump.jetInterval   = constrain(pump.jetInterval,   1, 60);
  pump.jetFlowRate   = constrain(pump.jetFlowRate,   10, 1600.0);
  pump.jetPressure   = constrain(pump.jetPressure,   1, 10);
  for (int i = 0; i < NUM_LIQUIDS; i++)
    pump.liquidSPM[i] = constrain(pump.liquidSPM[i], 10, 50000);
  pump.stepsPerMl = pump.liquidSPM[pump.currentLiquid];
  return true;
}

bool isPresetValid(int slot) {
  if (slot < 0 || slot > 3) return false;
  uint8_t mode = EEPROM.read(PRESET_BASE + slot * PRESET_SIZE);
  return ((mode & 0x7F) <= 2);  // v1: 0-2, v2: 0x80-0x82
}

void savePreset(int slot) {
  if (slot < 0 || slot > 3) return;
  int base = PRESET_BASE + slot * PRESET_SIZE;

  /* v2 layout: 29 bytes — includes stepsPerMl; jetVolume→uint16, jetPressure→uint8 */
  EEPROM.put(base,      (uint8_t)(0x80 | (uint8_t)pump.mode));  // v2 marker
  EEPROM.put(base + 1,  (uint8_t)pump.currentLiquid);
  EEPROM.put(base + 2,  pump.flowRate);
  EEPROM.put(base + 6,  pump.targetVolume);
  EEPROM.put(base + 10, pump.targetTime);
  EEPROM.put(base + 14, (uint16_t)(pump.jetVolume * 10.0f + 0.5f));   // 0.1-10.0 → 1-100
  EEPROM.put(base + 16, pump.jetInterval);
  EEPROM.put(base + 20, pump.jetFlowRate);
  EEPROM.put(base + 24, (uint8_t)(pump.jetPressure + 0.5f));           // 1-10
  EEPROM.put(base + 25, pump.stepsPerMl);                               // ★ 校准数据
  EEPROM.commit();
}

void loadPreset(int slot) {
  if (!isPresetValid(slot)) return;
  int base = PRESET_BASE + slot * PRESET_SIZE;
  uint8_t modeByte = EEPROM.read(base);
  bool isV2 = (modeByte & 0x80) != 0;
  pump.mode      = (PumpMode)(modeByte & 0x7F);
  pump.currentLiquid = EEPROM.read(base + 1);
  EEPROM.get(base + 2,  pump.flowRate);
  EEPROM.get(base + 6,  pump.targetVolume);
  EEPROM.get(base + 10, pump.targetTime);

  if (isV2) {
    /* v2: compressed fields + stepsPerMl */
    pump.jetVolume   = EEPROM.read(base + 14) / 10.0f;       // uint16→float
    EEPROM.get(base + 16, pump.jetInterval);
    EEPROM.get(base + 20, pump.jetFlowRate);
    pump.jetPressure = EEPROM.read(base + 24);                // uint8
    EEPROM.get(base + 25, pump.stepsPerMl);                   // ★ 校准数据
  } else {
    /* v1: legacy float fields */
    EEPROM.get(base + 14, pump.jetVolume);
    EEPROM.get(base + 18, pump.jetInterval);
    EEPROM.get(base + 22, pump.jetFlowRate);
    EEPROM.get(base + 26, pump.jetPressure);
    pump.stepsPerMl = pump.liquidSPM[pump.currentLiquid];
  }

  pump.flowRate     = constrain(pump.flowRate,     0.1, 1600.0);
  pump.targetVolume = constrain(pump.targetVolume, 0.1, 99999);
  pump.targetTime   = constrain(pump.targetTime,   1, 86400);
  pump.jetVolume    = constrain(pump.jetVolume,    0.1, 10.0);
  pump.jetInterval  = constrain(pump.jetInterval,  1, 60);
  pump.jetFlowRate  = constrain(pump.jetFlowRate,  10, 1600.0);
  pump.jetPressure  = constrain(pump.jetPressure,  1, 10);
  pump.stepsPerMl   = constrain(pump.stepsPerMl,   10, 50000);
  /* Also update liquidSPM so calibration is consistent */
  pump.liquidSPM[pump.currentLiquid] = pump.stepsPerMl;
  markDirty();
  saveParams();
  resetPump();
  pump.jetCount = 0;
}
