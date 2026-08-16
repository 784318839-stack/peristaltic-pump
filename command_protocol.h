/******************************************************************************
 * command_protocol.h - 远程命令协议
 *
 * 统一 JSON 命令入口:
 *   WiFi HTTP / USB Serial / 硬件 UART 共用同一套协议
 *   所有通道都在 loop() 单线程上下文中串行执行 parseAndExecute(), 无竞态
 *
 * 命令格式:
 *   -> {"cmd":"start", "params":{...}}
 *   <- {"type":"response","id":"<cmd>","ok":true/false,"error":"..."}
 *   <- {"type":"telemetry","ts":...,"state":"IDLE","flow":50,...}
 ******************************************************************************/
#ifndef COMMAND_PROTOCOL_H
#define COMMAND_PROTOCOL_H

#include <Arduino.h>

// 解析并执行一条 JSON 命令 (可用于串口直接调用)
// 返回 JSON 响应字符串 (静态缓冲区, 下次调用覆盖)
const char* parseAndExecute(const char* json);

// 初始化 PSRAM 缓冲区 (必须在 setup 中调用)
void initTelemetryBuffer();
void initResponseBuffer();

// 生成当前完整状态的 JSON 遥测字符串
// 返回 PSRAM 缓冲区指针, 调用方负责发送
const char* buildTelemetryJson();

#endif // COMMAND_PROTOCOL_H
