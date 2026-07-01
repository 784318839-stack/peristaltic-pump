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
