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
                SELECT_LIQUID, JET_OPTIONS, PRESET_LOAD };
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
#define IDLE_DISABLE_MS   5000
#define STALL_TIMEOUT_MS  1500
#define EEPROM_MAGIC  0x5059
#define EEPROM_ADDR   0
#define PRESET_BASE   64
#define PRESET_SIZE   30
#define ACCEL_FACTOR  0.3f
#define COMPLETIONS_PER_SAVE 10

constexpr const char* LIQUID_NAMES[NUM_LIQUIDS] = { "Wtr", "Thk", "Org", "Cst" };

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
