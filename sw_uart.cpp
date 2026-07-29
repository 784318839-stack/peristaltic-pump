/******************************************************************************
 * sw_uart.cpp — 单线软件模拟半双工 UART 实现
 *
 * 时序 (9600 bps): 每 bit = 104 µs, 半 bit = 52 µs
 * 帧格式: 1 起始位 (LOW) + 8 数据位 (LSB first) + 1 停止位 (HIGH)
 * 每帧: ~1.04 ms
 *
 * RX: 下降沿 ISR 记录时间 → swuart_tick() 采样位 → 环形缓冲区
 * TX: 关中断 → OUTPUT → 比特冲击 → INPUT_PULLUP → 重新武装中断
 ******************************************************************************/
#include "sw_uart.h"
#include <esp_heap_caps.h>

// ============================================================================
//                         静态状态 (模块自包含)
// ============================================================================
static uint8_t  g_pin        = 0;
static uint32_t g_bitTimeUs  = 104;   // 1e6 / 9600
static bool     g_initialized = false;

// ---- 接收状态 ----
static volatile bool         g_rxPending    = false;
static volatile unsigned long g_startUs     = 0;

// ---- 发送状态 ----
static volatile bool g_txActive = false;

// ---- 环形缓冲区 ----
static uint8_t* g_buf     = nullptr;
static size_t   g_bufSize = 0;
static volatile size_t g_bufHead = 0;   // 写位置
static volatile size_t g_bufTail = 0;   // 读位置
static volatile size_t g_bufCount = 0;  // 可读字节数

// ============================================================================
//                         辅助函数
// ============================================================================

// 忙等到 target_us 时刻 (微秒)
static inline void waitUntil(unsigned long target_us) {
  while ((long)(micros() - target_us) < 0) {
    asm volatile("nop");
  }
}

// 存入接收缓冲区 (ISR/tick 上下文调用)
static bool bufPush(uint8_t byte) {
  if (g_bufCount >= g_bufSize) return false;   // 满, 丢弃
  size_t idx = (g_bufHead + g_bufCount) % g_bufSize;
  g_buf[idx] = byte;
  g_bufCount++;
  return true;
}

// ============================================================================
//                         中断服务例程
// ============================================================================
static void IRAM_ATTR swuart_isr() {
  g_startUs    = micros();
  g_rxPending  = true;
  detachInterrupt(g_pin);
}

// ============================================================================
//                         初始化
// ============================================================================
void swuart_init(uint8_t pin, uint32_t baud, size_t bufSize) {
  g_pin       = pin;
  g_bitTimeUs = (baud > 0) ? (1000000UL / baud) : 104;
  g_bufSize   = (bufSize > 0) ? bufSize : 256;
  g_bufHead   = 0;
  g_bufTail   = 0;
  g_bufCount  = 0;
  g_rxPending = false;
  g_txActive  = false;

  // PSRAM 优先分配 (跟随项目模式)
  g_buf = (uint8_t*)heap_caps_malloc(g_bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!g_buf) {
    g_buf = (uint8_t*)malloc(g_bufSize);   // 回退到内部 SRAM
  }

  // 初始化引脚: INPUT_PULLUP = 空闲 HIGH
  pinMode(g_pin, INPUT_PULLUP);
  delayMicroseconds(100);  // 等上拉稳定

  // 挂载下降沿中断 (检测起始位)
  attachInterrupt(digitalPinToInterrupt(g_pin), swuart_isr, FALLING);

  g_initialized = true;
}

// ============================================================================
//                         轮询 (loop 中调用)
// ============================================================================
void swuart_tick() {
  if (!g_initialized || !g_rxPending) return;

  // 如果正在发送, 忽略 (自己 TX 可能触发边沿)
  if (g_txActive) {
    g_rxPending = false;
    return;
  }

  unsigned long startUs = g_startUs;
  uint32_t bt = g_bitTimeUs;

  // 1. 验证起始位 (采样于下降沿后 0.5 bitTime)
  waitUntil(startUs + bt / 2);
  if (digitalRead(g_pin) != LOW) {
    // 噪声毛刺, 丢弃
    g_rxPending = false;
    attachInterrupt(digitalPinToInterrupt(g_pin), swuart_isr, FALLING);
    return;
  }

  // 2. 采样 8 数据位 (LSB first), 每 bit 中心
  uint8_t val = 0;
  for (int i = 0; i < 8; i++) {
    waitUntil(startUs + (unsigned long)((i + 1.5) * bt));
    if (digitalRead(g_pin) == HIGH) {
      val |= (1 << i);
    }
  }

  // 3. 采样停止位
  waitUntil(startUs + (unsigned long)(9.5 * bt));
  bool stopOk = (digitalRead(g_pin) == HIGH);

  // 4. 存储或丢弃
  if (stopOk) {
    bufPush(val);
  }
  // 停止位错误 → 静默丢弃

  // 5. 重新武装
  g_rxPending = false;
  attachInterrupt(digitalPinToInterrupt(g_pin), swuart_isr, FALLING);
}

