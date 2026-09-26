/******************************************************************************
 * web_handlers.cpp - HTTP 服务实现 (WiFiServer, 无 AsyncTCP 依赖)
 *
 * 路由:
 *   GET  /              -> Web UI (内嵌 web_ui_gen.h)
 *   GET  /manifest.json -> PWA 清单
 *   GET  /api/status    -> JSON 遥测
 *   GET  /api/cmd?c=xxx -> 命令
 *   POST /api/wifi      -> 保存 WiFi 配置 (JSON body)
 *   GET  /api/scan      -> 扫描附近 WiFi (同步, ~8s, 结果缓存 15s)
 ******************************************************************************/
#include "web_handlers.h"
#include "command_protocol.h"
#include "wifi_manager.h"
#include "pump_shared.h"
#include "pump_state.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>

// ============================================================================
//                            HTTP 服务器
// ============================================================================
static WiFiServer server(80);

// 内嵌 Web UI
#include "web_ui_gen.h"

// 单个请求的最大字节数 (headers + body)。POST /api/wifi 的 body 只有 ~150B,
// headers ~400B, 2KB 绰绰有余。
#define REQ_BUF_SIZE 2048

// ============================================================================
//                            辅助函数
// ============================================================================

// RFC 7230 里理由短语只是给人看的, 但发 "HTTP/1.1 404 OK" 会让人误判。
static const char* reasonPhrase(int code) {
  switch (code) {
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    default:  return "OK";
  }
}

// 必须带 Content-Length: 否则浏览器只能靠连接关闭判断 body 结束, 一旦 stop()
// 早于 lwIP 真正把数据发出去, 24.5KB 的 WEB_UI 就会被截断。
static void sendResponse(WiFiClient &client, int code, const char* contentType, const char* body) {
  size_t len = strlen(body);
  char head[160];
  snprintf(head, sizeof(head),
           "HTTP/1.1 %d %s\r\n"
           "Content-Type: %s\r\n"
           "Content-Length: %u\r\n"
           "Connection: close\r\n\r\n",
           code, reasonPhrase(code), contentType, (unsigned)len);
  client.print(head);
  client.write((const uint8_t*)body, len);
}

static void sendJson(WiFiClient &client, int code, const char* json) {
  sendResponse(client, code, "application/json", json);
}

static void sendHtml(WiFiClient &client, int code, const char* html) {
  sendResponse(client, code, "text/html; charset=utf-8", html);
}

// 只认 '?' 之后、以 '&' 分隔的 key= 片段。
// 原实现直接 url.indexOf("c="), 于是 /api/cmd?xc=start 也会命中。
static String getQueryParam(const char* url, const char* key) {
  const char* q = strchr(url, '?');
  if (!q) return "";
  String query(q + 1);
  int sp = query.indexOf(' ');            // 去掉 " HTTP/1.1" 尾巴
  if (sp >= 0) query = query.substring(0, sp);

  String k = String(key) + "=";
  int pos = 0;
  while (pos < (int)query.length()) {
    int amp = query.indexOf('&', pos);
    String pair = (amp < 0) ? query.substring(pos) : query.substring(pos, amp);
    if (pair.startsWith(k)) return pair.substring(k.length());
    if (amp < 0) break;
    pos = amp + 1;
  }
  return "";
}

// 从 headers 中提取 Content-Length
static int getContentLength(const char* headers) {
  const char* p = strstr(headers, "Content-Length:");
  if (!p) p = strstr(headers, "content-length:");
  if (!p) return 0;
  return atoi(p + 15);
}

// 返回 body 的起始偏移, 找不到返回 -1。
// 必须同时正确处理 \r\n\r\n (4 字节) 和 \n\n (2 字节) —— 原实现两种都接受,
// 但之后一律按 4 字节算, LF-only 客户端的 body 会被吃掉前 2 个字节导致 JSON 解析失败。
static int findHeaderEnd(const char* s) {
  const char* p = strstr(s, "\r\n\r\n");
  if (p) return (int)(p - s) + 4;
  p = strstr(s, "\n\n");
  if (p) return (int)(p - s) + 2;
  return -1;
}

