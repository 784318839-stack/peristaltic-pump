/******************************************************************************
 * web_handlers.cpp - HTTP 鏈嶅姟瀹炵幇 (WiFiServer, 鏃?AsyncTCP 渚濊禆)
 *
 * 璺敱:
 *   GET  /              -> Web UI
 *   GET  /api/status    -> JSON 閬ユ祴
 *   GET  /api/cmd?c=xxx -> 鍛戒护
 *   POST /api/wifi      -> 淇濆瓨 WiFi 閰嶇疆 (JSON body)
 *   GET  /api/scan      -> 鎵弿闄勮繎 WiFi
 *   GET  /api/info      -> 缃戠粶淇℃伅 (IP / 妯″紡 / MAC)
 ******************************************************************************/
#include "web_handlers.h"
#include "command_protocol.h"
#include "wifi_manager.h"
#include "pump_shared.h"
#include "pump_state.h"
#include "tmc2226.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>

// ============================================================================
//                            HTTP 鏈嶅姟鍣?
// ============================================================================
static WiFiServer server(80);

// 网络控制 PIN (空 = 不启用, 启动时从 EEPROM 加载)
static char g_pin[PIN_MAX_LEN + 1] = "";

// 鍐呭祵 Web UI
#include "web_ui_gen.h"

// ============================================================================
//                            杈呭姪鍑芥暟
// ============================================================================

static void sendJson(WiFiClient &client, int code, const char* json) {
  client.print("HTTP/1.1 ");
  client.print(code);
  client.print(" OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n");
  client.print(json);
}

static void sendHtml(WiFiClient &client, int code, const char* html) {
  client.print("HTTP/1.1 ");
  client.print(code);
  client.print(" OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n");
  client.print(html);
}

static String getQueryParam(const String &url, const char* key) {
  String k = String(key) + "=";
  int start = url.indexOf(k);
  if (start < 0) return "";
  start += k.length();
  int end = url.indexOf('&', start);
  if (end < 0) end = url.indexOf(' ', start);
  if (end < 0) end = url.length();
  return url.substring(start, end);
}

// 浠?headers 涓彁鍙?Content-Length
static int getContentLength(const String &headers) {
  int idx = headers.indexOf("Content-Length:");
  if (idx < 0) idx = headers.indexOf("content-length:");
  if (idx < 0) return 0;
  idx += 15;
  int end = headers.indexOf('\r', idx);
  if (end < 0) end = headers.indexOf('\n', idx);
  if (end < 0) end = headers.length();
  return headers.substring(idx, end).toInt();
}

// ============================================================================
//                            WiFi scan (sync)
//
//  始终在 AP+STA 双模下同步扫描, 绝不切换 WiFi.mode()。
//  老固件扫描失败时切 WIFI_STA 回退, 会把 AP 接口关掉 → 连接在热点上的
//  手机/网页客户端全部断连; core 3.3.x 的 scanNetworks() 已重写
//  (直接 esp_wifi_scan_start + 状态位等待), 老核心异步扫描的状态机
//  bug 已不存在, 无需该回退。
//  扫描期间射频短暂离开信道, 由协议栈自动恢复, 不会断开任何连接。
//  结果缓存 15 秒, 防止前端 300ms 轮询积累的排队请求重复扫描。
// ============================================================================

static bool   scanBusy        = false;
static bool   scanResultReady = false;
static String scanResultJson  = "";
static unsigned long scanResultTime = 0;
#define SCAN_RESULT_TTL  15000  // cache results for 15 seconds

static String buildScanResultJson(int n) {
  String ownSSID = WiFi.softAPSSID();  // filter out our own AP
  String json = "{\"networks\":[";
  bool first = true;
  for (int i = 0; i < n && i < 20; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid == ownSSID) continue;  // skip self
    if (!first) json += ",";
    first = false;
    json += "{\"ssid\":\"";
    ssid.replace("\\", "\\\\");
    ssid.replace("\"", "\\\"");
    json += ssid;
    json += "\",\"rssi\":";
    json += WiFi.RSSI(i);
    json += ",\"secure\":";
    json += (WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");
    json += "}";
  }
  json += "]}";
  return json;
}

// ============================================================================
//                            璇锋眰璺敱
// ============================================================================

