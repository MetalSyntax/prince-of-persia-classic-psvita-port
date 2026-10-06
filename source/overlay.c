/*
 * Copyright (C) 2026 Prince of Persia Classic Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  overlay.c
 * @brief 2D overlay for the in-game port menu (see overlay.h).
 *
 * Drawn on the CPU straight into the framebuffer on screen, without touching
 * the game engine's GL state.
 */

#include "overlay.h"

#include "utils/font8x8.h"
#include "utils/logger.h"

#include <psp2/display.h>
#include <psp2/gxm.h>
#include <vitaGL.h>

#include <stdlib.h>
#include <string.h>

static uint32_t *frame_bg;      // last game frame, A8B8G8R8
static uint32_t *dim_bg;        // frame_bg with the dim colour applied
static uint32_t dim_color;
static uint32_t *canvas;        // menu being drawn
static uint32_t *shown;         // what is on screen now
static SceDisplayFrameBuf fb;
static int need_grab, frame_valid, fb_valid;

/* --- frame grab ------------------------------------------------------------- */

void overlay_capture_frame(void) {
    need_grab = 1;
}

int overlay_has_frame(void) {
    return frame_valid || fb_valid;
}

static void grab_frame(void) {
    need_grab = 0;
    frame_valid = 0;
    fb_valid = 0;
    dim_color = 0;

    // Wait for pending GPU and display flips
    glFinish();
    sceGxmDisplayQueueFinish();
    sceDisplayWaitVblankStart();

    memset(&fb, 0, sizeof(fb));
    fb.size = sizeof(fb);
    int ret = sceDisplayGetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    if (ret < 0 || !fb.base) {
        l_warn("overlay: sceDisplayGetFrameBuf returned 0x%08x (base %p)", ret, fb.base);
        return;
    }
    fb_valid = 1;

    if (!frame_bg) frame_bg = (uint32_t *) malloc(OVL_W * OVL_H * 4);
    if (!dim_bg)   dim_bg   = (uint32_t *) malloc(OVL_W * OVL_H * 4);
    if (!canvas)   canvas   = (uint32_t *) malloc(OVL_W * OVL_H * 4);
    if (!shown)    shown    = (uint32_t *) malloc(OVL_W * OVL_H * 4);

    if (!frame_bg || !dim_bg || !canvas || !shown) {
        l_error("overlay: out of memory for overlay buffers");
        fb_valid = 0;
        return;
    }

    if (fb.width >= OVL_W && fb.height >= OVL_H && fb.pitch >= OVL_W &&
        fb.pixelformat == SCE_DISPLAY_PIXELFORMAT_A8B8G8R8) {
        const uint32_t *src = (const uint32_t *) fb.base;
        for (int y = 0; y < OVL_H; y++)
            memcpy(frame_bg + y * OVL_W, src + y * fb.pitch, OVL_W * 4);
        memcpy(shown, frame_bg, OVL_W * OVL_H * 4);
        frame_valid = 1;
    } else {
        // Fallback: fill background with dark theme
        for (int i = 0; i < OVL_W * OVL_H; i++)
            frame_bg[i] = 0xff141a1e;
        memcpy(shown, frame_bg, OVL_W * OVL_H * 4);
        frame_valid = 1;
    }
}

/* --- pixels ----------------------------------------------------------------- */

static inline uint32_t div255(uint32_t x) {
    x += 128;
    return (x + (x >> 8)) >> 8;
}

// 0xAARRGGBB over an A8B8G8R8 pixel.
static inline uint32_t blend(uint32_t dst, uint32_t c) {
    uint32_t a = c >> 24;
    uint32_t r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
    if (a == 0xff)
        return 0xff000000 | (b << 16) | (g << 8) | r;
    uint32_t ia = 255 - a;
    uint32_t dr = dst & 0xff, dg = (dst >> 8) & 0xff, db = (dst >> 16) & 0xff;
    dr = div255(r * a + dr * ia);
    dg = div255(g * a + dg * ia);
    db = div255(b * a + db * ia);
    return 0xff000000 | (db << 16) | (dg << 8) | dr;
}

