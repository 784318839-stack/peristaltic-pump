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

#define MAX_SEQ 10  // 最多 5 段音 ( 每段 = freq + dur )

// 音序 : [ freq0, dur0, freq1, dur1, freq2, dur2, freq3, dur3, freq4, dur4 ]
static int  g_seq[MAX_SEQ * 2];
static int  g_seqLen = 0;    // 音序段数 ( 每段占 2 个 int )
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
      tone( BUZZER_PIN, freq, dur );   // ESP32 tone 带时长参数
    } else {
      noTone( BUZZER_PIN );            // 静音间隔
    }
    g_next = millis() + dur;
    g_seqPos++;
  }

  // 音序结束
  if ( g_seqPos >= g_seqLen ) {
    noTone( BUZZER_PIN );
    g_seqLen = 0;
  }
}

// 启动音序 : 可变参数 ( freq, dur ) 对 , 以 freq=0 结尾
// 实际用重载 : 最多 5 段
static void startSeq( int f0, int d0, int f1, int d1, int f2, int d2,
                       int f3, int d3, int f4, int d4 ) {
  g_seq[0] = f0; g_seq[1] = d0;
  g_seq[2] = f1; g_seq[3] = d1;
  g_seq[4] = f2; g_seq[5] = d2;
  g_seq[6] = f3; g_seq[7] = d3;
  g_seq[8] = f4; g_seq[9] = d4;

  // 统计有效段数
  g_seqLen = 0;
  for ( int i = 0; i < 5; i++ ) {
    if ( g_seq[i * 2 + 1] > 0 ) g_seqLen = i + 1;
  }
  if ( g_seqLen == 0 ) return;

  g_seqPos = 0;
  g_next = 0;
  buzzer_tick();  // 立刻开始第一段
}

// ---- 音效 ----
// freq>0 = 发声段 , freq=0 = 静音间隔

void beepInput()   { startSeq( 1200, 12,  0, 0,  0, 0,  0, 0,  0, 0 ); }
void beepConfirm() { startSeq(  880, 80,  0, 0,  0, 0,  0, 0,  0, 0 ); }
void beepCancel()  { startSeq(  440, 80,  0, 0,  0, 0,  0, 0,  0, 0 ); }

// 启动 : 660 Hz 50ms → 静音 35ms → 880 Hz 80ms
void beepStart()   { startSeq(  660, 50,  0, 35,  880, 80,  0, 0,  0, 0 ); }

// 暂停 : 880 Hz 50ms → 静音 35ms → 660 Hz 80ms
void beepPause()   { startSeq(  880, 50,  0, 35,  660, 80,  0, 0,  0, 0 ); }

// 完成 : 1000 Hz 60ms → 静音 45ms → 1000 Hz 60ms → 静音 45ms → 1000 Hz 100ms
void beepDone()    { startSeq( 1000, 60,  0, 45, 1000, 60,  0, 45, 1000, 100 ); }

// 断电 : 200 Hz 80ms
void beepDisable() { startSeq(  200, 80,  0, 0,  0, 0,  0, 0,  0, 0 ); }
