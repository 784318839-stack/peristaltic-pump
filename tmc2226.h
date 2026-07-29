/******************************************************************************
 * tmc2226.h — TMC2226 单线 UART 协议驱动
 *
 * 通过 GPIO15 软件模拟 UART (sw_uart) 与 TMC2226 PDN_UART 通信
 * 协议: Trinamic UART 数据报 (0x55 sync + addr + reg + data + CRC-8)
 * CRC: 多项式 x^8 + x^2 + x + 1 (0x07), 起始值 0x00
 *
 * 默认: 地址 0 (MS1=GND, MS2=GND), 16 细分 (3200 pulse/rev)
 ******************************************************************************/
#ifndef TMC2226_H
#define TMC2226_H

#include <Arduino.h>

// ---- TMC2226 寄存器地址 ----
#define TMC_REG_GCONF          0x00
#define TMC_REG_GSTAT          0x01
#define TMC_REG_IFCNT          0x02
#define TMC_REG_SLAVECONF      0x03
#define TMC_REG_OTP_PROG       0x04
#define TMC_REG_OTP_READ       0x05
#define TMC_REG_IOIN           0x06
#define TMC_REG_FACTORY_CONF   0x07
#define TMC_REG_IHOLD_IRUN     0x10
#define TMC_REG_TPOWERDOWN     0x11
#define TMC_REG_TSTEP          0x12
#define TMC_REG_TPWMTHRS       0x13
#define TMC_REG_VACTUAL        0x22
#define TMC_REG_SGTHRS         0x40
#define TMC_REG_SG_RESULT      0x41
#define TMC_REG_COOLCONF       0x6D
#define TMC_REG_CHOPCONF       0x6C
#define TMC_REG_PWMCONF        0x70

// ---- MRES 微步分辨率 (CHOPCONF bits 27-24) ----
#define TMC_MRES_256    0x00   // 256 微步
#define TMC_MRES_128    0x01
#define TMC_MRES_64     0x02
#define TMC_MRES_32     0x03
#define TMC_MRES_16     0x04   // 16 微步 (推荐)
#define TMC_MRES_8      0x05
#define TMC_MRES_4      0x06
#define TMC_MRES_2      0x07
#define TMC_MRES_FULL   0x08   // 全步

// ---- API ----
void tmc2226_init();                        // 上电初始化, 写默认配置
void tmc2226_setup_defaults();              // 写入推荐默认参数
void tmc2226_write(uint8_t reg, uint32_t data);
uint32_t tmc2226_read(uint8_t reg);
bool tmc2226_test_comm();                   // 通信测试 (读 IOIN/GSTAT)
void tmc2226_enable();                      // 软件使能 (写 GCONF)
void tmc2226_disable();                     // 软件关闭

#endif // TMC2226_H