// ============================================================================
//                            WiFi scan (sync, reliable)
//
//  改用同步扫描: ESP32 Arduino 异步扫描在 AP+STA 模式下 _scanStatus
//  状态机有 bug, scanDelete() 后 scanNetworks() 仍返回 -1。
//  同步扫描阻塞约 8 秒但可靠; 泵控由 MCPWM+PCNT 硬件发脉冲, 不受 loop 阻塞影响。
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
//                            请求路由
// ============================================================================

static void handleRequest(WiFiClient &client, const char* method,
                          const char* path, const char* body) {
  bool isGet  = (strcmp(method, "GET") == 0);
  bool isPost = (strcmp(method, "POST") == 0);

  // GET / 或 /index.html -> Web UI
  if (isGet && (strcmp(path, "/") == 0 || strncmp(path, "/index.html", 11) == 0)) {
    sendHtml(client, 200, WEB_UI);
    return;
  }

  // GET /manifest.json
  if (isGet && strncmp(path, "/manifest.json", 14) == 0) {
    sendJson(client, 200,
      "{\"name\":\"蠕动泵控制器\",\"short_name\":\"PumpCtrl\","
      "\"start_url\":\"/\",\"display\":\"standalone\","
      "\"background_color\":\"#0d1117\",\"theme_color\":\"#0d1117\"}");
    return;
  }

  // GET /api/status -> 遥测
  if (isGet && strncmp(path, "/api/status", 11) == 0) {
    sendJson(client, 200, buildTelemetryJson());
    return;
  }

  // GET /api/cmd?c=xxx&v=yyy&s=zzz&m=mmm&i=iii -> 命令
  if (isGet && strncmp(path, "/api/cmd", 8) == 0) {
    String cmd  = getQueryParam(path, "c");
    String val  = getQueryParam(path, "v");
    String slot = getQueryParam(path, "s");
    String mode = getQueryParam(path, "m");
    String idx  = getQueryParam(path, "i");

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

  // POST /api/wifi -> 保存 WiFi 配置
  if (isPost && strncmp(path, "/api/wifi", 9) == 0) {
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

    // 必须先把响应完整发出去并优雅关闭连接, 再重启 WiFi。
    // restartWiFi() 会 softAPdisconnect(true), 直接拆掉承载这条响应的 TCP 连接;
    // 连接被硬拆就会让前端 fetch() reject, 保存成功也显示成失败。
    sendJson(client, 200, "{\"ok\":true,\"saved\":true}");
    client.flush();
    client.stop();
    delay(300);

    restartWiFi();
    return;
  }

  // GET /api/scan -> WiFi 同步扫描 (阻塞 ~8s, 结果缓存 15s)
  if (isGet && strncmp(path, "/api/scan", 9) == 0) {
    // ANTI_DRIP 也要挡: 此时电机正在反转回吸, 而扫描会把 loop() 冻住约 8 秒,
    // pump_machine_tick() 停摆 -> 回吸结束后状态卡在 ANTI_DRIP。
    if (pump.state == RUNNING || pump.state == PAUSED || pump.state == ANTI_DRIP) {
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

    // 1) Free radio: disconnect STA if it's trying to connect
    wl_status_t sta = WiFi.status();
    bool staWasConnecting = false;
    if (sta != WL_CONNECTED && sta != WL_IDLE_STATUS) {
      staWasConnecting = true;
      WiFi.disconnect(true, true);  // turn off STA radio
      delay(100);
      Serial.println("[SCAN] STA was connecting, disconnected");
    }

    // 2) Clear stale scan state
    WiFi.scanDelete();
    esp_wifi_clear_ap_list();
    delay(100);

    // 3) Sync active scan
    int n = WiFi.scanNetworks(false, false, false, 300);
    Serial.printf("[SCAN] sync scan result: %d (sta=%d)\n", n, sta);

    // 4) STA-only fallback if AP+STA scan failed
    if (n <= 0) {
      String apSSID = WiFi.softAPSSID();
      Serial.println("[SCAN] retrying in STA-only mode...");

      WiFi.mode(WIFI_STA);
      delay(100);
      esp_wifi_clear_ap_list();
      delay(50);

      n = WiFi.scanNetworks(false, false, false, 300);
      Serial.printf("[SCAN] STA-only sync scan: %d\n", n);

      // Restore AP+STA
      WiFi.mode(WIFI_AP_STA);
      delay(100);
      if (apSSID.length() > 0) {
        WiFi.softAPdisconnect(true);  // ensure clean state
        delay(30);
        WiFi.softAP(apSSID.c_str(), WIFI_AP_PASSWORD, 1, 0, 2);
        delay(200);
        esp_wifi_set_max_tx_power(80);
        esp_wifi_set_ps(WIFI_PS_NONE);
      }
    }

    // 5) Reconnect STA if disconnected earlier
    if (staWasConnecting) {
      WiFiConfig cfg;
      if (loadWiFiConfig(cfg) && cfg.mode == WIFI_MODE_STA_FALLBACK && strlen(cfg.ssid) > 0)
        WiFi.begin(cfg.ssid, cfg.pass);
    }

    // 6) Build and cache result
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

  // 404
  sendJson(client, 404, "{\"ok\":false,\"error\":\"Not found\"}");
}

// ============================================================================
//                            客户端处理
// ============================================================================

void initWebServer() {
  server.begin();
}

void handleWebClients() {
  WiFiClient client = server.accept();
  if (!client) return;

  // 用固定缓冲成块读, 不要逐字节 String += —— Arduino String::concat 每次精确
  // 重分配, 一个 500 字节的请求就是 500 次 malloc/free, 长期运行会加剧堆碎片。
  static char buf[REQ_BUF_SIZE];
  int len = 0;
  int headerEnd = -1;
  int contentLength = 0;
  unsigned long deadline = millis() + 200;

  buf[0] = '\0';
  while (client.connected() && millis() < deadline && len < REQ_BUF_SIZE - 2) {
    int avail = client.available();
    if (avail <= 0) { delay(1); continue; }

    int room = REQ_BUF_SIZE - 2 - len;
    int n = client.read((uint8_t*)buf + len, (uint16_t)(avail < room ? avail : room));
    if (n <= 0) break;
    len += n;
    buf[len] = '\0';
    deadline = millis() + 200;            // 每次读到数据就重置超时

    if (headerEnd < 0) {
      headerEnd = findHeaderEnd(buf);
      if (headerEnd >= 0) {
        contentLength = getContentLength(buf);
        if (contentLength <= 0) break;    // 无 body, 结束
      }
    } else if (len - headerEnd >= contentLength) {
      break;                              // body 读完了
    }
  }

  if (len == 0) { client.stop(); return; }

  // 解析请求行: METHOD SP PATH SP VERSION
  char* firstSpace = strchr(buf, ' ');
  if (!firstSpace) { client.stop(); return; }
  *firstSpace = '\0';
  const char* method = buf;
  char* path = firstSpace + 1;
  char* secondSpace = strchr(path, ' ');
  if (!secondSpace) { client.stop(); return; }
  *secondSpace = '\0';

  // body 按 Content-Length 截断并就地补 NUL, 避免把管线化的后续请求当成本次 body
  const char* body = "";
  if (headerEnd >= 0 && contentLength > 0) {
    int bodyEnd = headerEnd + contentLength;
    if (bodyEnd > len) bodyEnd = len;
    buf[bodyEnd] = '\0';
    body = buf + headerEnd;
  }

  handleRequest(client, method, path, body);
  client.flush();                         // 确保数据真的发出去再关, 否则大响应会被截断
  client.stop();
}
