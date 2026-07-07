/******************************************************************************
 * wifi_manager.h - WiFi 管理
 *
 * 默认模式: SoftAP (手机/PC 直连, 无需路由器)
 * 可选模式: Station + SoftAP 回退 (连入局域网时)
 *
 * WiFi 配置存储在 EEPROM (offset 184+, 共 99 字节)
 ******************************************************************************/
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

// ----- WiFi 模式 -----
#define WIFI_MODE_AP_ONLY  0
#define WIFI_MODE_STA_FALLBACK 1

// ----- WiFi 配置结构 (存储在 EEPROM) -----
struct WiFiConfig {
  char ssid[32];
  char pass[64];
  uint8_t mode;  // 0=仅AP, 1=STA+AP回退
};

// 初始化 WiFi (setup 中调用)
// 直接从 WIFI_AP_STA 双模启动, SoftAP 始终可用
// 如有已保存的 STA 配置, 后台连接家里 WiFi
void initWiFi();

// WiFi 维护 (loop 中调用, 处理 STA 连接状态)
void wifiMaintain();

// 获取当前 WiFi 状态 (用于遥测)
// 返回 JSON 片段, 如: "ap", "192.168.4.1", 1
void getWiFiStatus(const char*& mode, const char*& ip, int& clientCount);

// EEPROM WiFi 配置读写
bool loadWiFiConfig(WiFiConfig& cfg);
void saveWiFiConfig(const WiFiConfig& cfg);

// 重启 WiFi (应用新配置后调用)
void restartWiFi();

// 默认 SoftAP 配置
#define WIFI_AP_SSID_PREFIX "PumpCtrl-"
#define WIFI_AP_IP          IPAddress(192, 168, 4, 1)
#define WIFI_AP_GATEWAY     IPAddress(192, 168, 4, 1)
#define WIFI_AP_SUBNET      IPAddress(255, 255, 255, 0)

#endif // WIFI_MANAGER_H
