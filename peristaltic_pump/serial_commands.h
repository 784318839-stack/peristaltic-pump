/******************************************************************************
 * serial_commands.h — 串口命令（UART0 + 硬件 UART1）
 *
 * 两路都是换行 `\n` 分隔的 JSON 帧, 与 HTTP 共用同一套 parseAndExecute() 引擎:
 *   - `Serial`  = **UART0 (GPIO43 TX / 44 RX)**。platformio.ini 钉死了
 *     `-DARDUINO_USB_CDC_ON_BOOT=0`, 于是 core 3.3.11 的 HardwareSerial.h:449-451
 *     走 `#define Serial Serial0` —— **不是原生 USB CDC**。插 USB 看不到输出,
 *     必须接 USB-TTL 到 GPIO43/44。(旧注释写「USB CDC」是 CDC 还开着时的遗留)
 *   - `hwUart`  = UART1 (GPIO21 RX / 47 TX), 115200 8N1, 供第二路 USB-TTL 直连
 ******************************************************************************/
#ifndef SERIAL_COMMANDS_H
#define SERIAL_COMMANDS_H

#include <Arduino.h>

// 初始化串口 PSRAM 缓冲区 (必须在 setup 中调用, 先于 initSerialCommands)
void initSerialBuffers();

// 初始化 UART0 (Serial) 并发一帧 hello (含 FW_VERSION)
void initSerialCommands();

// 非阻塞轮询 UART0 (Serial), 读到完整一行后解析执行 ( loop 中调用 )
void processSerialCommands();

// ----- 硬件 UART1 ( GPIO 21 = RX , 47 = TX , 115200 , USB-TTL 直连 PC ) -----
void initHardwareUart();
void processHardwareUart();

#endif // SERIAL_COMMANDS_H
