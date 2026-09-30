#pragma once

// Auto-sleep / idle screen-off configuration.
// All tunables live here so nothing is hard-coded in main.cpp / idle.cpp.

// Screen off: fade to black after this long without activity, on USB and on
// battery alike. Default only — the daemon's `screen_off = <minutes>|off`
// overrides it (payload field "so"); "off" keeps the panel on while on USB,
// but a battery-powered board still turns off after this default.
#define IDLE_TIMEOUT_MS             (30UL * 60UL * 1000UL)  // 30 min
#define IDLE_FADE_OUT_MS            400      // fade-to-black duration
#define IDLE_FADE_IN_MS             180      // wake fade-in (snappier)
#define IDLE_FADE_STEP_MS           20       // tick interval per fade step

// Burn-in saver: after this long without a button press, touch or Claude Code
// state change, fade to IDLE_DIM_BRIGHTNESS (never above the user's level).
// Applies on USB and battery. 0 disables.
#define IDLE_DIM_AFTER_MS           (5UL * 60UL * 1000UL)   // 5 min
#define IDLE_DIM_BRIGHTNESS         40
#define IDLE_DIM_FADE_MS            1500     // slow, so it doesn't catch the eye

// Burn-in saver ("screensaver"): after this long without activity, leave the
// static usage / music screens for the animated splash. The next activity
// (press, touch, Claude Code state change) brings the previous screen back.
// Default only — the daemon's `screensaver = <minutes>|off` overrides it
// (payload field "ss"). 0 disables.
#define IDLE_SPLASH_AFTER_MS        (10UL * 60UL * 1000UL)  // 10 min

// Burn-in saver: nudge the static screens (usage, music) by a pixel or two
// around a small orbit every PIXEL_SHIFT_MS so no pixel holds the same content
// all day. PIXEL_SHIFT_PX is the max offset from home; 0 disables.
#define PIXEL_SHIFT_MS              (3UL * 60UL * 1000UL)   // 3 min
#define PIXEL_SHIFT_PX              2

#define DISPLAY_DEFAULT_BRIGHTNESS  200      // active-screen brightness

// When true, a touch on the dark panel wakes the device (first touch is
// consumed for wake only, second touch acts normally). When false, touch is
// fully ignored during sleep — useful if cats/sleeves brushing the panel
// overnight would be a problem.
#define IDLE_WAKE_ON_TOUCH          true
