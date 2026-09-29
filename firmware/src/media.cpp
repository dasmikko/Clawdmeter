#include "media.h"
#include <Arduino.h>
#include <string.h>

static MediaInfo m = {};

bool media_update(JsonObjectConst np) {
    const char* st = np["st"] | "Stopped";
    media_status_t status = MEDIA_STOPPED;
    if (strcmp(st, "Playing") == 0)     status = MEDIA_PLAYING;
    else if (strcmp(st, "Paused") == 0) status = MEDIA_PAUSED;

    const char* title  = np["ti"] | "";
    const char* artist = np["ar"] | "";
    const bool new_track = strcmp(title, m.title) != 0 || strcmp(artist, m.artist) != 0;

    m.status = status;
    strlcpy(m.title,  title,  sizeof(m.title));
    strlcpy(m.artist, artist, sizeof(m.artist));
    strlcpy(m.album,  np["al"] | "", sizeof(m.album));
    m.len_s  = np["len"] | 0;
    m.pos_s  = np["pos"] | 0;
    m.at_ms  = millis();
    return new_track && status == MEDIA_PLAYING && m.title[0];
}

const MediaInfo& media_get(void) { return m; }

int32_t media_position_s(void) {
    int32_t pos = m.pos_s;
    if (m.status == MEDIA_PLAYING) pos += (int32_t)((millis() - m.at_ms) / 1000);
    if (m.len_s > 0 && pos > m.len_s) pos = m.len_s;
    return pos;
}