static inline int px(float v, int max) {
    int i = (int) (v + 0.5f);
    return i < 0 ? 0 : (i > max ? max : i);
}

/* --- begin / end ------------------------------------------------------------ */

void overlay_begin(void) {
    if (need_grab || !frame_valid)
        grab_frame();
}

void overlay_end(void) {
    if (fb_valid && canvas && shown) {
        if (memcmp(canvas, shown, OVL_W * OVL_H * 4) != 0) {
            sceDisplayWaitVblankStart();
            uint32_t *dst = (uint32_t *) fb.base;
            for (int y = 0; y < OVL_H; y++)
                memcpy(dst + y * fb.pitch, canvas + y * OVL_W, OVL_W * 4);
            memcpy(shown, canvas, OVL_W * OVL_H * 4);
        } else {
            sceDisplayWaitVblankStart();
        }
    } else {
        sceDisplayWaitVblankStart();
    }
}

/* --- primitives ------------------------------------------------------------- */

void overlay_background(uint32_t dim) {
    if (!canvas)
        return;
    if (frame_valid && frame_bg && dim_bg) {
        if (dim != dim_color) {
            for (int i = 0; i < OVL_W * OVL_H; i++)
                dim_bg[i] = blend(frame_bg[i], dim);
            dim_color = dim;
        }
        memcpy(canvas, dim_bg, OVL_W * OVL_H * 4);
    } else {
        for (int i = 0; i < OVL_W * OVL_H; i++)
            canvas[i] = 0xff141a1e;
    }
}

void overlay_rect(float x0, float y0, float x1, float y1, uint32_t c) {
    if (!canvas)
        return;
    int ix0 = px(x0, OVL_W), ix1 = px(x1, OVL_W);
    int iy0 = px(y0, OVL_H), iy1 = px(y1, OVL_H);
    for (int y = iy0; y < iy1; y++) {
        uint32_t *row = canvas + y * OVL_W;
        for (int x = ix0; x < ix1; x++)
            row[x] = blend(row[x], c);
    }
}

void overlay_frame(float x0, float y0, float x1, float y1, float t, uint32_t c) {
    overlay_rect(x0, y0, x1, y0 + t, c);
    overlay_rect(x0, y1 - t, x1, y1, c);
    overlay_rect(x0, y0, x0 + t, y1, c);
    overlay_rect(x1 - t, y0, x1, y1, c);
}

float overlay_text_width(const char *s, float scale) {
    if (!s) return 0.0f;
    return strlen(s) * OVL_GLYPH * scale;
}

void overlay_text(float x, float y, float scale, uint32_t c, const char *s) {
    if (!canvas || !s)
        return;
    const float g = OVL_GLYPH * scale;
    int gs = (int) (g + 0.5f);
    if (gs < 1)
        return;
    int iy = (int) (y + 0.5f);
    for (; *s; s++, x += g) {
        unsigned ch = (unsigned char) *s;
        if (ch == ' ')
            continue;
        const uint8_t *glyph = &font8x8[ch * 8];
        int ix = (int) (x + 0.5f);
        for (int py = 0; py < gs; py++) {
            int ty = iy + py;
            if (ty < 0 || ty >= OVL_H)
                continue;
            uint8_t bits = glyph[py * 8 / gs];
            if (!bits)
                continue;
            uint32_t *row = canvas + ty * OVL_W;
            for (int pxl = 0; pxl < gs; pxl++) {
                int tx = ix + pxl;
                if (tx < 0 || tx >= OVL_W)
                    continue;
                if (bits & (0x80 >> (pxl * 8 / gs)))
                    row[tx] = blend(row[tx], c);
            }
        }
    }
}
