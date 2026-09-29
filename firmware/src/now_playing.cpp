#include "now_playing.h"
#include "media.h"
#include "theme.h"
#include "hal/board_caps.h"
#include <Arduino.h>
#include <stdio.h>

LV_FONT_DECLARE(font_tiempos_56);
LV_FONT_DECLARE(font_tiempos_34);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_14);
LV_FONT_DECLARE(font_styrene_12);
LV_FONT_DECLARE(font_mono_18);

#define TOAST_SHOW_MS  5000
#define TOAST_ANIM_MS  250

// Same breakpoints as ui.cpp's compute_layout(): large (480×480), compact
// (368×448 and similar), small (240×240).
struct NpLayout {
    int16_t w, h, margin, header_y, gap, bar_h, art_max, note_cell;
    const lv_font_t *header, *title, *artist, *album, *time;
    int16_t toast_h, toast_note_cell;
    const lv_font_t *toast_title, *toast_artist;
};
static NpLayout N;

static void compute(void) {
    const BoardCaps& c = board_caps();
    N.w = c.width;
    N.h = c.height;
    if (c.height >= 460) {
        N = { N.w, N.h, 20, 30, 10, 8, 200, 9,
              &font_tiempos_56, &font_styrene_28, &font_styrene_24, &font_styrene_20, &font_mono_18,
              84, 4, &font_styrene_24, &font_styrene_20 };
    } else if (c.height >= 300) {
        N = { N.w, N.h, 20, 30, 8, 6, 170, 7,
              &font_tiempos_56, &font_styrene_24, &font_styrene_20, &font_styrene_16, &font_mono_18,
              70, 3, &font_styrene_20, &font_styrene_16 };
    } else {
        N = { N.w, N.h, 8, 4, 4, 4, 100, 4,
              &font_tiempos_34, &font_styrene_16, &font_styrene_14, &font_styrene_12, &font_styrene_12,
              46, 2, &font_styrene_14, &font_styrene_12 };
    }
}

static lv_obj_t *root, *art, *lbl_title, *lbl_artist, *lbl_album, *bar, *lbl_elapsed, *lbl_total, *lbl_state;
static lv_obj_t *toast, *toast_title, *toast_artist;
static bool toast_up = false;
static uint32_t toast_shown_ms = 0;
static int32_t last_pos_drawn = -1;

// ---- Pixel-art note (album-art placeholder), in the splash's blocky style ----
static const char* const NOTE[] = {
    ".....XX...",
    ".....XXX..",
    ".....X.XX.",
    ".....X..X.",
    ".....X....",
    ".....X....",
    ".....X....",
    "..XXXX....",
    ".XXXXX....",
    ".XXXX.....",
    "..XX......",
};
#define NOTE_W 10
#define NOTE_H 11

static lv_obj_t* plain_box(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);
    return o;
}

// One rect per horizontal run of set cells.
static lv_obj_t* make_note(lv_obj_t* parent, int cell) {
    lv_obj_t* box = plain_box(parent);
    lv_obj_set_size(box, NOTE_W * cell, NOTE_H * cell);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    for (int y = 0; y < NOTE_H; y++) {
        for (int x = 0; x < NOTE_W;) {
            if (NOTE[y][x] != 'X') { x++; continue; }
            int run = 0;
            while (x + run < NOTE_W && NOTE[y][x + run] == 'X') run++;
            lv_obj_t* r = plain_box(box);
            lv_obj_set_style_bg_color(r, THEME_ACCENT, 0);
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_pos(r, x * cell, y * cell);
            lv_obj_set_size(r, run * cell, cell);
            x += run;
        }
    }
    return box;
}

static lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color,
                            lv_label_long_mode_t mode, int w) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_long_mode(l, mode);
    // Single line: without a fixed height, DOTS/CLIP labels grow to wrap.
    lv_obj_set_size(l, w, lv_font_get_line_height(font));
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, "");
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
    return l;
}

static int lh(const lv_font_t* f) { return lv_font_get_line_height(f); }

static void fmt_time(char* buf, size_t n, int32_t s) {
    if (s >= 3600) snprintf(buf, n, "%ld:%02ld:%02ld", (long)(s / 3600), (long)(s / 60 % 60), (long)(s % 60));
    else           snprintf(buf, n, "%ld:%02ld", (long)(s / 60), (long)(s % 60));
}

