/******************************************************************************
 * Peristaltic Pump Controller v3 — YZ1515 precision dispensing / jet workstation
 * Hardware: ESP32-S3-WROOM-1-N16 (16 MB Flash)
 * v2.3.2: PumpState struct extracted, pump_machine state machine module, cleaner architecture
 * 当前版本号以 version.h 的 FW_VERSION 为准，变更历史见 README §6 更新日志
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
  // ENA_PIN(18) 有驱动, 但**没有接线到 DM542**（方案D: 脱机端悬空 = 默认使能,
  // 降温靠 DM542 自己的 SW4 半流待机）。所以这行 HIGH 对驱动器没有任何电气作用,
  // 只是把 GPIO 定在一个确定电平上。
  // 原注释写「ENA 始终使能」是误导: 按 DM542 的接口语义（ENA 光耦导通 = 脱机、
  // 悬空 = 默认使能）, HIGH 究竟是使能还是脱机, 完全取决于共阳/推挽接法与
  // 3.3V→5V 放大器是否反相 —— 而这里根本没接线, 无从谈起。
  // ⚠️ 将来若真的接线: 先用万用表量「MCU 输出 HIGH 时 DM542 侧光耦是否导通」,
  //    再决定这行该写 HIGH 还是 LOW, 不要沿用本段措辞。接线后还要复查
  //    ensureStepperOn() 与遥测的 stepperEnabled（审查档案 B3）。
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

  // wifi_restart 命令的延迟执行点。必须排在 handleWebClients() 之后 —— 那时响应已经
  // flush + stop 过, 拆 SoftAP 才不会把响应吞掉; 也排在 tick / saveParams 之后, 让它们
  // 先各转一轮再阻塞约 1.2 s。详见 wifi_manager.h 的 requestWifiRestart() 说明。
  serviceWifiRestart();
}
