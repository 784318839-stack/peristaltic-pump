/******************************************************************************
 * serial_commands.cpp — UART0 (Serial) & 硬件 UART1 串口命令实现
 *
 * 帧格式 : 换行分隔的 JSON ( 兼容 \n , \r\n )
 * 最大单帧 : 1024 字节 (SERIAL_BUF_SIZE)
 *
 * 缓冲区分配在 PSRAM 中, 减少内部 SRAM 占用
 *
 * 注意 `Serial` 在本构建里是 **UART0 (GPIO43/44)**, 不是原生 USB CDC ——
 * platformio.ini 钉了 -DARDUINO_USB_CDC_ON_BOOT=0, 详见 serial_commands.h 头部。
 ******************************************************************************/
#include "serial_commands.h"
#include "command_protocol.h"
#include "pump_shared.h"
#include "version.h"
#include <esp_heap_caps.h>

#define SERIAL_BAUD 115200
#define SERIAL_BUF_SIZE 1024

// PSRAM 优先分配的缓冲区
static char* serialBuffer = nullptr;
static int   serialLen = 0;

static char* hwUartBuf = nullptr;
static int   hwUartLen = 0;

// ---- 初始化 ----

void initSerialBuffers() {
  serialBuffer = (char*)heap_caps_malloc(SERIAL_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!serialBuffer) serialBuffer = (char*)malloc(SERIAL_BUF_SIZE);
  if (serialBuffer) serialBuffer[0] = '\0';

  hwUartBuf = (char*)heap_caps_malloc(SERIAL_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!hwUartBuf) hwUartBuf = (char*)malloc(SERIAL_BUF_SIZE);
  if (hwUartBuf) hwUartBuf[0] = '\0';
}

void initSerialCommands() {
  // 下面这个等待在**本构建里是死等**, 但刻意保留:
  //   CDC=0 (当前): `Serial` 就是 `Serial0`, 而 HardwareSerial::operator bool() 的实现是
  //     `uartIsDriverInstalled(_uart)` (core 3.3.11 HardwareSerial.cpp:594-596), begin()
  //     之后恒为 true ⇒ 循环第一次判断就退出, 一毫秒都不等。
  //   CDC=1 (若将来打开): `Serial` 变成 HWCDC, operator bool() 表示「主机已连接」,
  //     这个最多 2 s 的等待就真的有用了 —— 否则 hello 帧会在枚举完成前发出去、直接丢掉。
  // 删掉它省下的是 0 ms, 代价是将来切 CDC 时静默丢掉 hello。不划算。
  unsigned long start = millis();
  while ( !Serial && millis() - start < 2000 ) { delay( 10 ); }
  Serial.println( "{\"type\":\"hello\",\"device\":\"PeristalticPump\",\"version\":\"" FW_VERSION "\"}" );
}

void processSerialCommands() {
  if (!serialBuffer) return;
  while ( Serial.available() ) {
    char c = Serial.read();
    if ( c == '\n' || c == '\r' ) {
      if ( c == '\r' && Serial.peek() == '\n' ) Serial.read();
      if ( serialLen > 0 ) {
        serialBuffer[serialLen] = '\0';
        const char* response = parseAndExecute( serialBuffer );
        if ( response && response[0] ) Serial.println( response );
        serialLen = 0;
      }
    } else if ( serialLen < SERIAL_BUF_SIZE - 1 ) {
      serialBuffer[serialLen++] = c;
    }
  }
}

// ============================================================================
//  硬件 UART1 ( GPIO 21 = RX , 47 = TX )
// ============================================================================
static HardwareSerial hwUart( 1 );

void initHardwareUart() {
  // 必须在 begin() 之前调用。默认 RX 环形缓冲只有 256 字节, 而单帧上限是
  // SERIAL_BUF_SIZE; 115200 下 256B 只有约 22ms 余量, 而 parseAndExecute()
  // (JSON 解析 + 生成响应) 可能更久 -> 长命令帧会被截断。
  hwUart.setRxBufferSize( SERIAL_BUF_SIZE );
  hwUart.begin( 115200, SERIAL_8N1, HW_UART_RX, HW_UART_TX );
  // 这里**刻意没有** initSerialCommands() 里那种 while(!hwUart && ...) 等待。
  // hwUart 是 HardwareSerial(1), 任何配置下都不会变成 HWCDC, 而
  // HardwareSerial::operator bool() == uartIsDriverInstalled(), begin() 之后恒为 true
  // ⇒ 那种等待在本构建和任何未来构建里都是纯死代码, 连 begin() 失败都救不了
  // (真失败的话等 1 s 之后照样往死端口打 hello)。与 Serial 那处的区别就在这个
  // "将来有没有可能变得有用", 别当成疏漏又加回来。
  hwUart.println( "{\"type\":\"hello\",\"device\":\"PeristalticPump\",\"version\":\"" FW_VERSION "\",\"port\":\"UART1\"}" );
}

void processHardwareUart() {
  if (!hwUartBuf) return;
  while ( hwUart.available() ) {
    char c = hwUart.read();
    if ( c == '\n' || c == '\r' ) {
      if ( c == '\r' && hwUart.peek() == '\n' ) hwUart.read();
      if ( hwUartLen > 0 ) {
        hwUartBuf[hwUartLen] = '\0';
        const char* response = parseAndExecute( hwUartBuf );
        if ( response && response[0] ) hwUart.println( response );
        hwUartLen = 0;
      }
    } else if ( hwUartLen < SERIAL_BUF_SIZE - 1 ) {
      hwUartBuf[hwUartLen++] = c;
    }
  }
}
