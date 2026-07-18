/******************************************************************************
 * Peristaltic Pump Controller v3 — YZ1515 precision dispensing / jet workstation
 * Hardware: ESP32-S3-WROOM-1-N16 (16 MB Flash)
 * v2.3.2: PumpState struct extracted, pump_machine state machine module, cleaner architecture
 ******************************************************************************/

#include <Arduino.h>
#include <FastAccelStepper.h>

#include "command_protocol.h"
#include "serial_commands.h"
#include "wifi_manager.h"
#include "web_handlers.h"
#include "bluetooth_manager.h"

#include "pump_state.h"
#include "pump_shared.h"
#include "pump_machine.h"
#include "pump_core.h"
#include "buzzer.h"
#include "eeprom_store.h"
#include "led.h"

FastAccelStepperEngine stepperEngine;
FastAccelStepper *stepper = nullptr;

void setup() {
  Serial.begin(115200);
  delay(2000);

  setCpuFrequencyMhz(160);
  Serial.printf("[SETUP] CPU: %d MHz\n", getCpuFrequencyMhz());
  Serial.println("[SETUP] start");

  if (psramFound()) {
    Serial.printf("[SETUP] PSRAM: %d KB (%.1f MB)\n", ESP.getPsramSize() / 1024, ESP.getPsramSize() / 1048576.0);
    Serial.printf("[SETUP] Free PSRAM: %d KB\n", ESP.getFreePsram() / 1024);
  }
  Serial.printf("[SETUP] Free internal heap: %d KB\n", ESP.getFreeHeap() / 1024);

  initTelemetryBuffer();
  initResponseBuffer();
  initSerialBuffers();

  EEPROM.begin(512);
  if (!loadParams()) saveParams();
  Serial.println("[SETUP] eeprom ok");

  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
  pinMode(STEP_PIN, OUTPUT); digitalWrite(STEP_PIN, LOW);
  pinMode(DIR_PIN, OUTPUT); digitalWrite(DIR_PIN, LOW);
  pinMode(ENA_PIN, OUTPUT); digitalWrite(ENA_PIN, LOW);

  stepperEngine.init();
  stepper = stepperEngine.stepperConnectToPin(STEP_PIN);
  if (stepper) {
    stepper->setDirectionPin(DIR_PIN);
    stepper->setEnablePin(ENA_PIN);
    // 不用 setAutoEnable, 6N137+上拉下时序会触发 DM542 报警
    // 改用手动: enableOutputs/disableOutputs + checkIdleDisable() 5s 空闲断电
    stepper->enableOutputs();
    pump.stepperEnabled = true;
  }
  pump.lastStepperActivity = millis();
  updateStepperSpeed();
  Serial.println("[SETUP] gpio ok");

  initSerialCommands(); Serial.println("[SETUP] serial ok");
  initHardwareUart(); Serial.println("[SETUP] hw uart ok");
  initBluetooth(); Serial.println("[SETUP] ble ok");
  initWiFi(); initWebServer(); Serial.println("[SETUP] wifi ok");
  led_init(); Serial.println("[SETUP] led ok");

  Serial.println("[SETUP] done");
}

void loop() {
  buzzer_tick();
  led_tick();

  processSerialCommands();
  processHardwareUart();
  handleWebClients();
  handleBluetooth();
  wifiMaintain();

  pump_machine_tick();

  checkIdleDisable();

  if (pump.eepromDirty && (pump.state == STATE_IDLE || pump.state == DONE)) saveParams();

  led_update();
}
