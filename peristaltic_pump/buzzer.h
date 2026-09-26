/******************************************************************************
 * buzzer.h — 非阻塞无源蜂鸣器驱动
 *
 * 所有 beep 函数仅调度音序后立即返回 , 不阻塞 loop()
 * buzzer_tick() 必须在 loop() 中每帧调用以推进音序状态机
 *
 * 使用 ESP32 tone() / noTone() 驱动无源蜂鸣器
 *
 * 音效设计 :
 *   beepInput   — 1200 Hz × 12 ms   ( 按键反馈 )
 *   beepConfirm —  880 Hz × 80 ms   ( 确认操作 )
 *   beepCancel  —  440 Hz × 80 ms   ( 取消 / 返回 )
 *   beepStart   —  660→880 Hz 上行  ( 启动 / 恢复 )
 *   beepPause   —  880→660 Hz 下行  ( 暂停 )
 *   beepDone    — 1000 Hz 三连音    ( 完成 )
 *   beepDisable —  200 Hz × 80 ms   ( 电机断电 )
 ******************************************************************************/
#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>

// 每帧调用 , 推进蜂鸣器音序状态机
void buzzer_tick();

// 调度音效 ( 立即返回 , 非阻塞 )
void beepInput();
void beepConfirm();
void beepCancel();
void beepStart();
void beepPause();
void beepDone();
void beepDisable();

#endif
