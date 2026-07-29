// eeprom_store.cpp - EEPROM layout, save/load
#include "eeprom_store.h"
#include "pump_shared.h"
#include "pump_state.h"
#include <EEPROM.h>

#define EEPROM_MAGIC  0x5062  // v4.3: TMC2226 16 细分 (3200 pulse/rev)
#define EEPROM_ADDR   0

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