void now_playing_init(lv_obj_t* scr, lv_event_cb_t on_click, lv_event_cb_t on_toast_click) {
    compute();
    const int cw = N.w - 2 * N.margin;

    root = plain_box(scr);
    lv_obj_set_size(root, N.w, N.h);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(root, on_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t* header = lv_label_create(root);
    lv_label_set_text(header, "Music");
    lv_obj_set_style_text_font(header, N.header, 0);
    lv_obj_set_style_text_color(header, THEME_TEXT, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, N.header_y);
    lv_obj_add_flag(header, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Vertical budget: header, then [art, title, artist, album, bar, times]
    // centered in what's left. The art takes whatever the text leaves over.
    const int top = N.header_y + lh(N.header) + N.gap;
    const int text_h = lh(N.title) + lh(N.artist) + lh(N.album) + N.bar_h + lh(N.time) + 4 * N.gap;
    const int avail = N.h - top - N.margin;
    int art_px = avail - text_h - 2 * N.gap;
    if (art_px > N.art_max) art_px = N.art_max;
    if (art_px > cw) art_px = cw;
    int y = top + (avail - (art_px + 2 * N.gap + text_h)) / 2;

    art = plain_box(root);
    lv_obj_set_size(art, art_px, art_px);
    lv_obj_set_pos(art, (N.w - art_px) / 2, y);
    lv_obj_set_style_bg_color(art, THEME_PANEL, 0);
    lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(art, 12, 0);
    int cell = N.note_cell;
    while (cell > 1 && NOTE_H * cell > art_px * 6 / 10) cell--;
    lv_obj_center(make_note(art, cell));
    y += art_px + 2 * N.gap;

    // The scrolling title sits in a clipping box so it stays inside the
    // margins instead of running out to the (rounded) panel edge.
    lv_obj_t* title_clip = plain_box(root);
    lv_obj_set_style_bg_opa(title_clip, LV_OPA_TRANSP, 0);
    lv_obj_set_size(title_clip, cw, lh(N.title));
    lv_obj_set_pos(title_clip, N.margin, y);
    lbl_title = make_label(title_clip, N.title, THEME_TEXT, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR, cw);
    y += lh(N.title) + N.gap;
    lbl_artist = make_label(root, N.artist, THEME_DIM, LV_LABEL_LONG_MODE_DOTS, cw);
    lv_obj_set_pos(lbl_artist, N.margin, y);
    y += lh(N.artist) + N.gap;
    lbl_album = make_label(root, N.album, THEME_DIM, LV_LABEL_LONG_MODE_DOTS, cw);
    lv_obj_set_pos(lbl_album, N.margin, y);
    y += lh(N.album) + N.gap;

    bar = lv_bar_create(root);
    lv_obj_set_pos(bar, N.margin, y);
    lv_obj_set_size(bar, cw, N.bar_h);
    lv_bar_set_range(bar, 0, 1000);
    lv_obj_set_style_bg_color(bar, THEME_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, N.bar_h / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, THEME_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, N.bar_h / 2, LV_PART_INDICATOR);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_EVENT_BUBBLE);
    y += N.bar_h + N.gap;

    lbl_elapsed = lv_label_create(root);
    lv_obj_set_style_text_font(lbl_elapsed, N.time, 0);
    lv_obj_set_style_text_color(lbl_elapsed, THEME_DIM, 0);
    lv_obj_set_pos(lbl_elapsed, N.margin, y);
    lv_obj_add_flag(lbl_elapsed, LV_OBJ_FLAG_EVENT_BUBBLE);
    lbl_total = lv_label_create(root);
    lv_obj_set_style_text_font(lbl_total, N.time, 0);
    lv_obj_set_style_text_color(lbl_total, THEME_DIM, 0);
    lv_obj_align(lbl_total, LV_ALIGN_TOP_RIGHT, -N.margin, y);
    lv_obj_add_flag(lbl_total, LV_OBJ_FLAG_EVENT_BUBBLE);
    lbl_state = make_label(root, N.time, THEME_ACCENT, LV_LABEL_LONG_MODE_CLIP, cw / 2);
    lv_obj_set_pos(lbl_state, (N.w - cw / 2) / 2, y);

    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);

    // ---- Toast: slides up from the bottom edge on the top layer, so it
    // floats over whichever screen is showing. ----
    toast = plain_box(lv_layer_top());
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(toast, cw, N.toast_h);
    lv_obj_set_pos(toast, N.margin, N.h);
    lv_obj_set_style_bg_color(toast, THEME_PANEL, 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(toast, 14, 0);
    lv_obj_set_style_border_color(toast, THEME_ACCENT, 0);
    lv_obj_set_style_border_width(toast, 2, 0);
    lv_obj_add_flag(toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(toast, on_toast_click, LV_EVENT_CLICKED, NULL);
    lv_obj_t* tnote = make_note(toast, N.toast_note_cell);
    const int pad = (N.toast_h - NOTE_H * N.toast_note_cell) / 2;
    lv_obj_align(tnote, LV_ALIGN_LEFT_MID, pad, 0);
    const int tx = pad * 2 + NOTE_W * N.toast_note_cell;
    const int tw = cw - tx - pad;
    const int ty = (N.toast_h - lh(N.toast_title) - lh(N.toast_artist)) / 2;
    toast_title = make_label(toast, N.toast_title, THEME_TEXT, LV_LABEL_LONG_MODE_DOTS, tw);
    lv_obj_set_style_text_align(toast_title, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(toast_title, tx, ty);
    toast_artist = make_label(toast, N.toast_artist, THEME_DIM, LV_LABEL_LONG_MODE_DOTS, tw);
    lv_obj_set_style_text_align(toast_artist, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(toast_artist, tx, ty + lh(N.toast_title));
    lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);

    now_playing_refresh();
}

void now_playing_show(void) {
    lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    last_pos_drawn = -1;
    now_playing_tick();
}

void now_playing_hide(void) { lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN); }

void now_playing_refresh(void) {
    const MediaInfo& m = media_get();
    const bool have = (m.status == MEDIA_PLAYING || m.status == MEDIA_PAUSED) && m.title[0];
    if (!have) {
        lv_label_set_text(lbl_title, "Nothing playing");
        lv_obj_set_style_text_color(lbl_title, THEME_DIM, 0);
        lv_label_set_text(lbl_artist, "");
        lv_label_set_text(lbl_album, "");
    } else {
        lv_label_set_text(lbl_title, m.title);
        lv_obj_set_style_text_color(lbl_title, THEME_TEXT, 0);
        lv_label_set_text(lbl_artist, m.artist);
        lv_label_set_text(lbl_album, m.album);
    }
    const bool timed = have && m.len_s > 0;
    lv_obj_t* timed_objs[] = { bar, lbl_elapsed, lbl_total };
    for (lv_obj_t* o : timed_objs) {
        if (timed) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
        else       lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(lbl_state, !have ? "" : m.status == MEDIA_PAUSED ? "Paused"
                                         : m.len_s == 0 ? "Live" : "");
    last_pos_drawn = -1;
    now_playing_tick();
}

static void toast_y_cb(void* obj, int32_t v) { lv_obj_set_y((lv_obj_t*)obj, v); }
static void toast_hidden_cb(lv_anim_t* a) { lv_obj_add_flag((lv_obj_t*)a->var, LV_OBJ_FLAG_HIDDEN); }

static void toast_slide(bool in) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, toast);
    lv_anim_set_exec_cb(&a, toast_y_cb);
    const int32_t shown = N.h - N.toast_h - N.margin;
    lv_anim_set_values(&a, lv_obj_get_y(toast), in ? shown : N.h);
    lv_anim_set_duration(&a, TOAST_ANIM_MS);
    lv_anim_set_path_cb(&a, in ? lv_anim_path_ease_out : lv_anim_path_ease_in);
    if (!in) lv_anim_set_completed_cb(&a, toast_hidden_cb);
    lv_anim_delete(toast, toast_y_cb);
    lv_anim_start(&a);
}

void now_playing_toast(void) {
    const MediaInfo& m = media_get();
    lv_label_set_text(toast_title, m.title);
    lv_label_set_text(toast_artist, m.artist);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast);
    toast_slide(true);
    toast_up = true;
    toast_shown_ms = millis();
}

void now_playing_toast_hide(void) {
    if (!toast_up) return;
    toast_up = false;
    toast_slide(false);
}

void now_playing_tick(void) {
    if (toast_up && millis() - toast_shown_ms >= TOAST_SHOW_MS) now_playing_toast_hide();

    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return;
    const MediaInfo& m = media_get();
    if (m.len_s <= 0) return;
    const int32_t pos = media_position_s();
    if (pos == last_pos_drawn) return;
    last_pos_drawn = pos;
    lv_bar_set_value(bar, (int32_t)((int64_t)pos * 1000 / m.len_s), LV_ANIM_OFF);
    char buf[16];
    fmt_time(buf, sizeof(buf), pos);
    lv_label_set_text(lbl_elapsed, buf);
    fmt_time(buf, sizeof(buf), m.len_s);
    lv_label_set_text(lbl_total, buf);
    lv_obj_align(lbl_total, LV_ALIGN_TOP_RIGHT, -N.margin, lv_obj_get_y(lbl_elapsed));
}
