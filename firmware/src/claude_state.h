#pragma once
#include <stdbool.h>

// What Claude Code is doing on the host, reported by the daemon as "cc" in the
// usage payload (from Claude Code hooks, see daemon/claude-state-hook.sh).
// UNKNOWN when the daemon doesn't send it (hooks not installed, old daemon) or
// no payload has landed recently — callers then fall back to usage-rate logic.
enum claude_state_t {
    CLAUDE_UNKNOWN = -1,
    CLAUDE_IDLE    = 0,
    CLAUDE_WORKING = 1,
    CLAUDE_WAITING = 2,   // needs you: permission prompt or a question
};

// Parse the payload's "cc" string (NULL/unrecognized → UNKNOWN) and record it.
// Returns true when the effective state changed.
bool claude_state_set_from_str(const char *s);

// Current state; decays to UNKNOWN when no payload landed for a while.
claude_state_t claude_state_get(void);