static void handleRequest(WiFiClient &client, const String &method,
                          const String &path, const String &body) {
  // GET / 鎴?/index.html -> Web UI
  if (method == "GET" && (path == "/" || path.startsWith("/index.html"))) {
    sendHtml(client, 200, WEB_UI);
    return;
  }

  // GET /manifest.json
  if (method == "GET" && path.startsWith("/manifest.json")) {
    sendJson(client, 200,
      "{\"name\":\"锠曞姩娉垫帶鍒跺櫒\",\"short_name\":\"PumpCtrl\","
      "\"start_url\":\"/\",\"display\":\"standalone\","
      "\"background_color\":\"#0d1117\",\"theme_color\":\"#0d1117\"}");
    return;
  }

  // GET /api/status -> 閬ユ祴
  if (method == "GET" && path.startsWith("/api/status")) {
    sendJson(client, 200, buildTelemetryJson());
    return;
  }

  // GET /api/cmd?c=xxx&v=yyy&s=zzz&m=mmm&i=iii&p=pin -> 鍛戒护
  if (method == "GET" && path.startsWith("/api/cmd")) {
    // PIN 校验 (已设置时, 网络控制必须携带匹配 p 参数)
    if (g_pin[0] && getQueryParam(path, "p") != g_pin) {
      sendJson(client, 401, "{\"ok\":false,\"error\":\"Invalid PIN\"}");
      return;
    }

    String cmd = getQueryParam(path, "c");
    String val = getQueryParam(path, "v");
    String slot = getQueryParam(path, "s");
    String mode = getQueryParam(path, "m");
    String idx = getQueryParam(path, "i");

    if (cmd.length() == 0) {
      sendJson(client, 400, "{\"ok\":false,\"error\":\"Missing cmd\"}");
      return;
    }

    // 转义 cmd/mode, 防止特殊字符破坏 JSON 结构
    String cmdEsc, modeEsc;
    for (unsigned int i = 0; i < cmd.length(); i++) {
      char ch = cmd.charAt(i);
      if (ch == '"' || ch == '\\') cmdEsc += '\\';
      cmdEsc += ch;
    }
    for (unsigned int i = 0; i < mode.length(); i++) {
      char ch = mode.charAt(i);
      if (ch == '"' || ch == '\\') modeEsc += '\\';
      modeEsc += ch;
    }

    String params;
    if (val.length() > 0) params += "\"value\":" + val;
    if (slot.length() > 0) {
      if (params.length() > 0) params += ",";
      params += "\"slot\":" + slot;
    }
    if (modeEsc.length() > 0) {
      if (params.length() > 0) params += ",";
      params += "\"mode\":\"" + modeEsc + "\"";
    }
    if (idx.length() > 0) {
      if (params.length() > 0) params += ",";
      params += "\"index\":" + idx;
    }

    String jsonCmd;
    if (params.length() > 0) {
      jsonCmd = "{\"cmd\":\"" + cmdEsc + "\",\"params\":{" + params + "}}";
    } else {
      jsonCmd = "{\"cmd\":\"" + cmdEsc + "\",\"params\":{}}";
    }

    const char* resp = parseAndExecute(jsonCmd.c_str());
    sendJson(client, 200, resp);
    return;
  }

  // GET /api/selftest -> 设备自检 (TMC2226 通信 / EEPROM / 内存)
  if (method == "GET" && path.startsWith("/api/selftest")) {
    uint32_t ioin = tmc2226_read(TMC_REG_IOIN);
    bool tmcOk = (ioin != 0) && (((ioin & 0x10) != 0) || ((ioin & 0x0F0000) != 0));
    uint16_t magic = 0;
    EEPROM.get(EEPROM_ADDR, magic);
    char buf[384];
    snprintf(buf, sizeof(buf),
      "{\"fw\":\"%s\",\"tmc2226\":%s,\"ioin\":\"0x%08lX\","
      "\"eepromMagic\":\"0x%04X\",\"eepromOk\":%s,"
      "\"psramFree\":%d,\"psramTotal\":%d,\"heapFree\":%d,\"heapTotal\":%d}",
      FW_VERSION,
      tmcOk ? "true" : "false", (unsigned long)ioin,
      magic, (magic == EEPROM_MAGIC) ? "true" : "false",
      (int)(ESP.getFreePsram() / 1024), (int)(ESP.getPsramSize() / 1024),
      (int)(ESP.getFreeHeap() / 1024), (int)(ESP.getHeapSize() / 1024));
    sendJson(client, 200, buf);
    return;
  }

  // POST /api/wifi -> 淇濆瓨 WiFi 閰嶇疆
  if (method == "POST" && path.startsWith("/api/wifi")) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      sendJson(client, 400, "{\"ok\":false,\"error\":\"Invalid JSON\"}");
      return;
    }

    // PIN 校验
    const char* pinField = doc["pin"] | "";
    if (g_pin[0] && strcmp(pinField, g_pin) != 0) {
      sendJson(client, 401, "{\"ok\":false,\"error\":\"Invalid PIN\"}");
      return;
    }

    WiFiConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    const char* ssid = doc["ssid"] | "";
    const char* pass = doc["pass"] | "";
    int mode = doc["mode"] | 0;

    strncpy(cfg.ssid, ssid, 31);
    cfg.ssid[31] = '\0';
    strncpy(cfg.pass, pass, 63);
    cfg.pass[63] = '\0';
    cfg.mode = (mode != 0) ? WIFI_MODE_STA_FALLBACK : WIFI_MODE_AP_ONLY;

    saveWiFiConfig(cfg);
    restartWiFi();

    sendJson(client, 200, "{\"ok\":true,\"saved\":true}");
    return;
  }

  // GET /api/scan -> WiFi sync scan (blocking ~1-4s)
  // Cached: subsequent requests within 15s get cached results instantly
  if (method == "GET" && path.startsWith("/api/scan")) {
    if (pump.state == RUNNING || pump.state == PAUSED) {
      sendJson(client, 200, "{\"ok\":false,\"error\":\"Pump busy\",\"done\":true,\"networks\":[]}");
      return;
    }

    // Expire old cache
    if (scanResultReady && millis() - scanResultTime > SCAN_RESULT_TTL) {
      scanResultReady = false;
    }

    // Return cached results (instant, handles queued polling requests)
    if (scanResultReady) {
      sendJson(client, 200, scanResultJson.c_str());
      return;
    }

    // Scan already running from another client, tell frontend to wait
    if (scanBusy) {
      sendJson(client, 200, "{\"ok\":true,\"done\":false,\"networks\":[]}");
      return;
    }

    scanBusy = true;

    // Clear stale scan state, then sync scan in place (AP+STA mode)
    WiFi.scanDelete();
    esp_wifi_clear_ap_list();
    delay(100);

    int n = WiFi.scanNetworks(false, false, false, 300);
    Serial.printf("[SCAN] sync scan result: %d\n", n);

    // Build and cache result (scan failure -> empty list, connections untouched)
    if (n > 0) {
      String json = "{\"ok\":true,\"done\":true,";
      json += buildScanResultJson(n).substring(1);
      scanResultJson = json;
    } else {
      scanResultJson = "{\"ok\":true,\"done\":true,\"networks\":[]}";
    }
    scanResultReady = true;
    scanResultTime = millis();

    sendJson(client, 200, scanResultJson.c_str());

    WiFi.scanDelete();
    scanBusy = false;
    return;
  }
  if (method == "GET" && path.startsWith("/api/info")) {
    const char* mode;
    const char* ip;
    int clients;
    getWiFiStatus(mode, ip, clients);

    char buf[256];
    snprintf(buf, sizeof(buf),
      "{\"mode\":\"%s\",\"ip\":\"%s\",\"clients\":%d,\"staConnected\":%s,\"mac\":\"%s\"}",
      mode, ip, clients,
      (WiFi.status() == WL_CONNECTED) ? "true" : "false",
      WiFi.macAddress().c_str());
    sendJson(client, 200, buf);
    return;
  }

  // 404
  sendJson(client, 404, "{\"ok\":false,\"error\":\"Not found\"}");
}

