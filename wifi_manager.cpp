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
  // 0. 禁用 WiFi NVS 持久化: 防止 ESP-IDF 从 NVS 自动加载旧 AP 配置
  //    导致 WiFi.mode(WIFI_AP_STA) 时出现两个 AP (旧SSID + 新SSID)
  WiFi.persistent(false);

  // 1. 加载 EEPROM 中的 WiFi 配置
  bool hasConfig = loadWiFiConfig(wifiCfg);

  // 2. 生成唯一 SoftAP SSID — 必须读 eFuse MAC
  //    注意: core 3.3.x 的 WiFi.macAddress() 走 esp_netif_get_mac(),
  //    WiFi 初始化前 netif 未创建会失败且不写缓冲区, 导致读到栈残留
  //    (全零 → 热点名变成 PumpCtrl-0000)。esp_read_mac 读 eFuse, 随时可用。
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);   // 与老核心 WiFi.macAddress() 相同来源
  char macSuffix[5];
  snprintf(macSuffix, sizeof(macSuffix), "%02X%02X", mac[4], mac[5]);
  apSSID = String(WIFI_AP_SSID_PREFIX) + macSuffix;
  Serial.printf("[WIFI] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // 3. 断开任何残留 AP (确保只有一个)
  WiFi.softAPdisconnect(true);
  delay(50);

  // 4. 直接以 AP+STA 双模启动 (避免后续模式切换)
  WiFi.mode(WIFI_AP_STA);
  delay(100);

  // 注意: 这里曾调用 esp_wifi_restore() 清 NVS 幽灵 AP。但该 API 会连带重置
  // esp_wifi_set_mode() 的结果 (见 IDF 文档), 把上面刚设好的 AP_STA 抹回默认,
  // 导致随后 softAP() 无 AP 接口可用而失败 —— 表现为日志一切正常但热点不广播。
  // 幽灵 AP 已由 persistent(false) + softAPdisconnect(true) 解决, 故移除。
  // 若仍有旧固件写入的 NVS 残留, 用 IDE 的 "Erase All Flash" 一次性清除。

  // 5. 配置 SoftAP (全功率 20dBm, 热管理已解决)
  WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_GATEWAY, WIFI_AP_SUBNET);
  bool apOk = WiFi.softAP(apSSID.c_str(), "12345678", 1, 0, 4);
  if (apOk) {
    Serial.printf("[WIFI] AP SSID: %s\n", apSSID.c_str());
  } else {
    // 不要静默失败: 之前丢弃返回值导致 AP 没建起来也照样打印 SSID
    Serial.printf("[WIFI] softAP() FAILED! mode=%d\n", (int)WiFi.getMode());
  }
  esp_wifi_set_max_tx_power(80);  // 20dBm 全功率
  esp_wifi_set_ps(WIFI_PS_NONE);  // 禁用 WiFi 省电模式, 避免唤醒延迟导致步进电机卡顿
  delay(300);
  localIP = WiFi.softAPIP();
  Serial.printf("[WIFI] AP IP: %s  mode=%d  clients=%d\n",
                localIP.toString().c_str(), (int)WiFi.getMode(),
                WiFi.softAPgetStationNum());

  // 5. 如有 STA 配置，后台连接家里 WiFi
  if (hasConfig && wifiCfg.mode == WIFI_MODE_STA_FALLBACK && strlen(wifiCfg.ssid) > 0) {
    Serial.printf("[WIFI] STA connecting to \"%s\" ...\n", wifiCfg.ssid);
    WiFi.begin(wifiCfg.ssid, wifiCfg.pass);
    staConnectStart = millis();
    staConnecting = true;
  } else {
    // 打印出来才能区分"没配置"和"配置读坏了"
    Serial.printf("[WIFI] STA skipped (hasConfig=%d mode=%d ssid=\"%s\")\n",
                  (int)hasConfig, (int)wifiCfg.mode, wifiCfg.ssid);
  }

  // 6. 启动 mDNS (仅覆盖 AP 接口; STA 拿到 IP 后由 wifiMaintain 重注册)
  if (MDNS.begin("pump")) {
    MDNS.addService("http", "tcp", 80);
  }

  wifiReady = true;
}

// STA 连接维护 (loop 中调用)
// 三态: 已连接(监控掉线) / 连接中(等结果) / 空闲(定期重试)
static bool          staWasConnected = false;
static unsigned long staLastRetry    = 0;
#define STA_RETRY_MS   60000UL   // 失败/掉线后重试间隔
#define STA_TIMEOUT_MS 30000UL   // 单次连接超时

// STA 拿到 IP 后必须重注册 mDNS: initWiFi 里注册时 STA 尚无 IP,
// pump.local 只绑到了 AP 网段, 局域网侧解析不到。
static void restartMdns() {
  MDNS.end();
  if (MDNS.begin("pump")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[WIFI] mDNS re-registered -> pump.local");
  } else {
    Serial.println("[WIFI] mDNS re-register FAILED");
  }
}

void wifiMaintain() {
  wl_status_t status = WiFi.status();

  // --- 已连接: 只监控掉线 ---
  if (staWasConnected) {
    if (status != WL_CONNECTED) {
      Serial.printf("[WIFI] STA lost (status=%d), will retry\n", (int)status);
      staWasConnected = false;
      staConnecting   = false;
      staLastRetry    = millis();
    }
    return;
  }

  // --- 连接中: 等结果 ---
  if (staConnecting) {
    if (status == WL_CONNECTED) {
      staConnecting   = false;
      staWasConnected = true;
      Serial.printf("[WIFI] STA connected: IP=%s  RSSI=%d dBm  ch=%d\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
      restartMdns();
      return;
    }
    if (millis() - staConnectStart > STA_TIMEOUT_MS ||
        status == WL_CONNECT_FAILED ||
        status == WL_NO_SSID_AVAIL ||
        status == WL_CONNECTION_LOST) {
      Serial.printf("[WIFI] STA connect failed (status=%d), retry in %lus\n",
                    (int)status, STA_RETRY_MS / 1000);
      staConnecting = false;
      staLastRetry  = millis();
      // 不 disconnect — 让 WiFi stack 自己管理
    }
    return;
  }

  // --- 空闲: 有配置就定期重试 (原实现在此永久放弃) ---
  if (wifiCfg.mode == WIFI_MODE_STA_FALLBACK && strlen(wifiCfg.ssid) > 0 &&
      millis() - staLastRetry > STA_RETRY_MS) {
    Serial.printf("[WIFI] STA retry \"%s\" ...\n", wifiCfg.ssid);
    WiFi.begin(wifiCfg.ssid, wifiCfg.pass);
    staConnectStart = millis();
    staConnecting   = true;
    staLastRetry    = millis();
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

const char* getApSSID() {
  return apSSID.c_str();
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
  staConnecting   = false;
  staWasConnected = false;
  staLastRetry    = millis();
  initWiFi();
}