// ============================================================================
//                         接收 API
// ============================================================================
int swuart_available() {
  return (int)g_bufCount;
}

int swuart_read() {
  if (g_bufCount == 0) return -1;
  uint8_t byte = g_buf[g_bufTail];
  g_bufTail = (g_bufTail + 1) % g_bufSize;
  g_bufCount--;
  return byte;
}

int swuart_read_blocking(uint32_t timeout_ms) {
  unsigned long deadline = millis() + timeout_ms;
  while (millis() < deadline) {
    swuart_tick();
    int b = swuart_read();
    if (b >= 0) return b;
    delayMicroseconds(100);   // 100µs 细粒度重试
  }
  return -1;
}

void swuart_flush() {
  g_bufTail  = g_bufHead;
  g_bufCount = 0;
}

// ============================================================================
//                         发送 API (阻塞)
// ============================================================================
static void transmitByte(uint8_t byte) {
  uint32_t bt = g_bitTimeUs;

  // 1. 拿总线控制权
  g_txActive = true;
  detachInterrupt(g_pin);
  g_rxPending = false;   // 丢弃任何待处理的假边沿

  // 2. 切换为输出, 发送起始位
  pinMode(g_pin, OUTPUT);
  digitalWrite(g_pin, LOW);
  delayMicroseconds(bt);

  // 3. 发送 8 数据位 LSB first
  for (int i = 0; i < 8; i++) {
    digitalWrite(g_pin, (byte & (1 << i)) ? HIGH : LOW);
    delayMicroseconds(bt);
  }

  // 4. 停止位
  digitalWrite(g_pin, HIGH);
  delayMicroseconds(bt);

  // 5. 释放总线
  digitalWrite(g_pin, HIGH);            // 确保 HIGH
  pinMode(g_pin, INPUT_PULLUP);         // 切回输入 + 上拉
  delayMicroseconds(20);                // 等上拉稳定

  // 6. 重新武装 RX
  attachInterrupt(digitalPinToInterrupt(g_pin), swuart_isr, FALLING);
  g_txActive = false;
}

void swuart_write(uint8_t byte) {
  if (!g_initialized) return;
  transmitByte(byte);
}

void swuart_write_buffer(const uint8_t* data, size_t len) {
  if (!g_initialized || !data || len == 0) return;

  // 整批发送期间只切换一次方向 (效率)
  g_txActive = true;
  detachInterrupt(g_pin);
  g_rxPending = false;

  pinMode(g_pin, OUTPUT);
  uint32_t bt = g_bitTimeUs;

  for (size_t j = 0; j < len; j++) {
    uint8_t byte = data[j];

    // 起始位
    digitalWrite(g_pin, LOW);
    delayMicroseconds(bt);

    // 8 数据位
    for (int i = 0; i < 8; i++) {
      digitalWrite(g_pin, (byte & (1 << i)) ? HIGH : LOW);
      delayMicroseconds(bt);
    }

    // 停止位 + 帧间间隔 (1 bit)
    digitalWrite(g_pin, HIGH);
    delayMicroseconds(bt);
  }

  // 释放总线
  digitalWrite(g_pin, HIGH);
  pinMode(g_pin, INPUT_PULLUP);
  delayMicroseconds(20);

  attachInterrupt(digitalPinToInterrupt(g_pin), swuart_isr, FALLING);
  g_txActive = false;
}

void swuart_print(const char* str) {
  if (!g_initialized || !str) return;
  swuart_write_buffer((const uint8_t*)str, strlen(str));
}

void swuart_println(const char* str) {
  if (!g_initialized || !str) return;
  swuart_write_buffer((const uint8_t*)str, strlen(str));
  swuart_write('\r');
  swuart_write('\n');
}
