/******************************************************************************
 * command_protocol.h - 远程命令协议
 *
 * 统一 JSON 命令入口:
 *   WebSocket 和 USB Serial 共用同一套协议
 *   所有命令通过 FreeRTOS 队列在 loop() 上下文中执行, 无需互斥锁
 *
 * 命令格式:
 *   -> {"cmd":"start", "params":{...}}
 *   <- {"type":"response","id":"<cmd>","ok":true/false,"error":"..."}
 *   <- {"type":"telemetry","ts":...,"state":"IDLE","flow":50,...}
 ******************************************************************************/
#ifndef COMMAND_PROTOCOL_H
#define COMMAND_PROTOCOL_H

#include <Arduino.h>

// ----- 命令队列 -----
#define CMD_QUEUE_SIZE 16           // 最多缓存 16 条待处理命令
#define CMD_JSON_MAX   384          // 单条 JSON 最大长度

struct CommandMsg {
  char json[CMD_JSON_MAX];
  uint32_t clientId;               // WebSocket 客户端 ID (0 = 无客户端, 如串口)
};

// 命令响应回调: clientId->发给谁, response->JSON 响应字符串
typedef void (*CommandResponseCallback)(uint32_t clientId, const char* response);

// 初始化 FreeRTOS 命令队列 (setup 中调用)
void initCommandQueue();

// 设置响应回调 (由 web_handlers 注册, 用于将命令结果发回 WebSocket 客户端)
void setCommandResponseCallback(CommandResponseCallback cb);

// 入队一条 JSON 命令 (WebSocket 回调 / 串口线程均可调用, 线程安全)
// 返回 true 表示入队成功, false 表示队列满
bool enqueueCommand(const char* json);

// 入队一条 JSON 命令并关联 WebSocket 客户端 ID (用于将响应发回特定客户端)
bool enqueueCommandClient(const char* json, uint32_t clientId);

// 从队列取出所有待处理命令并执行 (loop 中调用)
void processCommandQueue();

// 解析并执行一条 JSON 命令 (可用于串口直接调用)
// 返回 JSON 响应字符串 (静态缓冲区, 下次调用覆盖)
const char* parseAndExecute(const char* json);

// 生成当前完整状态的 JSON 遥测字符串
// 返回静态缓冲区, 调用方负责发送
const char* buildTelemetryJson();

#endif // COMMAND_PROTOCOL_H
