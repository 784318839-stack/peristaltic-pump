/******************************************************************************
 * serial_commands.cpp — USB CDC & 硬件 UART 串口命令实现
 *
 * 帧格式 : 换行分隔的 JSON ( 兼容 \n , \r\n )
 * 最大单帧 : 512 字节
 *
 * 缓冲区分配在 PSRAM 中, 减少内部 SRAM 占用
 ******************************************************************************/
#include "serial_commands.h"
#include "command_protocol.h"
#include "pump_shared.h"
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
  unsigned long start = millis();
  while ( !Serial && millis() - start < 2000 ) { delay( 10 ); }
  Serial.println( "{\"type\":\"hello\",\"device\":\"PeristalticPump\",\"version\":\"2.3.2\"}" );
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
  hwUart.begin( 115200, SERIAL_8N1, HW_UART_RX, HW_UART_TX );
  unsigned long start = millis();
  while ( !hwUart && millis() - start < 1000 ) { delay( 5 ); }
  hwUart.println( "{\"type\":\"hello\",\"device\":\"PeristalticPump\",\"version\":\"2.3.2\",\"port\":\"UART1\"}" );
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
