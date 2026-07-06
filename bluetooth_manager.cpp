/******************************************************************************
 * bluetooth_manager.cpp - BLE UART 瀹炵幇 (NimBLE)
 *
 * Nordic UART Service (NUS):
 *   RX Characteristic (6E400002-...) - Write  (鎵嬫満鈫扙SP)
 *   TX Characteristic (6E400003-...) - Notify (ESP鈫掓墜鏈?
 ******************************************************************************/
#include "bluetooth_manager.h"
#include "command_protocol.h"
#include <NimBLEDevice.h>
#include <esp_mac.h>

// ---- NUS UUID ----
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

static NimBLEServer*         pServer   = nullptr;
static NimBLECharacteristic* pTxChar   = nullptr;
static bool                  connected = false;
static bool                  hasData   = false;
static String                rxBuffer;
static SemaphoreHandle_t     rxMutex   = nullptr;

// ---- 杩炴帴/鏂紑鍥炶皟 ----
class PumpBLECallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    connected = true;
    Serial.printf("[BLE] connected: %s\n", connInfo.getAddress().toString().c_str());
  }
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    connected = false;
    Serial.println("[BLE] disconnected");
    NimBLEDevice::startAdvertising();
  }
};

// ---- RX 鍐欏洖璋?(鎵嬫満鈫扙SP) ----
class PumpRXCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    std::string val = pChar->getValue();
    rxBuffer += val.c_str();
    hasData = true;
  }
};

void initBluetooth() {
  rxMutex = xSemaphoreCreateMutex();
  // 鍏堣幏鍙?MAC 鐢熸垚璁惧鍚? 鍐嶅垵濮嬪寲
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  char name[32];
  snprintf(name, sizeof(name), "PumpCtrl-%02X%02X", mac[4], mac[5]);

  NimBLEDevice::init(name);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setMTU(256);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new PumpBLECallbacks());

  NimBLEService* pService = pServer->createService(SERVICE_UUID);

  pTxChar = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, NIMBLE_PROPERTY::NOTIFY);

  NimBLECharacteristic* pRxChar = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, NIMBLE_PROPERTY::WRITE);
  pRxChar->setCallbacks(new PumpRXCallbacks());

  pService->start();

  // 閰嶇疆骞挎挱鏁版嵁 (璁惧鍚?+ 鏈嶅姟 UUID)
  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID(SERVICE_UUID);

    NimBLEAdvertisementData advData;
  advData.setName(name);
  advData.setCompleteServices(NimBLEUUID(SERVICE_UUID));
  pAdv->setAdvertisementData(advData);

  NimBLEAdvertisementData scanData;
  scanData.setName(name);
  pAdv->setScanResponseData(scanData);

  pAdv->start();
  Serial.printf("[BLE] started as '%s'\n", name);
}

void handleBluetooth() {
  if (!hasData) return;
  hasData = false;

  String data = rxBuffer;
  rxBuffer = "";

  while (data.length() > 0) {
    int nl = data.indexOf('\n');
    String cmd;
    if (nl >= 0) {
      cmd = data.substring(0, nl);
      data = data.substring(nl + 1);
    } else {
      cmd = data;
      data = "";
    }

    cmd.trim();
    cmd.replace("\r", "");
    if (cmd.length() == 0) continue;

    if (cmd == "status" || cmd == "s") {
      bleSend(buildTelemetryJson());
      continue;
    }

    const char* resp = parseAndExecute(cmd.c_str());
    bleSend(resp);
  }
}

void bleSend(const char* data) {
  if (!connected || !pTxChar) return;
  pTxChar->setValue((const uint8_t*)data, strlen(data));
  pTxChar->notify();
  delay(5);
}

bool bleConnected() {
  return connected;
}



