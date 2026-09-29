#pragma once
#include <lvgl.h>

// Now-playing screen + "new song" toast. Layout follows the same size
// breakpoints as ui.cpp's compute_layout(), from board_caps().

// Build the (hidden) screen under `scr` and the toast on the top layer.
// `on_click` is attached to the screen (tap → next screen); `on_toast_click`
// fires when the toast is tapped.
void now_playing_init(lv_obj_t* scr, lv_event_cb_t on_click, lv_event_cb_t on_toast_click);

void now_playing_show(void);
void now_playing_hide(void);

// Redraw from media_get(). Cheap; call on every media update.
void now_playing_refresh(void);

// Slide in the toast for the current track (hides itself after a few s).
void now_playing_toast(void);
void now_playing_toast_hide(void);

// Progress bar + elapsed time while visible. Call from the UI tick.
void now_playing_tick(void);
