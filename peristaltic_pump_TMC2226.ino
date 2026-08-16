/******************************************************************************
 * Peristaltic Pump Controller v3 — YZ1515 precision dispensing / jet workstation
 * Hardware: ESP32-S3-WROOM-1-N16 (16 MB Flash)
 * v2.5.0: StallGuard 堵转保护, 自动断电, 自检, 网络 PIN, 启动时序安全
 ******************************************************************************/

#include <Arduino.h>
#include <FastAccelStepper.h>

#include "command_protocol.h"
#include "serial_commands.h"
#include "wifi_manager.h"
#include "web_handlers.h"
#include "sw_uart.h"
#include "tmc2226.h"

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
  // 启动时序安全: 上电先禁用电机 (ENN=HIGH), 首次启动时 ensureStepperOn() 再使能
  pinMode(ENA_PIN, OUTPUT); digitalWrite(ENA_PIN, HIGH);
  pump.stepperEnabled = false;

  stepperEngine.init();
  stepper = stepperEngine.stepperConnectToPin(STEP_PIN);
  if (stepper) {
    stepper->setDirectionPin(DIR_PIN);
  }
  pump.lastStepperActivity = millis();
  updateStepperSpeed();
  Serial.println("[SETUP] gpio ok");

  initSerialCommands(); Serial.println("[SETUP] serial ok");
  initHardwareUart(); Serial.println("[SETUP] hw uart ok");
  swuart_init(SW_UART_PIN, 9600); Serial.println("[SETUP] sw uart ok");
  tmc2226_init();
  if (tmc2226_test_comm()) {
    Serial.println("[SETUP] tmc2226 ok");
  } else {
    Serial.println("[SETUP] tmc2226 COMM FAIL! (check GPIO15 pullup / wiring)");
    beepCancel();
  }
  initWiFi(); initWebServer(); Serial.println("[SETUP] wifi ok");
  led_init(); Serial.println("[SETUP] led ok");

  Serial.println("[SETUP] done");
}

void loop() {
  buzzer_tick();
  led_tick();
  swuart_tick();

  processSerialCommands();
  processHardwareUart();
  handleWebClients();
  wifiMaintain();

  pump_machine_tick();

  // 自动断电: 待机/暂停/完成 5s 后关闭电机使能 (TMC2226 ENN=HIGH, 节能降热)
  if (pump.stepperEnabled &&
      (pump.state == STATE_IDLE || pump.state == PAUSED || pump.state == DONE) &&
      millis() - pump.lastStepperActivity > AUTO_OFF_MS) {
    digitalWrite(ENA_PIN, HIGH);
    pump.stepperEnabled = false;
    beepDisable();
  }

  if (pump.eepromDirty && (pump.state == STATE_IDLE || pump.state == DONE)) saveParams();

  led_update();
}
