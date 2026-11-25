#pragma once
#include <tsugumi/tsugumi_view.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  TSUGUMI_DAWN_DRAW_AGAIN = 1,
  TSUGUMI_DAWN_POINTER_MOVE = 0,
  TSUGUMI_DAWN_POINTER_DOWN = 1,
  TSUGUMI_DAWN_POINTER_UP = 2,
  TSUGUMI_DAWN_POINTER_WHEEL = 3,
  TSUGUMI_DAWN_POINTER_LEAVE = 4,
  TSUGUMI_DAWN_KEY_DOWN = 0,
  TSUGUMI_DAWN_KEY_UP = 1,
  TSUGUMI_DAWN_GAMEPAD_CONNECTED = 0,
  TSUGUMI_DAWN_GAMEPAD_CHANGED = 1,
  TSUGUMI_DAWN_GAMEPAD_DISCONNECTED = 2,
  TSUGUMI_DAWN_MOD_SHIFT = 1u << 0,
  TSUGUMI_DAWN_MOD_CONTROL = 1u << 1,
  TSUGUMI_DAWN_MOD_ALT = 1u << 2,
  TSUGUMI_DAWN_MOD_META = 1u << 3,
  TSUGUMI_DAWN_BUTTON_PRIMARY = 1u << 0,
  TSUGUMI_DAWN_BUTTON_SECONDARY = 1u << 1,
  TSUGUMI_DAWN_BUTTON_MIDDLE = 1u << 2,
};

static inline double tsugumi_dawn_seconds(tsugumi_view_time frames) {
  return (double)frames / TSUGUMI_VIEW_SAMPLE_RATE;
}

#ifdef __cplusplus
}
#endif
