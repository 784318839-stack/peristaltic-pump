/******************************************************************************
 * serial_commands.h - USB CDC 串口命令
 *
 * 通过 USB 串口收发 JSON 命令, 换行 (`\n`) 分隔每帧
 * 与 WebSocket 使用同一套 parseAndExecute() 引擎
 ******************************************************************************/
#ifndef SERIAL_COMMANDS_H
#define SERIAL_COMMANDS_H

#include <Arduino.h>

// 初始化串口 (打印 READY 横幅, 设置波特率)
void initSerialCommands();

// 非阻塞轮询串口, 读到完整一行后解析执行 (loop 中调用)
void processSerialCommands();

#endif // SERIAL_COMMANDS_H
