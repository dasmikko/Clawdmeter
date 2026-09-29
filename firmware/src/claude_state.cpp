#include "claude_state.h"
#include <Arduino.h>
#include <string.h>

// The daemon writes at least once a minute (poll or heartbeat) and pushes
// every state change immediately, so silence this long means it's gone.
#define CLAUDE_STATE_STALE_MS 180000UL

static claude_state_t state = CLAUDE_UNKNOWN;
static uint32_t set_ms = 0;

claude_state_t claude_state_get(void) {
    if (state != CLAUDE_UNKNOWN && millis() - set_ms > CLAUDE_STATE_STALE_MS)
        state = CLAUDE_UNKNOWN;
    return state;
}

bool claude_state_set_from_str(const char *s) {
    claude_state_t next = CLAUDE_UNKNOWN;
    if (s) {
        if      (strcmp(s, "idle") == 0)    next = CLAUDE_IDLE;
        else if (strcmp(s, "working") == 0) next = CLAUDE_WORKING;
        else if (strcmp(s, "waiting") == 0) next = CLAUDE_WAITING;
    }
    const claude_state_t prev = claude_state_get();
    state = next;
    set_ms = millis();
    return next != prev;
}
