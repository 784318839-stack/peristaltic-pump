/******************************************************************************
 * sw_uart.h — 单线软件模拟半双工 UART (纯 GPIO 比特冲击)
 *
 * 特性:
 *   - 单引脚 (TX/RX 共用), 默认 9600 bps 8N1
 *   - 空闲 = INPUT_PULLUP, 下降沿中断检测起始位
 *   - 实际采样在 swuart_tick() 中完成 (非 ISR)
 *   - TX 期间自动关中断/切方向, 完成后恢复 RX 监听
 *   - PSRAM 优先分配接收环形缓冲区
 *   - 零项目依赖 (仅需 <Arduino.h>)
 *
 * 硬件要求: GPIO 外接 4.7k~10k 上拉电阻到 3.3V
 ******************************************************************************/
#ifndef SW_UART_H
#define SW_UART_H

#include <Arduino.h>

// ---- 生命周期 ----
void swuart_init(uint8_t pin, uint32_t baud = 9600, size_t bufSize = 256);

// ---- 非阻塞轮询 (loop 中调用) ----
void swuart_tick();

// ---- 接收 (非阻塞) ----
int  swuart_available();
int  swuart_read();                       // 返回 0-255, 无数据返回 -1
int  swuart_read_blocking(uint32_t timeout_ms);  // 阻塞读, 超时返回 -1
void swuart_flush();                      // 丢弃全部已缓冲数据

// ---- 发送 (阻塞, 自动方向切换) ----
void swuart_write(uint8_t byte);          // 单字节, ~1ms @9600
void swuart_write_buffer(const uint8_t* data, size_t len);
void swuart_print(const char* str);       // 发字符串 (不含换行)
void swuart_println(const char* str);     // 发字符串 + CRLF

#endif // SW_UART_H
