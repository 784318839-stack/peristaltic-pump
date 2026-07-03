/******************************************************************************
 * web_handlers.cpp - HTTP 服务实现 (WiFiServer, 无 AsyncTCP 依赖)
 *
 * 使用 ESP32 内置 WiFiServer/WiFiClient, 不依赖 AsyncTCP/ESPAsyncWebServer
 * 彻底解决 Core 3.x 的 tcp_alloc LWIP 锁冲突
 *
 * Web UI 通过 HTTP 轮询 /api/status 获取实时数据
 * 命令通过 /api/cmd?c=xxx 发送
 ******************************************************************************/
#include "web_handlers.h"
#include "command_protocol.h"
#include "wifi_manager.h"
#include "pump_shared.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// ============================================================================
//                            HTTP 服务器
// ============================================================================
static WiFiServer server(80);

// 内嵌 Web UI (编译进固件, 无需 LittleFS)
#include "web_ui_gen.h"

// ============================================================================
//                            HTTP 请求处理
// ============================================================================

// 发送 JSON 响应
static void sendJson(WiFiClient &client, int code, const char* json) {
  client.printf("HTTP/1.1 %d OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n%s", code, json);
}

// 发送 HTML 响应
static void sendHtml(WiFiClient &client, int code, const char* html) {
  client.printf("HTTP/1.1 %d OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n%s", code, html);
}

// 从 URL 中提取查询参数值
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

// 处理单个 HTTP 请求
static void handleRequest(WiFiClient &client, const String &method, const String &path) {
  // GET / 或 /index.html -> Web UI
  if (method == "GET" && (path == "/" || path.startsWith("/index.html"))) {
    sendHtml(client, 200, WEB_UI);
    return;
  }

  // GET /manifest.json -> PWA manifest
  if (method == "GET" && path.startsWith("/manifest.json")) {
    sendJson(client, 200,
      "{\"name\":\"蠕动泵控制器\",\"short_name\":\"PumpCtrl\","
      "\"start_url\":\"/\",\"display\":\"standalone\","
      "\"background_color\":\"#0d1117\",\"theme_color\":\"#0d1117\"}");
    return;
  }

  // GET /api/status -> 遥测 JSON
  if (method == "GET" && path.startsWith("/api/status")) {
    const char* json = buildTelemetryJson();
    String resp = "{\"type\":\"telemetry\",";
    resp += (json + 1);  // 跳过 buildTelemetryJson 的起始 '{'
    sendJson(client, 200, resp.c_str());
    return;
  }

  // GET /api/cmd?c=xxx&v=yyy&s=zzz&m=mmm&i=iii -> 命令
  if (method == "GET" && path.startsWith("/api/cmd")) {
    String cmd = getQueryParam(path, "c");
    String val = getQueryParam(path, "v");
    String slot = getQueryParam(path, "s");
    String mode = getQueryParam(path, "m");
    String idx = getQueryParam(path, "i");

    // 构建 JSON 命令
    String jsonCmd;
    if (cmd.length() == 0) {
      sendJson(client, 400, "{\"ok\":false,\"error\":\"Missing cmd\"}");
      return;
    }

    // 构建 params
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

    if (params.length() > 0) {
      jsonCmd = "{\"cmd\":\"" + cmd + "\",\"params\":{" + params + "}}";
    } else {
      jsonCmd = "{\"cmd\":\"" + cmd + "\",\"params\":{}}";
    }

    const char* resp = parseAndExecute(jsonCmd.c_str());
    sendJson(client, 200, resp);
    return;
  }

  // 404
  sendJson(client, 404, "{\"ok\":false,\"error\":\"Not found\"}");
}

// ============================================================================
//                            初始化 & 客户端轮询
// ============================================================================

void initWebServer() {
  server.begin();
}

void handleWebClients() {
  WiFiClient client = server.accept();
  if (!client) return;

  // 读取 HTTP 请求的第一行 (最多等 50ms)
  unsigned long timeout = millis() + 50;
  String request;
  while (client.connected() && millis() < timeout) {
    if (client.available()) {
      char c = client.read();
      request += c;
      if (request.endsWith("\r\n\r\n")) break;
      if (request.endsWith("\n\n")) break;
      timeout = millis() + 50;
    }
  }

  // 快速消费剩余数据
  while (client.available()) client.read();

  // 解析方法 & 路径
  int firstSpace = request.indexOf(' ');
  int secondSpace = request.indexOf(' ', firstSpace + 1);
  if (firstSpace < 0 || secondSpace < 0) { client.stop(); return; }
  String method = request.substring(0, firstSpace);
  String path = request.substring(firstSpace + 1, secondSpace);

  handleRequest(client, method, path);
  client.stop();
}
