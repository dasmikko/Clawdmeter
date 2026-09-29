#pragma once
#include <stdint.h>
#include <ArduinoJson.h>

// What's playing on the host, sent by the daemon as {"np":{...}} (see
// daemon/media_watch.py). Text is already folded to ASCII host-side.
enum media_status_t { MEDIA_NONE, MEDIA_STOPPED, MEDIA_PAUSED, MEDIA_PLAYING };

struct MediaInfo {
    media_status_t status;   // MEDIA_NONE until the first update
    char title[124];
    char artist[84];
    char album[84];
    int32_t len_s;           // 0 = unknown (live stream)
    int32_t pos_s;           // position when the update landed
    uint32_t at_ms;          // millis() when the update landed
};

// Apply an "np" object. Returns true when it's a different track (title or
// artist changed) that is playing — the cue for the "now playing" toast.
bool media_update(JsonObjectConst np);

const MediaInfo& media_get(void);

// Position now, extrapolated while playing and clamped to the length.
int32_t media_position_s(void);
