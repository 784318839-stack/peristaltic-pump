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
#include <WiFi.h>
#include <ArduinoJson.h>

// ============================================================================
//                            HTTP 鏈嶅姟鍣?
// ============================================================================
static WiFiServer server(80);

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
//                            WiFi 鎵弿 (寮傛, 涓嶉樆濉炰富寰幆)
// ============================================================================

static unsigned long lastScanDoneMs = 0;

static String buildScanResultJson(int n) {
  String json = "{\"networks\":[";
  for (int i = 0; i < n && i < 20; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"";
    String ssid = WiFi.SSID(i);
    /* 绠€鍗曡浆涔?JSON 涓殑寮曞彿 */
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

  // GET /api/cmd?c=xxx&v=yyy&s=zzz&m=mmm&i=iii -> 鍛戒护
  if (method == "GET" && path.startsWith("/api/cmd")) {
    String cmd = getQueryParam(path, "c");
    String val = getQueryParam(path, "v");
    String slot = getQueryParam(path, "s");
    String mode = getQueryParam(path, "m");
    String idx = getQueryParam(path, "i");

    if (cmd.length() == 0) {
      sendJson(client, 400, "{\"ok\":false,\"error\":\"Missing cmd\"}");
      return;
    }

    String params;
    if (val.length() > 0) params += "\"value\":" + val;
    if (slot.length() > 0) {
      if (params.length() > 0) params += ",";
      params += "\"slot\":" + slot;
    }
    if (mode.length() > 0) {
      if (params.length() > 0) params += ",";
      params += "\"mode\":\"" + mode + "\"";
    }
    if (idx.length() > 0) {
      if (params.length() > 0) params += ",";
      params += "\"index\":" + idx;
    }

    String jsonCmd;
    if (params.length() > 0) {
      jsonCmd = "{\"cmd\":\"" + cmd + "\",\"params\":{" + params + "}}";
    } else {
      jsonCmd = "{\"cmd\":\"" + cmd + "\",\"params\":{}}";
    }

    const char* resp = parseAndExecute(jsonCmd.c_str());
    sendJson(client, 200, resp);
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

  // GET /api/scan -> WiFi scan (sync, 3s debounce)
  if (method == "GET" && path.startsWith("/api/scan")) {
    if (pump.state == RUNNING || pump.state == PAUSED) {
      sendJson(client, 200, "{\"ok\":false,\"error\":\"Pump busy\",\"done\":true,\"networks\":[]}");
      return;
    }

    /* 3s debounce: prevent duplicate sync scans from frontend polling */
    if (millis() - lastScanDoneMs < 3000) {
      sendJson(client, 200, "{\"ok\":true,\"done\":true,\"networks\":[]}");
      return;
    }

    WiFi.scanDelete();
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
      sendJson(client, 200, "{\"ok\":true,\"done\":false,\"networks\":[]}");
      return;
    }

    n = WiFi.scanNetworks(false, false, false, 120);
    WiFi.scanDelete();
    lastScanDoneMs = millis();

    if (n > 0) {
      String json = "{\"ok\":true,\"done\":true,";
      json += buildScanResultJson(n).substring(1);
      sendJson(client, 200, json.c_str());
    } else {
      sendJson(client, 200, "{\"ok\":true,\"done\":true,\"networks\":[]}");
    }
    return;
  }
startsWith("/api/info")) {
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
      request += c;
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
