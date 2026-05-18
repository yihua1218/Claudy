#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "state.h"
#include "display.h"
#include "mascot.h"
#include "net.h"
#include "touch.h"

#ifndef ENABLE_TOUCH
#define ENABLE_TOUCH 0
#endif

AppState g_state;

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== Claudy boot ===");
  g_state.brightness = BRIGHTNESS;

  displayBegin(BRIGHTNESS);
  g_state.state = STATE_BOOT;
  strncpy(g_state.message, "Connecting WiFi...", sizeof(g_state.message));
  requestRedraw();
  renderFrame();

  if (netBegin()) {
    g_state.state = STATE_IDLE;
    snprintf(g_state.message, sizeof(g_state.message),
             "%s.local  %s", MDNS_HOSTNAME, WiFi.localIP().toString().c_str());
  } else {
    g_state.state = STATE_ERROR;
    strncpy(g_state.message, "WiFi failed. Check config.h", sizeof(g_state.message));
  }

#if ENABLE_TOUCH
  g_state.touchPresent = touchBegin();
  snprintf(g_state.touchHint, sizeof(g_state.touchHint),
           g_state.touchPresent ? "Touch ready" : "No touch");
#else
  g_state.touchPresent = false;
  snprintf(g_state.touchHint, sizeof(g_state.touchHint), "Touch off");
#endif
  requestRedraw();
}

void loop() {
  netLoop();

  TouchEvent tev = {};
#if ENABLE_TOUCH
  tev = touchLoop();
#endif
  if (tev.gesture != TOUCH_NONE) {
    if (tev.gesture == TOUCH_LONG_PRESS) {
      g_state.touchLocked = !g_state.touchLocked;
      snprintf(g_state.touchHint, sizeof(g_state.touchHint),
               g_state.touchLocked ? "Touch locked" : "Touch unlocked");
      g_state.uiHoldUntilMs = millis() + 2200;
      requestRedraw();
    } else if (!g_state.touchLocked) {
      switch (tev.gesture) {
        case TOUCH_TAP:
          if (tev.x < 140) {
            g_state.uiView = (g_state.uiView + 1) % 3;
            snprintf(g_state.touchHint, sizeof(g_state.touchHint), "View %u", g_state.uiView + 1);
          } else {
            g_state.uiHoldUntilMs = millis() + 30000;
            snprintf(g_state.touchHint, sizeof(g_state.touchHint), "Pinned 30s");
          }
          break;
        case TOUCH_DOUBLE_TAP:
          g_state.state = STATE_DONE;
          g_state.tool = TOOL_NONE;
          strncpy(g_state.message, "Touch OK", sizeof(g_state.message));
          g_state.lastUpdateMs = millis();
          g_state.uiHoldUntilMs = millis() + 2500;
          snprintf(g_state.touchHint, sizeof(g_state.touchHint), "Double tap");
          break;
        case TOUCH_SWIPE_UP:
          if (g_state.brightness < 240) g_state.brightness += 15;
          else g_state.brightness = 255;
          tft.setBrightness(g_state.brightness);
          snprintf(g_state.touchHint, sizeof(g_state.touchHint), "Bright %u", g_state.brightness);
          break;
        case TOUCH_SWIPE_DOWN:
          if (g_state.brightness > 30) g_state.brightness -= 15;
          else g_state.brightness = 12;
          tft.setBrightness(g_state.brightness);
          snprintf(g_state.touchHint, sizeof(g_state.touchHint), "Dim %u", g_state.brightness);
          break;
        case TOUCH_SWIPE_LEFT:
        case TOUCH_SWIPE_RIGHT:
          g_state.uiView = (g_state.uiView + (tev.gesture == TOUCH_SWIPE_RIGHT ? 1 : 2)) % 3;
          snprintf(g_state.touchHint, sizeof(g_state.touchHint), "View %u", g_state.uiView + 1);
          break;
        default:
          break;
      }
      g_state.uiHoldUntilMs = millis() + 1800;
      requestRedraw();
    }
  }

  // Auto-return to IDLE after timeout
  if (IDLE_TIMEOUT_MS > 0 &&
      g_state.state != STATE_IDLE &&
      g_state.state != STATE_BOOT &&
      g_state.state != STATE_ERROR &&
      millis() > g_state.uiHoldUntilMs &&
      g_state.lastUpdateMs > 0 &&
      millis() - g_state.lastUpdateMs > IDLE_TIMEOUT_MS) {
    g_state.state = STATE_IDLE;
      g_state.tool = TOOL_NONE;
      strncpy(g_state.message, "Idle", sizeof(g_state.message));
      requestRedraw();
  }

  // Render only when there's something to draw (state changed via requestRedraw,
  // or the current state's animation tick has elapsed). This frees CPU/bus time
  // for the HTTP server during steady-state operation.
  static unsigned long lastFrame = 0;
  static MascotState   lastAnimState = STATE_BOOT;
  unsigned long now = millis();
  uint32_t interval = mascotAnimInterval(g_state.state);

  if (g_state.state != lastAnimState) {
    lastAnimState = g_state.state;
    lastFrame = 0;   // force immediate render of new state
  }

  if (interval > 0 && now - lastFrame >= interval) {
    lastFrame = now;
    requestRedraw();
  }

  renderFrame();    // no-op unless dirty
  netLoop();        // extra drain in case the render took a moment
}