// ============================================================================
//                            瀹㈡埛绔鐞?
// ============================================================================

void initWebServer() {
  loadPin(g_pin, sizeof(g_pin));
  if (g_pin[0]) Serial.printf("[WEB] PIN protection enabled\n");
  server.begin();
}

void handleWebClients() {
  WiFiClient client = server.accept();
  if (!client) return;

  // 璇诲彇 HTTP 璇锋眰 (headers + body)
  unsigned long timeout = millis() + 200;
  String request;
  int contentLength = 0;
  bool headersDone = false;

  while (client.connected() && millis() < timeout) {
    if (client.available()) {
      char c = client.read();
      if (request.length() < 4096) request += c;
      timeout = millis() + 200;  // 姣忔璇诲彇閲嶇疆瓒呮椂

      if (!headersDone) {
        if (request.endsWith("\r\n\r\n") || request.endsWith("\n\n")) {
          headersDone = true;
          contentLength = getContentLength(request);
          if (contentLength <= 0) break;  // 鏃?body锛岀粨鏉?
        }
      } else {
        // 璁＄畻宸茶鍙栫殑 body 瀛楄妭鏁?
        int headerEnd = request.indexOf("\r\n\r\n");
        if (headerEnd < 0) headerEnd = request.indexOf("\n\n");
        int bodyRead = request.length() - headerEnd - 4;
        if (bodyRead >= contentLength) break;  // body 璇诲彇瀹屾瘯
      }
    }
  }

  // 瑙ｆ瀽鏂规硶 & 璺緞
  int firstSpace = request.indexOf(' ');
  int secondSpace = request.indexOf(' ', firstSpace + 1);
  if (firstSpace < 0 || secondSpace < 0) { client.stop(); return; }

  String method = request.substring(0, firstSpace);
  String path = request.substring(firstSpace + 1, secondSpace);

  // 鎻愬彇 body
  String body;
  if (contentLength > 0) {
    int headerEnd = request.indexOf("\r\n\r\n");
    if (headerEnd < 0) headerEnd = request.indexOf("\n\n");
    if (headerEnd >= 0) {
      body = request.substring(headerEnd + 4, headerEnd + 4 + contentLength);
    }
  }

  handleRequest(client, method, path, body);
  client.stop();
}
