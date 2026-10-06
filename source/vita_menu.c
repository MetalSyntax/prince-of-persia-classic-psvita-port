/*
 * Copyright (C) 2026 Prince of Persia Classic Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "vita_menu.h"

#include "input.h"
#include "overlay.h"
#include "utils/logger.h"
#include "utils/settings.h"

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>

#include <stdio.h>
#include <string.h>

typedef enum {
    ROW_ACTION,
    ROW_DEADZONE,
    ROW_DEFAULTS,
    ROW_CLOSE,
} RowKind;

#define MAX_ROWS 32

#define COL_WHITE   0xffffffff
#define COL_GREY    0xffb4b4b4
#define COL_ACCENT  0xff00c8ff
#define COL_SEL_BG  0x5000c8ff
#define COL_DIM     0xb0000000
#define COL_PANEL   0xf0141a1e
#define COL_BORDER  0xff4a6070

#define REPEAT_DELAY 400000
#define REPEAT_RATE  110000

#define TEXT_SCALE 2.0f
#define ROW_H      26.0f

static struct {
    RowKind kind;
    int action;
} rows[MAX_ROWS];
static int row_count;

static int open;
static int sel, scroll;
static int capture;             // 0 off, 1 replace, 2 add
static uint32_t prev_dirs;
static uint64_t repeat_at;

static void build_rows(void) {
    row_count = 0;
    for (int i = 0; i < input_action_count() && row_count < MAX_ROWS - 4; i++) {
        rows[row_count].kind = ROW_ACTION;
        rows[row_count++].action = i;
    }
    rows[row_count++].kind = ROW_DEADZONE;
    rows[row_count++].kind = ROW_DEFAULTS;
    rows[row_count++].kind = ROW_CLOSE;
}

int vita_menu_active(void) {
    return open;
}

void vita_menu_open(void) {
    build_rows();
    open = 1;
    capture = 0;
    prev_dirs = SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT;
    if (sel < 0 || sel >= row_count)
        sel = 0;
    overlay_capture_frame();
}

static void close_menu(void) {
    settings_save();
    input_controls_save();
    open = 0;
    capture = 0;
}

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void restore_defaults(void) {
    input_controls_defaults();
    setting_analogDeadzone = 38;
}

void vita_menu_update(uint32_t held, uint32_t pressed) {
    if (!open)
        return;

    if (capture) {
        if (pressed & SCE_CTRL_START) {
            capture = 0;
            return;
        }
        uint32_t b = pressed & input_bindable_buttons();
        if (b) {
            b &= -b; // one button per press
            input_action_bind(rows[sel].action, b, capture == 2);
            capture = 0;
        }
        return;
    }

    if (pressed & (SCE_CTRL_CIRCLE | SCE_CTRL_START)) {
        close_menu();
        return;
    }

    uint32_t dirs = held & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
    uint32_t fresh = dirs & ~prev_dirs;
    prev_dirs = dirs;
    uint64_t now = sceKernelGetProcessTimeWide();
    uint32_t step = 0;
    if (fresh) {
        step = fresh;
        repeat_at = now + REPEAT_DELAY;
    } else if (dirs && now >= repeat_at) {
        step = dirs;
        repeat_at = now + REPEAT_RATE;
    }

    if (step & SCE_CTRL_UP)
        sel = (sel + row_count - 1) % row_count;
    else if (step & SCE_CTRL_DOWN)
        sel = (sel + 1) % row_count;

    RowKind kind = rows[sel].kind;
    switch (kind) {
    case ROW_ACTION:
        if (pressed & SCE_CTRL_CROSS)
            capture = 1;
        else if (pressed & SCE_CTRL_SQUARE)
            capture = 2;
        else if (pressed & SCE_CTRL_TRIANGLE)
            input_action_clear(rows[sel].action);
        break;
    case ROW_DEADZONE:
        if (step & SCE_CTRL_LEFT)
            setting_analogDeadzone = clampi(setting_analogDeadzone - 5, 10, 80);
        else if (step & SCE_CTRL_RIGHT)
            setting_analogDeadzone = clampi(setting_analogDeadzone + 5, 10, 80);
        else if (pressed & SCE_CTRL_CROSS)
            setting_analogDeadzone = (setting_analogDeadzone >= 70) ? 20 : setting_analogDeadzone + 10;
        break;
    case ROW_DEFAULTS:
        if (pressed & SCE_CTRL_CROSS)
            restore_defaults();
        break;
    case ROW_CLOSE:
        if (pressed & SCE_CTRL_CROSS)
            close_menu();
        break;
    }
}

/* --- drawing -------------------------------------------------------------- */

