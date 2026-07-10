/******************************************************************************
 * wifi_manager.cpp - WiFi 管理 (SoftAP + Station 双模)
 *
 * 启动策略:
 *   1. 直接从 WIFI_AP_STA 双模启动 (避免模式切换导致 LWIP 锁冲突)
 *   2. SoftAP 始终可用: PumpCtrl-XXXX, IP 192.168.4.1, 密码 12345678
 *   3. 如有已保存的 STA 配置, 后台尝试连接家里 WiFi (不阻塞)
 *   4. STA 连接成功后可通过路由器分配的 IP 访问 (看 /api/info)
 *   5. 支持 mDNS: pump.local  (同时绑定两个接口)
 ******************************************************************************/
#include "wifi_manager.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <EEPROM.h>
#include <esp_wifi.h>
#include <esp_mac.h>

// ----- EEPROM 偏移量 -----
#define WIFI_EEPROM_BASE   184
#define WIFI_EEPROM_MAGIC  0x5746   // "WF"

// ----- 密码加密 (XOR with device MAC, 拆机读 EEPROM 也是乱码) -----
static uint8_t  cryptKey[6];
static bool     cryptKeyReady = false;

static void initCryptKey() {
  if (cryptKeyReady) return;
  esp_read_mac(cryptKey, ESP_MAC_BASE);  // 出厂熔丝 MAC, 永不改变
  cryptKeyReady = true;
}

static void cryptData(uint8_t* data, size_t len) {
  initCryptKey();
  for (size_t i = 0; i < len; i++) {
    data[i] ^= cryptKey[i % 6];
  }
}

// ----- 全局状态 -----
static WiFiConfig wifiCfg;
static String apSSID;
static IPAddress localIP;
static bool wifiReady = false;
static unsigned long staConnectStart = 0;
static bool staConnecting = false;

void initWiFi() {
  // 1. 加载 EEPROM 中的 WiFi 配置
  bool hasConfig = loadWiFiConfig(wifiCfg);

  // 2. 生成唯一 SoftAP SSID
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macSuffix[5];
  snprintf(macSuffix, sizeof(macSuffix), "%02X%02X", mac[4], mac[5]);
  apSSID = String(WIFI_AP_SSID_PREFIX) + macSuffix;

  // 3. 直接以 AP+STA 双模启动 (避免后续模式切换)
  WiFi.mode(WIFI_AP_STA);
  delay(100);

  // 4. 配置 SoftAP (降低发射功率降温: 默认 20dBm -> 8dBm)
  WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_GATEWAY, WIFI_AP_SUBNET);
  WiFi.softAP(apSSID.c_str(), "12345678", 1, 0, 2);
  esp_wifi_set_max_tx_power(32);  // 8dBm ≈ 6mW, 家庭室内足够
  esp_wifi_set_ps(WIFI_PS_NONE);  // 禁用 WiFi 省电模式, 避免唤醒延迟导致步进电机卡顿
  delay(300);
  localIP = WiFi.softAPIP();

  // 5. 如有 STA 配置，后台连接家里 WiFi
  if (hasConfig && wifiCfg.mode == WIFI_MODE_STA_FALLBACK && strlen(wifiCfg.ssid) > 0) {
    WiFi.begin(wifiCfg.ssid, wifiCfg.pass);
    staConnectStart = millis();
    staConnecting = true;
  }

  // 6. 启动 mDNS
  if (MDNS.begin("pump")) {
    MDNS.addService("http", "tcp", 80);
  }

  wifiReady = true;
}

// STA 连接维护 (loop 中调用)
void wifiMaintain() {
  if (!staConnecting) return;

  wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED) {
    // 连接成功, mDNS 已在 initWiFi 注册好, 无需重注册
    staConnecting = false;
    return;
  }

  // 超时 (30 秒) 或连接失败 → 放弃本次 STA 尝试, SoftAP 仍在
  if (millis() - staConnectStart > 30000 ||
      status == WL_CONNECT_FAILED ||
      status == WL_NO_SSID_AVAIL ||
      status == WL_CONNECTION_LOST) {
    staConnecting = false;
    // 不 disconnnect — 让 WiFi stack 自己管理
  }
}

void getWiFiStatus(const char*& mode, const char*& ip, int& clientCount) {
  static char ipBuf[24];
  wl_status_t sta = WiFi.status();

  if (sta == WL_CONNECTED) {
    mode = "sta+ap";
    snprintf(ipBuf, sizeof(ipBuf), "%s", WiFi.localIP().toString().c_str());
  } else {
    mode = "ap";
    snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d",
             localIP[0], localIP[1], localIP[2], localIP[3]);
  }
  ip = ipBuf;
  clientCount = WiFi.softAPgetStationNum();
}

bool loadWiFiConfig(WiFiConfig& cfg) {
  uint16_t magic;
  EEPROM.get(WIFI_EEPROM_BASE, magic);
  if (magic != WIFI_EEPROM_MAGIC) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = WIFI_MODE_AP_ONLY;
    return false;
  }

  for (int i = 0; i < 32; i++) {
    cfg.ssid[i] = EEPROM.read(WIFI_EEPROM_BASE + 2 + i);
  }
  cfg.ssid[31] = '\0';

  // 读取加密密码并 XOR 解密
  for (int i = 0; i < 64; i++) {
    cfg.pass[i] = EEPROM.read(WIFI_EEPROM_BASE + 34 + i);
  }
  cryptData((uint8_t*)cfg.pass, 64);  // XOR 解密 (与加密同一操作)

  // 完整性校验: 解密后必须是可打印 ASCII 或空字符
  bool passValid = true;
  for (int i = 0; i < 64; i++) {
    uint8_t c = (uint8_t)cfg.pass[i];
    if (c != 0 && (c < 0x20 || c > 0x7E)) { passValid = false; break; }
  }
  if (!passValid) {
    // 密钥不匹配 (MAC 改变 / 数据损坏) → 清空配置, 回退 SoftAP
    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = WIFI_MODE_AP_ONLY;
    return false;
  }
  cfg.pass[63] = '\0';

  cfg.mode = EEPROM.read(WIFI_EEPROM_BASE + 98);
  if (cfg.mode > WIFI_MODE_STA_FALLBACK) cfg.mode = WIFI_MODE_AP_ONLY;

  return true;
}

void saveWiFiConfig(const WiFiConfig& cfg) {
  EEPROM.put(WIFI_EEPROM_BASE, (uint16_t)WIFI_EEPROM_MAGIC);

  // SSID 明文存储 (不算敏感)
  for (int i = 0; i < 32; i++) {
    EEPROM.write(WIFI_EEPROM_BASE + 2 + i, cfg.ssid[i]);
  }

  // 密码 XOR 加密后存储
  char encPass[64];
  memcpy(encPass, cfg.pass, 64);
  cryptData((uint8_t*)encPass, 64);
  for (int i = 0; i < 64; i++) {
    EEPROM.write(WIFI_EEPROM_BASE + 34 + i, encPass[i]);
  }

  EEPROM.write(WIFI_EEPROM_BASE + 98, cfg.mode);
  EEPROM.commit();
}

void restartWiFi() {
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  delay(500);
  staConnecting = false;
  initWiFi();
}
