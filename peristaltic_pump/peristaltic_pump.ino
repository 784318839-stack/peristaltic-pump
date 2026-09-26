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
  Serial.printf("[SETUP] CPU: %lu MHz\n", (unsigned long)getCpuFrequencyMhz());
  Serial.println("[SETUP] start");

  if (psramFound()) {
    Serial.printf("[SETUP] PSRAM: %lu KB (%.1f MB)\n", (unsigned long)(ESP.getPsramSize() / 1024), ESP.getPsramSize() / 1048576.0);
    Serial.printf("[SETUP] Free PSRAM: %lu KB\n", (unsigned long)(ESP.getFreePsram() / 1024));
  }
  Serial.printf("[SETUP] Free internal heap: %lu KB\n", (unsigned long)(ESP.getFreeHeap() / 1024));

  initTelemetryBuffer();
  initResponseBuffer();
  initSerialBuffers();

  EEPROM.begin(512);
  // 首次上电必须显式 markDirty(): saveParams() 开头就有 if (!eepromDirty) return,
  // 否则 magic 永远写不进去, 每次开机都走 loadParams 失败分支, 管路寿命累计也不持久化
  if (!loadParams()) { markDirty(); saveParams(); }
  Serial.println("[SETUP] eeprom ok");

  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
  pinMode(STEP_PIN, OUTPUT); digitalWrite(STEP_PIN, LOW);
  pinMode(DIR_PIN, OUTPUT); digitalWrite(DIR_PIN, LOW);
  pinMode(ENA_PIN, OUTPUT); digitalWrite(ENA_PIN, LOW);

  stepperEngine.init();
  stepper = stepperEngine.stepperConnectToPin(STEP_PIN);
  if (!stepper) {
    // 拿不到步进对象整机就没有意义, 而后续所有 stepper-> 调用都会解引用空指针。
    // 与其进入崩溃-重启循环, 不如停在这里给出明确诊断。
    Serial.printf("[SETUP] FATAL: stepperConnectToPin(%d) failed\n", STEP_PIN);
    for (;;) delay(1000);
  }
  stepper->setDirectionPin(DIR_PIN);
  // ENA 始终使能: DM542 不支持运行时切使能, SW4 半流待机已够降温
  digitalWrite(ENA_PIN, HIGH);
  pump.stepperEnabled = true;
  updateStepperSpeed();
  Serial.println("[SETUP] gpio ok");

  initSerialCommands(); Serial.println("[SETUP] serial ok");
  initHardwareUart(); Serial.println("[SETUP] hw uart ok");
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

  pump_machine_tick();

  if (pump.eepromDirty && (pump.state == STATE_IDLE || pump.state == DONE)) saveParams();
}
