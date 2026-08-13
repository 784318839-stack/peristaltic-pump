/******************************************************************************
 * tmc2226.cpp — TMC2226 单线 UART 协议驱动实现
 *
 * PDN_UART 单线半双工:
 *   - 空闲: GPIO15 = INPUT_PULLUP = HIGH (UART 激活)
 *   - 发送: swuart_write/write_buffer (软件比特冲击)
 *   - 接收: swuart_read_blocking (中断触发 + 轮询采样)
 *
 * 数据报格式 (Trinamic UART, TMC2209/2226 家族):
 *   写: [0x05] [addr:8] [reg|W:8] [D31..D24] ... [D7..D0] [CRC-8]
 *   读请求: [0x05] [addr:8] [reg|R:8] [CRC-8]
 *   读响应: [0x05] [addr:8] [reg回显:8] [D31..D24] ... [D7..D0] [CRC-8]
 *
 * CRC-8 多项式: x^8 + x^2 + x + 1 (0x07), 初始 0x00
 * 覆盖范围: 从 sync 到 data 末字节 (不含 CRC 自身)
 ******************************************************************************/
#include "tmc2226.h"
#include "sw_uart.h"

// ============================================================================
//                         CRC-8 计算
// ============================================================================
static uint8_t crc8(const uint8_t* data, size_t len) {
  // 标准 Trinamic UART CRC-8: 多项式 x^8+x^2+x+1 (0x07), 初始值 0
  // MSB-first 移位 + 反射输入 (字节按 LSB 先行处理)
  uint8_t crc = 0;
  while (len--) {
    uint8_t currentByte = *data++;
    for (int j = 0; j < 8; j++) {
      if ((crc >> 7) ^ (currentByte & 0x01))
        crc = (crc << 1) ^ 0x07;
      else
        crc = (crc << 1);
      currentByte >>= 1;
    }
  }
  return crc;
}

// ============================================================================
//                        UART 数据报
// ============================================================================
#define TMC_ADDR   0x00            // 默认从机地址 (MS1=GND, MS2=GND)
#define TMC_READ   0x80            // 寄存器地址 bit7 = 读标志

static void tmc2226_write_raw(uint8_t reg, uint32_t data) {
  uint8_t buf[8];

  buf[0] = 0x05;                   // 同步字节 (TMC2209/2226 家族)
  buf[1] = TMC_ADDR;               // 从机地址
  buf[2] = reg & 0x7F;             // 寄存器 (bit7=0 = 写)
  buf[3] = (data >> 24) & 0xFF;    // D31..D24
  buf[4] = (data >> 16) & 0xFF;    // D23..D16
  buf[5] = (data >> 8)  & 0xFF;    // D15..D8
  buf[6] =  data        & 0xFF;    // D7..D0
  buf[7] = crc8(buf, 7);           // CRC over [sync..D0] = 7 bytes

  swuart_write_buffer(buf, 8);

  // 等总线稳定后恢复监听 (8 bit 时间 @ 9600 = ~0.83ms, 取整 1ms)
  delayMicroseconds(1200);
}

static uint32_t tmc2226_read_raw(uint8_t reg) {
  // 1. 发读请求: [0x05] [addr] [reg|R] [CRC]
  uint8_t req[4];
  req[0] = 0x05;                        // 同步字节
  req[1] = TMC_ADDR;
  req[2] = (reg & 0x7F) | TMC_READ;     // 寄存器地址 + 读标志
  req[3] = crc8(req, 3);                // CRC over [sync, addr, reg|R]

  swuart_flush();                       // 清空旧数据
  swuart_write_buffer(req, 4);

  // 2. 等待从机响应 (TMC2226 在 ~10 bit 时间后开始发送)
  // 响应长度: sync + addr + reg回显 + 4(data) + crc = 8 bytes
  // 8 * 10 * 104us = ~8.3ms, 给 15ms 超时
  delayMicroseconds(200);  // 等 TMC2226 准备好

  uint8_t resp[8];
  int len = 0;
  unsigned long deadline = millis() + 15;

  while (millis() < deadline && len < 8) {
    swuart_tick();
    int b = swuart_read();
    if (b >= 0) {
      resp[len++] = (uint8_t)b;
    }
  }

  // 3. 解析响应
  if (len < 8) return 0;  // 超时或短帧

  // 找 sync 字节位置
  int syncIdx = -1;
  for (int i = 0; i <= len - 8; i++) {
    if (resp[i] == 0x05) { syncIdx = i; break; }
  }
  if (syncIdx < 0 || len - syncIdx < 8) return 0;

  // 验证 CRC (覆盖 sync..D0 = 7 bytes)
  if (crc8(resp + syncIdx, 7) != resp[syncIdx + 7]) return 0;

  // 组装 32-bit 数据 (跳过 reg 回显字节)
  uint32_t data = ((uint32_t)resp[syncIdx + 3] << 24)
                | ((uint32_t)resp[syncIdx + 4] << 16)
                | ((uint32_t)resp[syncIdx + 5] << 8)
                |  (uint32_t)resp[syncIdx + 6];

  return data;
}