static const char *row_label(int r) {
    switch (rows[r].kind) {
    case ROW_ACTION:   return input_action_label(rows[r].action);
    case ROW_DEADZONE: return "Analog Stick Deadzone";
    case ROW_DEFAULTS: return "Restore Defaults";
    case ROW_CLOSE:    return "Save and Close";
    }
    return "";
}

static void row_value(int r, char *out, size_t size) {
    out[0] = '\0';
    switch (rows[r].kind) {
    case ROW_ACTION:
        if (capture && r == sel)
            snprintf(out, size, "%s", capture == 2 ? "add: press button..." : "press button...");
        else
            input_buttons_text(input_action_buttons(rows[r].action), out, size);
        break;
    case ROW_DEADZONE:
        snprintf(out, size, "< %d >", setting_analogDeadzone);
        break;
    default:
        break;
    }
}

static const char *footer(void) {
    if (capture)
        return "Press the new button    START: cancel";
    switch (rows[sel].kind) {
    case ROW_ACTION:
        return "X: set  []: add  /\\: clear  O: save & close";
    case ROW_DEADZONE:
        return "Left / Right: adjust    O: save & close";
    default:
        return "X: select    O: save & close";
    }
}

static void text_center(float cx, float y, float scale, uint32_t col, const char *s) {
    overlay_text(cx - overlay_text_width(s, scale) * 0.5f, y, scale, col, s);
}

void vita_menu_render(void) {
    if (!open)
        return;

    overlay_begin();
    overlay_background(COL_DIM);

    const float px0 = 40.0f, px1 = OVL_W - 40.0f, py0 = 14.0f, py1 = OVL_H - 14.0f;
    overlay_rect(px0, py0, px1, py1, COL_PANEL);
    overlay_frame(px0, py0, px1, py1, 1.0f, COL_BORDER);

    text_center((px0 + px1) * 0.5f, py0 + 12.0f, TEXT_SCALE, COL_ACCENT, "PS Vita Controls & Settings");
    overlay_rect(px0 + 10.0f, py0 + 38.0f, px1 - 10.0f, py0 + 39.0f, COL_BORDER);

    const float list_top = py0 + 46.0f, list_bottom = py1 - 36.0f;
    int visible = (int) ((list_bottom - list_top) / ROW_H);
    if (visible < 1)
        visible = 1;
    if (sel < scroll)
        scroll = sel;
    if (sel >= scroll + visible)
        scroll = sel - visible + 1;
    scroll = clampi(scroll, 0, row_count > visible ? row_count - visible : 0);

    const float value_x = px0 + (px1 - px0) * 0.52f;
    const float glyph = OVL_GLYPH * TEXT_SCALE;
    for (int i = 0; i < visible && scroll + i < row_count; i++) {
        int r = scroll + i;
        float top = list_top + i * ROW_H;
        float ty = top + (ROW_H - glyph) * 0.5f;

        // Line before the settings block
        if (rows[r].kind != ROW_ACTION && r > 0 && rows[r - 1].kind == ROW_ACTION)
            overlay_rect(px0 + 10.0f, top, px1 - 10.0f, top + 1.0f, COL_BORDER);

        if (r == sel)
            overlay_rect(px0 + 6.0f, top + 1.0f, px1 - 6.0f, top + ROW_H - 1.0f, COL_SEL_BG);

        overlay_text(px0 + 16.0f, ty, TEXT_SCALE, r == sel ? COL_WHITE : COL_GREY, row_label(r));

        char value[96];
        row_value(r, value, sizeof(value));
        overlay_text(value_x, ty, TEXT_SCALE, r == sel ? COL_ACCENT : COL_WHITE, value);
    }

    // Scroll marks
    if (scroll > 0)
        overlay_text(px1 - 28.0f, list_top + 4.0f, TEXT_SCALE, COL_GREY, "^");
    if (scroll + visible < row_count)
        overlay_text(px1 - 28.0f, list_bottom - glyph - 4.0f, TEXT_SCALE, COL_GREY, "v");

    overlay_rect(px0 + 10.0f, py1 - 30.0f, px1 - 10.0f, py1 - 29.0f, COL_BORDER);
    text_center((px0 + px1) * 0.5f, py1 - 22.0f, 1.5f, COL_GREY, footer());

    overlay_end();
}
