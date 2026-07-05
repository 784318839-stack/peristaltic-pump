/******************************************************************************
 * serial_commands.cpp - USB CDC 串口命令实现
 *
 * 帧格式: 换行分隔的 JSON (兼容 \n, \r\n)
 * 最大单帧: 512 字节
 *
 * 用法 (PC 端):
 *   echo '{"cmd":"start"}' > /dev/ttyACM0     (Linux)
 *   echo {"cmd":"start"} > COM3                (Windows)
 *
 *   Python:
 *     import serial
 *     s = serial.Serial('COM3', 115200)
 *     s.write(b'{"cmd":"start"}\n')
 *     print(s.readline())  # 读取响应
 ******************************************************************************/
#include "serial_commands.h"
#include "command_protocol.h"

#define SERIAL_BAUD 115200
#define SERIAL_BUF_SIZE 512

static char serialBuffer[SERIAL_BUF_SIZE];
static int  serialLen = 0;

void initSerialCommands() {
  // Serial.begin 已在 setup() 中调用，此处只检查就绪并发送 hello
  unsigned long start = millis();
  while (!Serial && millis() - start < 2000) {
    delay(10);
  }
  Serial.println("{\"type\":\"hello\",\"device\":\"PeristalticPump\",\"version\":\"2.0\"}");
}

void processSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();

    // 换行 = 帧结束
    if (c == '\n' || c == '\r') {
      // 跳过连续的 \r\n
      if (c == '\r' && Serial.peek() == '\n') {
        Serial.read();  // 吃掉 \n
      }

      if (serialLen > 0) {
        serialBuffer[serialLen] = '\0';

        // 解析并执行
        const char* response = parseAndExecute(serialBuffer);
        if (response && response[0]) {
          Serial.println(response);
        }

        serialLen = 0;
      }
    } else if (serialLen < SERIAL_BUF_SIZE - 1) {
      serialBuffer[serialLen++] = c;
    }
    // 缓冲区满则丢弃 (防止恶意数据撑爆)
  }
}

// ============================================================================
//                        硬件 UART (PC 直连)
// ============================================================================
// GPIO 44=RX, 43=TX, 115200 baud 8N1
// 用 USB-TTL 转接板连接到电脑, 与 USB CDC 并行工作
// 协议与 USB CDC 完全一致: JSON 换行分隔

static HardwareSerial hwUart(1);  // UART1
static char hwUartBuf[512];
static int  hwUartLen = 0;

void initHardwareUart() {
  hwUart.begin(115200, SERIAL_8N1, HW_UART_RX, HW_UART_TX);
  // 等就绪后发 hello
  unsigned long start = millis();
  while (!hwUart && millis() - start < 1000) { delay(5); }
  hwUart.println("{\"type\":\"hello\",\"device\":\"PeristalticPump\",\"version\":\"2.0\",\"port\":\"UART1\"}");
}

void processHardwareUart() {
  while (hwUart.available()) {
    char c = hwUart.read();

    if (c == '\n' || c == '\r') {
      if (c == '\r' && hwUart.peek() == '\n') {
        hwUart.read();
      }

      if (hwUartLen > 0) {
        hwUartBuf[hwUartLen] = '\0';
        const char* response = parseAndExecute(hwUartBuf);
        if (response && response[0]) {
          hwUart.println(response);
        }
        hwUartLen = 0;
      }
    } else if (hwUartLen < 511) {
      hwUartBuf[hwUartLen++] = c;
    }
  }
}