// ============================================================================
//                        公共 API
// ============================================================================
void tmc2226_write(uint8_t reg, uint32_t data) {
  tmc2226_write_raw(reg, data);
}

uint32_t tmc2226_read(uint8_t reg) {
  return tmc2226_read_raw(reg);
}

bool tmc2226_test_comm() {
  // 读 IOIN 寄存器 (0x06), 上电默认值 bit4 (VERSION) = 1
  uint32_t v = tmc2226_read(TMC_REG_IOIN);
  // TMC2226 IOIN bit4 = 1 表示版本号
  return (v != 0) && ((v & 0x00000010) != 0 || (v & 0x000F0000) != 0);
}

void tmc2226_setup_defaults() {
  // --- GCONF: 默认全部 0 (使用 STEP/DIR 控制) ---
  tmc2226_write(TMC_REG_GCONF, 0x00000000);

  // --- CHOPCONF: 16 细分 + SpreadCycle ---
  // TOFF=3 (12µs off time), HSTRT=4, HEND=2, TBL=2 (24 clocks blanking)
  // MRES=4 (16 微步), intpol=1 (256 微步插值)
  // CHM=0 (SpreadCycle)
  uint32_t chop = (3 << 0)              // TOFF
                | (4 << 4)              // HSTRT
                | (2 << 7)              // HEND
                | (2 << 15)             // TBL
                | (TMC_MRES_16 << 24)   // MRES = 16 微步
                | (1UL << 28);          // intpol = 1 (256 插值)
  tmc2226_write(TMC_REG_CHOPCONF, chop);

  // --- IHOLD_IRUN: 最大电流上限 (CoolStep 自动调节) ---
  // IRUN=24/31 = 上限 ~1.5A RMS (CoolStep 会从这儿往下调)
  // IHOLD=6/31  = 静止保持 ~0.4A RMS
  // IHOLDDELAY=3 = 30ms 后进保持电流
  uint32_t ihold_irun = (6  << 0)       // IHOLD (保持电流)
                      | (24 << 8)       // IRUN  (运行电流上限)
                      | (3  << 16);     // IHOLDDELAY
  tmc2226_write(TMC_REG_IHOLD_IRUN, ihold_irun);

  // --- TPOWERDOWN: CoolStep 降流后仍超 0.2s 无运动则进一步降电流 ---
  tmc2226_write(TMC_REG_TPOWERDOWN, 20);

  // --- TPWMTHRS: StealthChop 速度阈值 ---
  // TSTEP >= TPWMTHRS 时切 SpreadCycle (高速)
  tmc2226_write(TMC_REG_TPWMTHRS, 500);

  // --- PWMCONF: StealthChop PWM 配置 ---
  // CoolStep 依赖 StealthChop (PWM 模式)
  uint32_t pwm = (1 << 0)               // PWM_AMPL LSB
               | (180 << 1)             // PWM_AMPL[7:1]
               | (14 << 8)              // PWM_GRAD
               | (1 << 16)              // PWM_FREQ = 36kHz
               | (1 << 17)              // PWM_AUTOSCALE
               | (1 << 18);             // PWM_AUTOGRAD
  tmc2226_write(TMC_REG_PWMCONF, pwm);

  // --- COOLCONF: CoolStep 自动电流调节 ---
  // CoolStep 用 StallGuard 值检测负载:
  //   SG_RESULT 高 → 轻载 → 自动降流
  //   SG_RESULT 低 → 重载 → 自动升流 (最多回到 IRUN)
  // SEIMIN=1 → 最轻载电流 = IRUN × 1/4
  // SEDN=1   → 降流速度 = 32 step (慢降, 避免抖动)
  // SEUP=1   → 升流速度 = 2 step  (快升, 快速响应负载)
  // SEMAX=0  → 最大 = IRUN (不超调)
  // SEMIN=4  → 最小电流比例
  // SFILT=1  → StallGuard 滤波使能
  uint32_t cool = (4 << 0)              // SEMIN
                | (1 << 8)              // SEUP
                | (0 << 12)             // SEMAX
                | (0 << 14)             // SEDN
                | (1 << 15)             // SEIMIN
                | (1UL << 24);          // SFILT
  tmc2226_write(TMC_REG_COOLCONF, cool);

  // --- SGTHRS: StallGuard 阈值 (CoolStep 负载检测基准) ---
  // 值越高越灵敏, 泵启动负载适中取 10, 后续可据实际工况微调
  tmc2226_write(TMC_REG_SGTHRS, 10);
}

void tmc2226_init() {
  tmc2226_setup_defaults();
}

void tmc2226_enable() {
  // TMC2226 上电默认已使能, GCONF 无单独 enable 位
  // ENN 引脚物理控制使能 (由 pump_core 管理)
}

void tmc2226_disable() {
  // 通过 TOFF=0 (CHOPCONF bits 3-0) 关闭电机输出
  uint32_t chop = tmc2226_read(TMC_REG_CHOPCONF);
  chop &= ~0xF;              // TOFF=0 = driver disable
  tmc2226_write(TMC_REG_CHOPCONF, chop);
}
