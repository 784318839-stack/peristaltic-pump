// led.cpp - WS2812 status LED on GPIO48
// Color scheme:
//   IDLE       : dim green breathing
//   RUNNING    : blue solid
//   PAUSED     : amber pulsing
//   DONE       : bright green flash -> fade
//   ANTI_DRIP  : cyan quick pulse
//   Tube >80%  : red overlay blink (superimposed)
#include "led.h"
#include "pump_shared.h"
#include "pump_state.h"
#include <Adafruit_NeoPixel.h>

// LED_PIN 定义在 pump_shared.h 的引脚表里
#define LED_COUNT 1

static Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// Breathing / pulse timing
static unsigned long g_frame = 0;   // millis of last frame
static unsigned long g_phase = 0;   // phase accumulator for animations (~20ms per tick)

static State    g_lastState    = STATE_IDLE;
static PumpMode g_lastMode     = MODE_VOLUME;
static int      g_lastTubePct  = 0;

// ----- Helpers -----
static void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  strip.setPixelColor(0, strip.Color(r, g, b));
  strip.show();
}

// Dim a color by factor 0.0..1.0 (越界就地钳位, 保证 (uint8_t)(255*dim) 不会溢出)
static void setRGBDim(uint8_t r, uint8_t g, uint8_t b, float dim) {
  if (!(dim > 0.0f)) dim = 0.0f;        // 同时挡住负数和 NaN
  else if (dim > 1.0f) dim = 1.0f;
  setRGB((uint8_t)(r * dim), (uint8_t)(g * dim), (uint8_t)(b * dim));
}

// ----- Init -----
void led_init() {
  strip.begin();
  strip.setBrightness(40);  // global max brightness (0-255)
  strip.clear();
  strip.show();
}

// ----- Tick -----
void led_tick() {
  unsigned long now = millis();
  if (now - g_frame < 20) return;  // ~50 fps
  g_frame = now;

  // 管路寿命告警 (>80%)
  bool tubeWarn = (pump.tubeLifeML > 0 && pump.totalDispensed > pump.tubeLifeML * 0.8);
  int  tubePct  = (pump.tubeLifeML > 0) ? (int)(pump.totalDispensed / pump.tubeLifeML * 100) : 0;

  // 状态变化时先归零相位再自增 —— 必须在算颜色之前做, 否则 DONE 的第一帧
  // 会拿着上一个状态的相位去算渐暗 (原先重置写在函数末尾, 就是这个问题)
  if (pump.state != g_lastState || pump.mode != g_lastMode || tubeWarn != (g_lastTubePct > 80)) {
    g_phase = 0;
  }
  g_lastState   = pump.state;
  g_lastMode    = pump.mode;
  g_lastTubePct = tubePct;
  g_phase++;

  // Base color from pump state. 所有 dim 表达式都必须落在 [0,1] 内。
  uint8_t r = 0, g = 0, b = 0;
  float dim = 1.0f;

  switch (pump.state) {
    case STATE_IDLE:
      r = 0; g = 30; b = 0;
      dim = 0.1f + 0.15f * (1.0f + sinf(g_phase * 0.02f));   // [0.10, 0.40] 慢呼吸
      break;

    case RUNNING:
      if (pump.mode == MODE_JET) {
        r = 30; g = 0; b = 30;   // magenta for jet
      } else if (pump.mode == MODE_TIME) {
        r = 0; g = 20; b = 60;   // deeper blue for timed
      } else {
        r = 0; g = 0; b = 80;    // blue for volume
      }
      dim = 0.7f;
      break;

    case PAUSED:
      r = 60; g = 30; b = 0;     // amber
      dim = 0.3f + 0.35f * (1.0f + sinf(g_phase * 0.1f));    // [0.30, 1.00]
      break;

    case DONE: {
      // 亮绿闪一下然后在 DONE_HOLD_MS 内渐暗到 0。
      // led_tick 每 20ms 推进一帧, 所以 g_phase * 20 就是进入 DONE 后的毫秒数。
      r = 0; g = 180; b = 0;
      dim = 1.0f - (float)(g_phase * 20) / (float)DONE_HOLD_MS;
      break;
    }

    case ANTI_DRIP:
      r = 0; g = 50; b = 50;     // cyan
      dim = 0.3f + 0.35f * (1.0f + sinf(g_phase * 0.3f));    // [0.30, 1.00] 快脉动
      break;
  }

  // 寿命告警红灯叠加 (只在待机/完成时, 避免盖掉运行状态色)
  if (tubeWarn && (pump.state == STATE_IDLE || pump.state == DONE)) {
    if (sinf(g_phase * 0.03f) > 0.7f) { r = 80; g = 0; b = 0; dim = 0.5f; }
  }

  setRGBDim(r, g, b, dim);
}
