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
enum State    { STATE_IDLE, RUNNING, PAUSED, DONE, ANTI_DRIP };
// OLED + 4x4 键盘已停用, 只剩这三个界面状态会被实际赋值
enum Menu     { MAIN, CALIBRATE, PRIME };
enum CalibStep { CALIB_IDLE, CALIB_SELECT_LIQUID, CALIB_SET_VOL,
                 CALIB_RUN, CALIB_MEASURE, CALIB_RESULT, CALIB_SETTINGS };

// ============================================================================
//                              Constants
// ============================================================================
#define NUM_LIQUIDS 4
#define STEP_PIN   16
#define DIR_PIN    17
#define ENA_PIN    18
#define BUZZER_PIN 5
#define HW_UART_RX 21
#define HW_UART_TX 47
#define EEPROM_MAGIC  0x5061  // v4.2: 400 pulse/rev 细分 (revert from 1600)
#define EEPROM_ADDR   0
#define ACCEL_FACTOR  0.3f
#define COMPLETIONS_PER_SAVE 10

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
