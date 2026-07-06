/******************************************************************************
 * wifi_manager.cpp - WiFi 管理实现
 *
 * SoftAP:       SSID = "PumpCtrl-XXXX" (XXXX = MAC 后 4 位)
 *               IP   = 192.168.4.1
 * mDNS:         pump.local
 *
 * Station 模式下连接失败 -> 30 秒超时自动回退 SoftAP
 *
 * EEPROM 布局 (WiFi 部分):
 *   Offset 184: magic (2B) = 0x5746 ("WF")
 *   Offset 186: SSID    (32B, null-terminated)
 *   Offset 218: Password (64B, null-terminated)
 *   Offset 282: mode     (1B, 0=AP only, 1=STA+AP fallback)
 ******************************************************************************/
#include "wifi_manager.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <EEPROM.h>

// ----- EEPROM 偏移量 -----
#define WIFI_EEPROM_BASE   184
#define WIFI_EEPROM_MAGIC  0x5746   // "WF"

// ----- 全局 WiFi 状态 -----
static WiFiConfig wifiCfg;
static String apSSID;
static IPAddress localIP;
static bool wifiReady = false;

void initWiFi() {
  // 1. 尝试加载 EEPROM 中的 WiFi 配置
  bool hasConfig = loadWiFiConfig(wifiCfg);

  // 2. 生成唯一的 SoftAP SSID
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macSuffix[5];
  snprintf(macSuffix, sizeof(macSuffix), "%02X%02X", mac[4], mac[5]);
  apSSID = String(WIFI_AP_SSID_PREFIX) + macSuffix;

  // 3. 始终以 SoftAP 启动 (安全默认, 避免模式切换导致 LWIP 冲突)
  WiFi.mode(WIFI_AP);
  delay(200);
  WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_GATEWAY, WIFI_AP_SUBNET);
  WiFi.softAP(apSSID.c_str(), "12345678", 1, 0, 2);  // WPA2, 密码 12345678
  // 等待 WiFi 初始化完成 (确保 LWIP 栈就绪再继续)
  delay(500);
  localIP = WiFi.softAPIP();

  // 4. 如果有 STA 配置, 切换到 AP+STA 模式后台连接 (不阻塞)
  if (hasConfig && wifiCfg.mode == WIFI_MODE_STA_FALLBACK && strlen(wifiCfg.ssid) > 0) {
    WiFi.mode(WIFI_AP_STA);
    delay(100);
    // 重新应用 AP 配置 (模式切换后需要)
    WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_GATEWAY, WIFI_AP_SUBNET);
    WiFi.softAP(apSSID.c_str(), "12345678", 1, 0, 2);
    WiFi.begin(wifiCfg.ssid, wifiCfg.pass);
    // 不等待连接 — 让它在后台连, SoftAP 始终可用
  }

  // 5. 启动 mDNS
  if (MDNS.begin("pump")) {
    MDNS.addService("http", "tcp", 80);
  }

  wifiReady = true;
}

void getWiFiStatus(const char*& mode, const char*& ip, int& clientCount) {
  static char ipBuf[16];
  mode = (WiFi.status() == WL_CONNECTED) ? "sta+ap" : "ap";
  snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d",
           localIP[0], localIP[1], localIP[2], localIP[3]);
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

  // 读取 SSID
  for (int i = 0; i < 32; i++) {
    cfg.ssid[i] = EEPROM.read(WIFI_EEPROM_BASE + 2 + i);
  }
  cfg.ssid[31] = '\0';

  // 读取 Password
  for (int i = 0; i < 64; i++) {
    cfg.pass[i] = EEPROM.read(WIFI_EEPROM_BASE + 34 + i);
  }
  cfg.pass[63] = '\0';

  cfg.mode = EEPROM.read(WIFI_EEPROM_BASE + 98);
  if (cfg.mode > WIFI_MODE_STA_FALLBACK) cfg.mode = WIFI_MODE_AP_ONLY;

  return true;
}

void saveWiFiConfig(const WiFiConfig& cfg) {
  EEPROM.put(WIFI_EEPROM_BASE, (uint16_t)WIFI_EEPROM_MAGIC);

  // 写入 SSID
  for (int i = 0; i < 32; i++) {
    EEPROM.write(WIFI_EEPROM_BASE + 2 + i, cfg.ssid[i]);
  }

  // 写入 Password
  for (int i = 0; i < 64; i++) {
    EEPROM.write(WIFI_EEPROM_BASE + 34 + i, cfg.pass[i]);
  }

  EEPROM.write(WIFI_EEPROM_BASE + 98, cfg.mode);
  EEPROM.commit();
}

void restartWiFi() {
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  delay(500);
  initWiFi();
}
