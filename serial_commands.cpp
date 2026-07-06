/******************************************************************************
 * serial_commands.cpp — USB CDC & 硬件 UART 串口命令实现
 *
 * 帧格式 : 换行分隔的 JSON ( 兼容 \n , \r\n )
 * 最大单帧 : 512 字节
 *
 * PC 端用法 :
 *   echo '{"cmd":"start"}' > /dev/ttyACM0     ( Linux )
 *   echo {"cmd":"start"} > COM3                ( Windows )
 *   Python: s.write( b'{"cmd":"start"}\n' )
 ******************************************************************************/
#include "serial_commands.h"
#include "command_protocol.h"
#include "pump_shared.h"

#define SERIAL_BAUD 115200
#define SERIAL_BUF_SIZE 512

static char serialBuffer[SERIAL_BUF_SIZE];
static int  serialLen = 0;

void initSerialCommands() {
  unsigned long start = millis();
  while ( !Serial && millis() - start < 2000 ) { delay( 10 ); }
  Serial.println( "{\"type\":\"hello\",\"device\":\"PeristalticPump\",\"version\":\"2.0\"}" );
}

void processSerialCommands() {
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
//  硬件 UART1 ( GPIO 21 = RX , 47 = TX ) — PC 直连 , 协议与 USB CDC 一致
// ============================================================================
static HardwareSerial hwUart( 1 );
static char hwUartBuf[512];
static int  hwUartLen = 0;

void initHardwareUart() {
  hwUart.begin( 115200, SERIAL_8N1, HW_UART_RX, HW_UART_TX );
  unsigned long start = millis();
  while ( !hwUart && millis() - start < 1000 ) { delay( 5 ); }
  hwUart.println( "{\"type\":\"hello\",\"device\":\"PeristalticPump\",\"version\":\"2.0\",\"port\":\"UART1\"}" );
}

void processHardwareUart() {
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
    } else if ( hwUartLen < 511 ) {
      hwUartBuf[hwUartLen++] = c;
    }
  }
}
