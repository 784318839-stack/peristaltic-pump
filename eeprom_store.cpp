// eeprom_store.cpp - EEPROM layout, save/load, presets
#include "eeprom_store.h"
#include "pump_shared.h"
#include <EEPROM.h>

#define EEPROM_MAGIC  0x5058
#define EEPROM_ADDR   0
#define PRESET_BASE   64
#define PRESET_SIZE   30

void markDirty() { eepromDirty = true; }

void saveParams() {
  if (!eepromDirty) return;
  EEPROM.put(EEPROM_ADDR,     (uint16_t)EEPROM_MAGIC);
  EEPROM.put(EEPROM_ADDR + 2, stepsPerMl);
  EEPROM.put(EEPROM_ADDR + 6, flowRate);
  EEPROM.put(EEPROM_ADDR + 10, targetVolume);
  EEPROM.put(EEPROM_ADDR + 14, targetTime);
  EEPROM.put(EEPROM_ADDR + 18, (uint8_t)pumpMode);
  EEPROM.put(EEPROM_ADDR + 19, antiDripVol);
  EEPROM.put(EEPROM_ADDR + 23, totalDispensed);
  EEPROM.put(EEPROM_ADDR + 27, tubeLifeML);
  EEPROM.put(EEPROM_ADDR + 31, jetVolume);
  EEPROM.put(EEPROM_ADDR + 35, jetInterval);
  EEPROM.put(EEPROM_ADDR + 39, jetFlowRate);
  for (int i = 0; i < NUM_LIQUIDS; i++)
    EEPROM.put(EEPROM_ADDR + 43 + i * 4, liquidSPM[i]);
  EEPROM.put(EEPROM_ADDR + 59, (uint8_t)currentLiquid);
  EEPROM.put(EEPROM_ADDR + 60, jetPressure);
  EEPROM.commit();
  eepromDirty = false;
}

bool loadParams() {
  uint16_t magic;
  EEPROM.get(EEPROM_ADDR, magic);
  if (magic != EEPROM_MAGIC) return false;

  EEPROM.get(EEPROM_ADDR + 2,  stepsPerMl);
  EEPROM.get(EEPROM_ADDR + 6,  flowRate);
  EEPROM.get(EEPROM_ADDR + 10, targetVolume);
  EEPROM.get(EEPROM_ADDR + 14, targetTime);
  pumpMode = (PumpMode)EEPROM.read(EEPROM_ADDR + 18);
  if (pumpMode > MODE_JET) pumpMode = MODE_VOLUME;
  EEPROM.get(EEPROM_ADDR + 19, antiDripVol);
  EEPROM.get(EEPROM_ADDR + 23, totalDispensed);
  EEPROM.get(EEPROM_ADDR + 27, tubeLifeML);
  EEPROM.get(EEPROM_ADDR + 31, jetVolume);
  EEPROM.get(EEPROM_ADDR + 35, jetInterval);
  EEPROM.get(EEPROM_ADDR + 39, jetFlowRate);
  for (int i = 0; i < NUM_LIQUIDS; i++)
    EEPROM.get(EEPROM_ADDR + 43 + i * 4, liquidSPM[i]);
  currentLiquid = EEPROM.read(EEPROM_ADDR + 59);
  if (currentLiquid >= NUM_LIQUIDS) currentLiquid = 0;
  EEPROM.get(EEPROM_ADDR + 60, jetPressure);

  stepsPerMl    = constrain(stepsPerMl,    10, 50000);
  flowRate      = constrain(flowRate,      0.1, 2000.0);
  targetVolume  = constrain(targetVolume,  0.1, 99999);
  targetTime    = constrain(targetTime,    1, 86400);
  antiDripVol   = constrain(antiDripVol,   0, 5.0);
  tubeLifeML    = constrain(tubeLifeML,    0, 200000);
  jetVolume     = constrain(jetVolume,     0.1, 10.0);
  jetInterval   = constrain(jetInterval,   1, 60);
  jetFlowRate   = constrain(jetFlowRate,   10, 2000.0);
  jetPressure   = constrain(jetPressure,   1, 10);
  for (int i = 0; i < NUM_LIQUIDS; i++)
    liquidSPM[i] = constrain(liquidSPM[i], 10, 50000);
  stepsPerMl = liquidSPM[currentLiquid];
  return true;
}

bool isPresetValid(int slot) {
  if (slot < 0 || slot > 3) return false;
  uint8_t mode = EEPROM.read(PRESET_BASE + slot * PRESET_SIZE);
  return (mode <= 2);
}

void savePreset(int slot) {
  if (slot < 0 || slot > 3) return;
  int base = PRESET_BASE + slot * PRESET_SIZE;
  EEPROM.put(base,      (uint8_t)pumpMode);
  EEPROM.put(base + 1,  (uint8_t)currentLiquid);
  EEPROM.put(base + 2,  flowRate);
  EEPROM.put(base + 6,  targetVolume);
  EEPROM.put(base + 10, targetTime);
  EEPROM.put(base + 14, jetVolume);
  EEPROM.put(base + 18, jetInterval);
  EEPROM.put(base + 22, jetFlowRate);
  EEPROM.put(base + 26, jetPressure);
  EEPROM.commit();
}

void loadPreset(int slot) {
  if (!isPresetValid(slot)) return;
  int base = PRESET_BASE + slot * PRESET_SIZE;
  pumpMode      = (PumpMode)EEPROM.read(base);
  currentLiquid = EEPROM.read(base + 1);
  EEPROM.get(base + 2,  flowRate);
  EEPROM.get(base + 6,  targetVolume);
  EEPROM.get(base + 10, targetTime);
  EEPROM.get(base + 14, jetVolume);
  EEPROM.get(base + 18, jetInterval);
  EEPROM.get(base + 22, jetFlowRate);
  EEPROM.get(base + 26, jetPressure);
  flowRate     = constrain(flowRate,     0.1, 2000.0);
  targetVolume = constrain(targetVolume, 0.1, 99999);
  targetTime   = constrain(targetTime,   1, 86400);
  jetVolume    = constrain(jetVolume,    0.1, 10.0);
  jetInterval  = constrain(jetInterval,  1, 60);
  jetFlowRate  = constrain(jetFlowRate,  10, 2000.0);
  jetPressure  = constrain(jetPressure,  1, 10);
  stepsPerMl   = liquidSPM[currentLiquid];
  markDirty();
  saveParams();
  resetPump();
  jetCount = 0;
}
