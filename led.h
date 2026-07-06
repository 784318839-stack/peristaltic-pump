/******************************************************************************
 * led.h — WS2812 状态指示灯驱动
 *
 * 单颗 WS2812 , 非阻塞状态机 , led_tick() 必须在 loop() 中每帧调用
 *
 * 颜色方案 :
 *   IDLE       — 暗绿呼吸
 *   RUNNING    — 蓝色常亮 ( VOL ) / 深蓝 ( TIME ) / 品红 ( JET )
 *   PAUSED     — 琥珀色脉动
 *   DONE       — 亮绿闪烁渐灭
 *   ANTI_DRIP  — 青色快速脉动
 *   管路 > 80% — 红色叠加闪烁 ( 待机 / 完成时 )
 ******************************************************************************/
#ifndef LED_H
#define LED_H

#include <Arduino.h>

// 初始化 WS2812 ( setup 中调用 )
void led_init();

// 每帧推进呼吸灯动画 ( loop 中调用 , ~50 fps )
void led_tick();

// 同步泵状态到 LED ( loop 末尾调用 )
void led_update();

#endif
