/******************************************************************************
 * buzzer.cpp — 非阻塞无源蜂鸣器实现
 *
 * 使用 freq+duration 数组状态机驱动 ESP32 tone() / noTone()
 * 多音调的 beep ( beepStart, beepPause, beepDone ) 不会阻塞 loop()
 *
 * 音序格式 : [ freq_hz, dur_ms, freq_hz, dur_ms, ... ]
 *   freq > 0  → tone( pin, freq, dur )  发出指定频率
 *   freq = 0  → noTone( pin )           静音间隔
 ******************************************************************************/
#include "buzzer.h"
#include "pump_shared.h"

#define MAX_SEGMENTS 5  // 最多 5 段音 ( 每段 = freq + dur )

// 音序 : [ freq0, dur0, freq1, dur1, freq2, dur2, freq3, dur3, freq4, dur4 ]
static int  g_seq[MAX_SEGMENTS * 2];
static int  g_seqLen = 0;    // 音序段数
static int  g_seqPos = 0;    // 当前播放到的段索引
static unsigned long g_next = 0;

void buzzer_tick() {
  if ( g_seqLen == 0 ) return;
  if ( millis() < g_next ) return;

  // 播放当前段
  if ( g_seqPos < g_seqLen ) {
    int freq = g_seq[g_seqPos * 2];
    int dur  = g_seq[g_seqPos * 2 + 1];
    if ( freq > 0 ) {
      // ESP32 core 3.x 的 tone() 是异步的: 只把 TONE_START 投进 _tone_queue,
      // 由 toneTask 去 ledcWriteTone() + delay(dur) + 自动静音。
      tone( BUZZER_PIN, freq, dur );
    } else {
      noTone( BUZZER_PIN );            // 静音间隔
    }
    g_next = millis() + dur;
    g_seqPos++;
    return;   // 关键: 本 tick 到此为止, 不要顺手做收尾
  }

  // 走到这里说明最后一段的时长已经走完, 现在才 noTone() 释放 LEDC 通道。
  // 绝不能和最后那次 tone() 放在同一个 tick 里: noTone() 内部会先
  // xQueueReset(_tone_queue) 清空待发队列, 刚投进去的 TONE_START 会被直接丢弃
  // —— 表现就是「单段音效完全无声、多段音效最后一段被切掉」。
  noTone( BUZZER_PIN );
  g_seqLen = 0;
  g_seqPos = 0;
}

// 启动音序 : ( freq, dur ) 对 , 用 dur=0 表示后续没有段
// 注意: 新的 beep 会直接覆盖正在播放的音序, 不做排队
static void startSeq( int f0, int d0, int f1, int d1, int f2, int d2,
                       int f3, int d3, int f4, int d4 ) {
  g_seq[0] = f0; g_seq[1] = d0;
  g_seq[2] = f1; g_seq[3] = d1;
  g_seq[4] = f2; g_seq[5] = d2;
  g_seq[6] = f3; g_seq[7] = d3;
  g_seq[8] = f4; g_seq[9] = d4;

  // 统计有效段数
  g_seqLen = 0;
  for ( int i = 0; i < MAX_SEGMENTS; i++ ) {
    if ( g_seq[i * 2 + 1] > 0 ) g_seqLen = i + 1;
  }
  if ( g_seqLen == 0 ) return;

  g_seqPos = 0;
  g_next = 0;
  buzzer_tick();  // 立刻开始第一段
}

// ---- 音效 ----
// freq>0 = 发声段 , freq=0 = 静音间隔

void beepConfirm() { startSeq(  880, 80,  0, 0,  0, 0,  0, 0,  0, 0 ); }
void beepCancel()  { startSeq(  440, 80,  0, 0,  0, 0,  0, 0,  0, 0 ); }

// 启动 : 660 Hz 50ms → 静音 35ms → 880 Hz 80ms
void beepStart()   { startSeq(  660, 50,  0, 35,  880, 80,  0, 0,  0, 0 ); }

// 暂停 : 880 Hz 50ms → 静音 35ms → 660 Hz 80ms
void beepPause()   { startSeq(  880, 50,  0, 35,  660, 80,  0, 0,  0, 0 ); }

// 完成 : 1000 Hz 60ms → 静音 45ms → 1000 Hz 60ms → 静音 45ms → 1000 Hz 100ms
void beepDone()    { startSeq( 1000, 60,  0, 45, 1000, 60,  0, 45, 1000, 100 ); }
