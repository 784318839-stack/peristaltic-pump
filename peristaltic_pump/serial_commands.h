/******************************************************************************
 * serial_commands.h — USB CDC 串口命令
 *
 * 通过 USB 串口收发 JSON 命令 , 换行 `\n` 分隔每帧
 * 与 HTTP / BLE 共用同一套 parseAndExecute() 引擎
 * 同时支持硬件 UART1 ( GPIO 21 = RX , 47 = TX ) 供 USB-TTL 直连
 ******************************************************************************/
#ifndef SERIAL_COMMANDS_H
#define SERIAL_COMMANDS_H

#include <Arduino.h>

// 初始化串口 PSRAM 缓冲区 (必须在 setup 中调用, 先于 initSerialCommands)
void initSerialBuffers();

// 初始化 USB CDC 串口 ( 打印 READY 横幅 )
void initSerialCommands();

// 非阻塞轮询 USB CDC 串口 , 读到完整一行后解析执行 ( loop 中调用 )
void processSerialCommands();

// ----- 硬件 UART1 ( GPIO 21 = RX , 47 = TX , 115200 , USB-TTL 直连 PC ) -----
void initHardwareUart();
void processHardwareUart();

#endif // SERIAL_COMMANDS_H
