#pragma once
#include <Arduino.h>

enum TouchGesture : uint8_t {
  TOUCH_NONE = 0,
  TOUCH_TAP,
  TOUCH_DOUBLE_TAP,
  TOUCH_LONG_PRESS,
  TOUCH_SWIPE_UP,
  TOUCH_SWIPE_DOWN,
  TOUCH_SWIPE_LEFT,
  TOUCH_SWIPE_RIGHT,
};

struct TouchEvent {
  TouchGesture gesture = TOUCH_NONE;
  int16_t x = 0;
  int16_t y = 0;
};

bool touchBegin();
TouchEvent touchLoop();
const char* touchConfigName();
