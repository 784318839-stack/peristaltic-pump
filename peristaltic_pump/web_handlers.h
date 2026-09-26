/******************************************************************************
 * web_handlers.h - HTTP 服务 (WiFiServer, 无外部依赖)
 *
 * HTTP 路由:
 *   GET  /              -> 内嵌 HTML 页面 (Web UI)
 *   GET  /api/status    -> JSON 遥测快照
 *   GET  /api/cmd?c=xxx -> 发送命令 (替代 WebSocket)
 *   POST /api/wifi      -> 设置 WiFi 配置
 *   GET  /api/scan      -> 扫描附近 WiFi 网络
 ******************************************************************************/
#ifndef WEB_HANDLERS_H
#define WEB_HANDLERS_H

#include <Arduino.h>

// 初始化 Web 服务器 (setup 中调用, 在 initWiFi 之后)
void initWebServer();

// 处理 HTTP 请求 (loop 中每次调用, 非阻塞)
void handleWebClients();

#endif // WEB_HANDLERS_H
