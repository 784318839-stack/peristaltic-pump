/******************************************************************************
 * pump_shared.h - Shared types, enums, pin definitions, constants.
 *
 * THIS FILE NO LONGER DECLARES pump-parameter extern globals.
 * All pump state lives in a PumpState struct (see pump_state.h).
 ******************************************************************************/
#ifndef PUMP_SHARED_H
#define PUMP_SHARED_H

#include <Arduino.h>

// ============================================================================
//                              Enums
// ============================================================================
enum PumpMode { MODE_VOLUME, MODE_TIME, MODE_JET };
enum State    { STATE_IDLE, RUNNING, PAUSED, DONE, ANTI_DRIP, STALL_ERROR };
enum Menu     { MAIN, SET_FLOW, SET_VOL, SET_TIME, CALIBRATE, PRIME,
                SET_JET_VOL, SET_JET_INTERVAL, SET_JET_FLOW, SET_JET_PRESSURE,
                SELECT_LIQUID, JET_OPTIONS };
enum CalibStep { CALIB_IDLE, CALIB_SELECT_LIQUID, CALIB_SET_VOL,
                 CALIB_RUN, CALIB_MEASURE, CALIB_RESULT, CALIB_SETTINGS };

// ============================================================================
//                              Constants
// ============================================================================
#define FW_VERSION "2.5.0"   // 固件版本 (hello 报文 / 自检 / 遥测共用)
#define NUM_LIQUIDS 4
#define STEP_PIN   16
#define DIR_PIN    17
#define ENA_PIN    18
#define BUZZER_PIN 5
#define HW_UART_RX 21
#define HW_UART_TX 47
#define SW_UART_PIN 15   // 软件模拟单线半双工 UART (9600bps, 自定义协议)
#define EEPROM_MAGIC  0x5062  // v2.4.0: TMC2226 16 细分 (3200 pulse/rev)
#define EEPROM_ADDR   0
#define ACCEL_FACTOR  0.3f
#define COMPLETIONS_PER_SAVE 10

// ---- 自动断电 (TMC2226 ENN=HIGH 关闭电机) ----
#define AUTO_OFF_MS          5000   // 待机/暂停/完成 5s 后自动断电
#define JET_OFF_DELAY_MS     2000   // 喷射间隔 >15s 时, 等待 2s 后断电

// ---- StallGuard 堵转检测 (硬件级, 替代已删除的软件检测) ----
// 注意: SG_RESULT 仅在 StealthChop 区间有效 (TSTEP >= TPWMTHRS, 即步速低于
// ~25000 pps)。高速进入 SpreadCycle 后 SG_RESULT 恒为 0, 因此高于
// SG_MAX_PPS 时跳过检测, 避免误报。
#define SG_CHECK_INTERVAL_MS 250    // 检测间隔
#define SG_STALL_THRESHOLD   2      // SG_RESULT (0-1023) <= 此值视为过载, 需按实际负载微调
#define SG_STALL_CONSECUTIVE 3      // 连续 N 次低值判定堵转
#define SG_MAX_PPS           20000  // 高于此步速跳过检测 (SpreadCycle 区间)

// ---- 网络控制 PIN (EEPROM 布局: 100=magic, 101..108=PIN) ----
#define PIN_EEPROM_BASE   100
#define PIN_EEPROM_MAGIC  0x50   // 'P'
#define PIN_MAX_LEN       8

constexpr const char* LIQUID_NAMES[NUM_LIQUIDS] = { "Wtr", "Thk", "Liq1", "Liq2" };

// ============================================================================
//                      Hardware object externs
// ============================================================================
#include <FastAccelStepper.h>
#include <EEPROM.h>

extern FastAccelStepperEngine stepperEngine;
extern FastAccelStepper *stepper;

// Convenience includes (needed by modules that include this header)
#include "buzzer.h"
#include "eeprom_store.h"
#include "pump_core.h"
#include "led.h"

#endif
