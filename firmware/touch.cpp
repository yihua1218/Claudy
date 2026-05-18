#include "touch.h"
#include "display.h"

#include <Wire.h>

static const uint8_t CST816_ADDR = 0x15;

struct TouchPins {
  int sda;
  int scl;
  int rst;
  int irq;
  const char* name;
};

static const TouchPins CANDIDATES[] = {
  // LilyGo examples for the T-Display-S3 Touch commonly use SDA=18, SCL=17,
  // RST=21, IRQ=16. Some Qwiic-oriented references expose I2C on 43/44.
  {18, 17, 21, 16, "CST816 18/17 rst21 irq16"},
#ifdef TOUCH_TRY_ALT_I2C
  {43, 44, -1, -1, "CST816 43/44"},
#endif
};

static bool s_present = false;
static TouchPins s_pins = {-1, -1, -1, -1, "none"};
static bool s_wasDown = false;
static bool s_longSent = false;
static int16_t s_downX = 0;
static int16_t s_downY = 0;
static int16_t s_lastX = 0;
static int16_t s_lastY = 0;
static uint32_t s_downAt = 0;
static uint32_t s_lastTapAt = 0;

static bool probeAddress() {
  Wire.beginTransmission(CST816_ADDR);
  return Wire.endTransmission() == 0;
}

static void resetTouch(const TouchPins& pins) {
  if (pins.rst < 0) return;
  pinMode(pins.rst, OUTPUT);
  digitalWrite(pins.rst, LOW);
  delay(6);
  digitalWrite(pins.rst, HIGH);
  delay(50);
}

bool touchBegin() {
  for (const auto& pins : CANDIDATES) {
    if (pins.rst >= 0) resetTouch(pins);
    Wire.end();
    Wire.begin(pins.sda, pins.scl);
    Wire.setTimeOut(35);
    Wire.setClock(400000);
    delay(20);
    if (probeAddress()) {
      s_present = true;
      s_pins = pins;
      if (pins.irq >= 0) pinMode(pins.irq, INPUT_PULLUP);
      Serial.printf("Touch: found %s\n", s_pins.name);
      return true;
    }
  }
  Wire.end();
  Serial.println("Touch: CST816 not found");
  return false;
}

const char* touchConfigName() {
  return s_present ? s_pins.name : "none";
}

static bool readPoint(int16_t& x, int16_t& y) {
  if (!s_present) return false;

  // CST816 data from 0x01: gesture, finger count, XH, XL, YH, YL.
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0x01);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)CST816_ADDR, 6) != 6) return false;

  (void)Wire.read();                  // gesture id; local deltas are steadier
  uint8_t fingers = Wire.read();
  uint8_t xh = Wire.read();
  uint8_t xl = Wire.read();
  uint8_t yh = Wire.read();
  uint8_t yl = Wire.read();
  if ((fingers & 0x0F) == 0) return false;

  int16_t rawX = ((xh & 0x0F) << 8) | xl;
  int16_t rawY = ((yh & 0x0F) << 8) | yl;

  // Native panel is 170x320 portrait; display rotation is landscape.
  x = rawY;
  y = 169 - rawX;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= tft.width()) x = tft.width() - 1;
  if (y >= tft.height()) y = tft.height() - 1;
  return true;
}

TouchEvent touchLoop() {
  TouchEvent out;
  if (!s_present) return out;

  int16_t x = 0, y = 0;
  bool down = readPoint(x, y);
  uint32_t now = millis();

  if (down && !s_wasDown) {
    s_wasDown = true;
    s_longSent = false;
    s_downX = s_lastX = x;
    s_downY = s_lastY = y;
    s_downAt = now;
    return out;
  }

  if (down && s_wasDown) {
    s_lastX = x;
    s_lastY = y;
    int dx = abs(s_lastX - s_downX);
    int dy = abs(s_lastY - s_downY);
    if (!s_longSent && now - s_downAt > 850 && dx < 18 && dy < 18) {
      s_longSent = true;
      out.gesture = TOUCH_LONG_PRESS;
      out.x = x;
      out.y = y;
    }
    return out;
  }

  if (!down && s_wasDown) {
    s_wasDown = false;
    if (s_longSent) return out;

    int dx = s_lastX - s_downX;
    int dy = s_lastY - s_downY;
    int adx = abs(dx);
    int ady = abs(dy);
    out.x = s_lastX;
    out.y = s_lastY;

    if (adx > 42 || ady > 42) {
      if (adx > ady) {
        out.gesture = dx > 0 ? TOUCH_SWIPE_RIGHT : TOUCH_SWIPE_LEFT;
      } else {
        out.gesture = dy > 0 ? TOUCH_SWIPE_DOWN : TOUCH_SWIPE_UP;
      }
    } else if (now - s_lastTapAt < 360) {
      out.gesture = TOUCH_DOUBLE_TAP;
      s_lastTapAt = 0;
    } else {
      out.gesture = TOUCH_TAP;
      s_lastTapAt = now;
    }
  }

  return out;
}
