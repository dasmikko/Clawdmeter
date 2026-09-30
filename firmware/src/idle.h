#pragma once
#include <stdbool.h>
#include <stdint.h>

void idle_init(void);
void idle_tick(void);
void idle_note_activity(void);

// Set the "awake" brightness target (0..255). idle owns display brightness
// (it fades between this and 0), so user brightness control routes through
// here. Applied immediately if the screen is currently fully awake; otherwise
// picked up by the next fade-in. See brightness.{h,cpp}.
void idle_set_awake_brightness(uint8_t level);

// Returns true if this press was consumed as a wake-up from a dark panel or
// the screensaver (caller MUST skip the button's normal action). Returns false
// when awake or merely dimmed (a dim panel brightens and the press still acts). Always notes
// the activity, so callers don't need a separate idle_note_activity() call.
bool idle_consume_wake_press(void);

// Touch should NOT count as activity (avoids accidental wakes from pets,
// sleeves, etc.). Callers use this to silently drop touch events while the
// panel is dark.
bool idle_is_asleep(void);

// Screen off after this many quiet ms. 0 = never while on USB (battery still
// uses IDLE_TIMEOUT_MS). Set from the daemon's `screen_off` option.
void idle_set_sleep_after_ms(uint32_t ms);

// Screensaver: true once there's been no activity for the configured time,
// false again on the next activity. main.cpp shows the splash while it's on
// and restores the previous screen after. 0 ms disables it.
void idle_set_saver_after_ms(uint32_t ms);
bool idle_saver_active(void);
