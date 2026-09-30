#include <Arduino.h>
#include "idle.h"
#include "idle_cfg.h"
#include "hal/display_hal.h"
#include "hal/power_hal.h"

// Three resting levels: AWAKE (user brightness), DIM (burn-in saver, panel
// still readable) and ASLEEP (black). Moves between them are linear fades;
// FADING carries the level it is heading to.
enum IdleState {
    STATE_AWAKE,
    STATE_DIM,
    STATE_ASLEEP,
    STATE_FADING,
};

static IdleState state = STATE_AWAKE;
static IdleState fade_target = STATE_AWAKE;
static uint32_t last_activity_ms = 0;
static uint32_t fade_started_ms  = 0;
static uint32_t fade_last_step_ms = 0;
static uint32_t fade_dur_ms = IDLE_FADE_IN_MS;
static uint8_t  cur_brightness = 0;   // last value sent to the panel
static uint8_t  fade_from = 0;
static uint8_t  fade_to   = 0;
static uint8_t  awake_brightness = DISPLAY_DEFAULT_BRIGHTNESS;  // user-set "full" level (brightness.cpp)
static uint32_t saver_after_ms = IDLE_SPLASH_AFTER_MS;  // daemon's `screensaver` overrides; 0 = off
static bool     saver_on = false;
static uint32_t sleep_after_ms = IDLE_TIMEOUT_MS;   // daemon's `screen_off` overrides; 0 = never on USB
static bool     usb_was = false;

static void apply_brightness(uint8_t b) {
    cur_brightness = b;
    display_hal_set_brightness(b);
}

static uint8_t level_of(IdleState s) {
    switch (s) {
    case STATE_AWAKE: return awake_brightness;
    case STATE_DIM:   return awake_brightness < IDLE_DIM_BRIGHTNESS ? awake_brightness : IDLE_DIM_BRIGHTNESS;
    default:          return 0;
    }
}

// Current resting state, or where an in-flight fade is heading.
static IdleState heading(void) {
    return state == STATE_FADING ? fade_target : state;
}

static void begin_fade(IdleState to, uint32_t dur, uint32_t now) {
    fade_from   = cur_brightness;   // start from wherever the panel is now
    fade_to     = level_of(to);
    fade_target = to;
    fade_dur_ms = dur;
    fade_started_ms = now;
    fade_last_step_ms = now;
    state = STATE_FADING;
}

static void wake(uint32_t now) {
    last_activity_ms = now;
    saver_on = false;
    if (heading() != STATE_AWAKE) begin_fade(STATE_AWAKE, IDLE_FADE_IN_MS, now);
}

void idle_init(void) {
    state = STATE_AWAKE;
    last_activity_ms = millis();
    usb_was = power_hal_is_vbus_in();
    apply_brightness(awake_brightness);
}

void idle_set_awake_brightness(uint8_t level) {
    awake_brightness = level;
    // Apply now if resting so a button press is visible immediately; during
    // fades the next fade picks it up.
    if (state == STATE_AWAKE || state == STATE_DIM) apply_brightness(level_of(state));
}

void idle_note_activity(void) {
    wake(millis());
}

bool idle_consume_wake_press(void) {
    uint32_t now = millis();
    // A dim panel is still readable, so the press both brightens it and acts.
    // A dark (or darkening) panel swallows the press, and so does the
    // screensaver — that press only brings the previous screen back.
    const IdleState h = heading();
    const bool dark = h == STATE_ASLEEP ||
                      (state == STATE_FADING && fade_target == STATE_AWAKE && fade_from == 0);
    const bool was_saver = saver_on;
    wake(now);
    return dark || was_saver;
}

bool idle_is_asleep(void) {
    return heading() == STATE_ASLEEP;
}

void idle_set_sleep_after_ms(uint32_t ms) {
    sleep_after_ms = ms;
}

void idle_set_saver_after_ms(uint32_t ms) {
    saver_after_ms = ms;
    if (ms == 0) saver_on = false;
}

bool idle_saver_active(void) {
    return saver_on;
}

void idle_tick(void) {
    uint32_t now = millis();

    // Plugging USB in counts as activity (wakes a dark panel).
    const bool usb = power_hal_is_vbus_in();
    if (usb && !usb_was) wake(now);
    usb_was = usb;

    // screen_off = off only holds on USB; on battery fall back to the default.
    const uint32_t sleep_ms = sleep_after_ms ? sleep_after_ms : (usb ? 0 : IDLE_TIMEOUT_MS);

    const uint32_t quiet = now - last_activity_ms;
    if (saver_after_ms > 0 && quiet >= saver_after_ms) saver_on = true;
    const IdleState h = heading();
    if (sleep_ms > 0 && quiet >= sleep_ms) {
        if (h != STATE_ASLEEP) begin_fade(STATE_ASLEEP, IDLE_FADE_OUT_MS, now);
    } else if (IDLE_DIM_AFTER_MS > 0 && quiet >= IDLE_DIM_AFTER_MS) {
        if (h == STATE_AWAKE) begin_fade(STATE_DIM, IDLE_DIM_FADE_MS, now);
    }

    if (state != STATE_FADING) return;
    if (now - fade_last_step_ms < IDLE_FADE_STEP_MS) return;
    fade_last_step_ms = now;
    uint32_t elapsed = now - fade_started_ms;
    if (elapsed >= fade_dur_ms) {
        apply_brightness(fade_to);
        state = fade_target;
    } else {
        // Linear interpolation fade_from -> fade_to over fade_dur_ms.
        int32_t span = (int32_t)fade_to - (int32_t)fade_from;
        int32_t b = (int32_t)fade_from + (span * (int32_t)elapsed) / (int32_t)fade_dur_ms;
        if (b < 0) b = 0;
        if (b > 255) b = 255;
        apply_brightness((uint8_t)b);
    }
}
