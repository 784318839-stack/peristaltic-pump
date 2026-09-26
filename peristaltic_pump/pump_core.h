/******************************************************************************
 * pump_core.h — 步进电机控制 / 泵状态机 / 校准
 *
 * 本模块封装所有泵送核心逻辑 , 供 .ino 和远程命令模块调用
 ******************************************************************************/
#ifndef PUMP_CORE_H
#define PUMP_CORE_H

#include <Arduino.h>

// ---- 流量 / 速度 ----
float flowRateToPPS( float mLmin );

// 唯一的速度设置入口。FastAccelStepper 会拒绝 0 速度和非正加速度并保持原值不变,
// 之后 moveTo() 返回 MOVE_ERR_SPEED_IS_UNDEFINED —— 一个脉冲都不发, 但状态机看到
// isRunning()==false 就以为已经跑完, 于是上报"分液完成"。低速段必须走 millHz 接口。
// 返回 false 表示设置失败 (未接步进 / 超出驱动器范围)。
bool applySpeed( float pps, float accel );

// 按 mL/min 设定速度 (内部换算成 pps 后调 applySpeed)
void  applyFlowSpeed( float mLmin );

// 用「用户设定的流量」更新速度 —— 待机 / set_flow 用这个。
// 运行中请用 applyFlowSpeed(pump.activeFlowRate), TIME 模式下两者不同;
// 校准运行用 applyFlowSpeed(pump.calibFlowRate), 两者也不同。
void  updateStepperSpeed();

// ---- 使能管理 ----
void ensureStepperOn();

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
// 校准只读写 calib* 字段, 不改 mode / flowRate / targetVolume / currentLiquid /
// stepsPerMl; 唯一的提交点是 calibSave()
void calibEnter();
// 退出校准向导 (calib_abort / calib_settings_done / menu_main 都走这里)
void calibLeave();
// 本次校准用的 stepsPerMl —— 取自向导第 1 步选的 calibLiquid, 不是日常那个
float calibSPM();
void calibStartRun();
void calibStopRun();
void calibFinishRun();
// 返回 false 表示算不出有效 stepsPerMl (电机没转 / 读数为 0), 调用方必须报错而不是继续
bool calibCalculate();
void calibSave();

#endif
