// led.cpp - WS2812 status LED on GPIO48
// Color scheme:
//   IDLE       : dim green breathing
//   RUNNING    : blue solid
//   PAUSED     : amber pulsing
//   DONE       : bright green flash -> fade
//   ANTI_DRIP  : cyan quick pulse
//   Tube >80%  : red overlay blink (superimposed)
//   WiFi client: white subtle glow overlay
//   BLE conn   : cyan micro-glow overlay
#include "led.h"
#include "pump_shared.h"
#include <Adafruit_NeoPixel.h>

#define LED_PIN   48
#define LED_COUNT 1

static Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// Breathing / pulse timing
static unsigned long g_frame = 0;   // millis of last frame
static unsigned long g_phase = 0;   // phase accumulator for animations

static State    g_lastState    = STATE_IDLE;
static PumpMode g_lastMode     = MODE_VOLUME;
static bool     g_lastEnabled  = true;
static int      g_lastTubePct  = 0;
static bool     g_lastWifiCli  = false;
static bool     g_lastBleConn  = false;

// ----- Helpers -----
static void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  strip.setPixelColor(0, strip.Color(r, g, b));
  strip.show();
}

// Dim a color by factor 0.0..1.0
static void setRGBDim(uint8_t r, uint8_t g, uint8_t b, float dim) {
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
  g_phase++;

  // Base color from pump state
  uint8_t r = 0, g = 0, b = 0;
  float dim = 1.0;
  bool pulse = false;

  switch (pumpState) {
    case STATE_IDLE:
      r = 0; g = 30; b = 0;
      // Breathing: slow sine wave
      dim = 0.1 + 0.15 * (1 + sin(g_phase * 0.02));
      break;

    case RUNNING:
      if (pumpMode == MODE_JET) {
        r = 30; g = 0; b = 30;   // magenta for jet
      } else if (pumpMode == MODE_TIME) {
        r = 0; g = 20; b = 60;   // deeper blue for timed
      } else {
        r = 0; g = 0; b = 80;    // blue for volume
      }
      dim = 0.7;
      break;

    case PAUSED:
      r = 60; g = 30; b = 0;     // amber
      dim = 0.3 + 0.4 * (1 + sin(g_phase * 0.1));
      pulse = true;
      break;

    case DONE:
      // Bright green flash, then fade over ~3 seconds
      {
        unsigned long doneAge = g_phase; // approx since phase resets on state change
        r = 0; g = 180; b = 0;
        float fade = 1.0 - (doneAge % 150) / 150.0;
        if (fade < 0) fade = 0;
        dim = fade;
      }
      break;

    case ANTI_DRIP:
      r = 0; g = 50; b = 50;     // cyan
      dim = 0.3 + 0.5 * (1 + sin(g_phase * 0.3));
      pulse = true;
      break;

    case STALL_ERROR:
      // Fast red blink (alarm)
      r = 120; g = 0; b = 0;
      dim = (sin(g_phase * 0.3) > 0) ? 1.0 : 0.1;
      break;
  }

  // Tube life warning overlay (>80%)
  bool tubeWarn = (tubeLifeML > 0 && totalDispensed > tubeLifeML * 0.8);
  if (tubeWarn && (pumpState == STATE_IDLE || pumpState == DONE)) {
    // Red blink every ~2 seconds
    float blink = sin(g_phase * 0.03);
    if (blink > 0.7) { r = 80; g = 0; b = 0; dim = 0.5; }
  }

  // Apply brightness
  setRGBDim(r, g, b, dim);

  // Reset phase on state change (for DONE fade timing)
  if (pumpState != g_lastState    || pumpMode   != g_lastMode ||
      stepperEnabled != g_lastEnabled || tubeWarn != (g_lastTubePct > 80)) {
    g_phase = 0;
  }
  g_lastState   = pumpState;
  g_lastMode    = pumpMode;
  g_lastEnabled = stepperEnabled;
  g_lastTubePct = (tubeLifeML > 0) ? (int)(totalDispensed / tubeLifeML * 100) : 0;
}

void led_update() {
  // Called from setup/loop to sync state - tick handles everything
}
