/******************************************************************************
 * pump_core.h — 步进电机控制 / 泵状态机 / 校准 / 输入缓冲
 *
 * 本模块封装所有泵送核心逻辑 , 供 .ino 和远程命令模块调用
 ******************************************************************************/
#ifndef PUMP_CORE_H
#define PUMP_CORE_H

#include <Arduino.h>

// ---- 流量 / 速度 ----
float flowRateToPPS( float mLmin );
void  updateStepperSpeed();

// ---- 使能管理 ----
void ensureStepperOn();
void checkIdleDisable();

// ---- 泵状态机 ----
void startPump();
void stopPump();
void pausePump();
void resumePump();
void resetPump();

// ---- 喷射模式 ----
void startJetSquirt();
void startJetCycle();
void stopJetCycle();

// ---- 液体选择 ----
void selectLiquid( int idx );

// ---- 校准向导 ----
void calibEnter();
void calibStartRun();
void calibStopRun();
void calibFinishRun();
void calibCalculate();
void calibSave();

// ---- 数字输入缓冲 ( 所有参数输入界面共用 ) ----
void  inputClear();
void  inputBackspace();
void  inputAppend( char c );
float inputToFloat();

#endif
