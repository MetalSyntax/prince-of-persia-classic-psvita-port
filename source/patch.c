/*
 * Copyright (C) 2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  patch.c
 * @brief Patching some of the .so internal functions or bridging them to native
 *        for better compatibility.
 *        @note See docs/comments/patch.c.md for design rationale.
 */

#include <kubridge.h>
#include <so_util/so_util.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/logger.h"
#include "trophies.h"
#include "patch.h"

extern so_module so_mod;
extern so_module cocos2d_mod;
extern so_module game_mod;

//! @see docs/comments/patch.c.md#loose-file-override-for-ccfileutilsgetfiledata
#define GETFILEDATA_SYM "_ZN7cocos2d11CCFileUtils11getFileDataEPKcS2_Pm"
#define UNLOCK_ACHIEVEMENT_SYM "_ZN18AchievementManager17UnLockAchievementEib"

static so_hook gUnLockAchievementHook;
static void (*real_UnLockAchievement)(void *this, int id, int popup) = NULL;

static void hook_UnLockAchievement(void *this, int id, int popup) {
    l_info("Game Logic Achievement triggered: ID %d (popup=%d)", id, popup);

    if (real_UnLockAchievement) {
        so_unhook(&gUnLockAchievementHook);
        real_UnLockAchievement(this, id, popup);
        gUnLockAchievementHook = hook_addr((uintptr_t) real_UnLockAchievement, (uintptr_t) hook_UnLockAchievement);
    }

    // Map 0-indexed game achievements (0..16) to Vita trophy IDs (1..17)
    if (id >= 0 && id <= 16) {
        trophies_unlock((uint32_t)(id + 1));
    }
}

static so_hook gGetFileDataHook;
static void *(*real_getFileData)(const char *, const char *, unsigned long *) = NULL;

static void *read_loose_file(const char *path, unsigned long *out_size) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return NULL;
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (size <= 0) {
        sceIoClose(fd);
        return NULL;
    }
    void *buf = malloc((size_t) size);
    if (!buf) {
        sceIoClose(fd);
        return NULL;
    }
    int n = sceIoRead(fd, buf, (SceSize) size);
    sceIoClose(fd);
    if (n != (int) size) {
        free(buf);
        return NULL;
    }
    if (out_size)
        *out_size = (unsigned long) size;
    return buf;
}

/** @brief Serves a relative asset path loose from the memory card when possible.
 *  @note See docs/comments/patch.c.md#hook_getfiledata--loose-first-path-resolution-logic */
static void *hook_getFileData(const char *filename, const char *mode, unsigned long *size) {
    //! @see docs/comments/patch.c.md#hook_getfiledata--loose-first-path-resolution-logic
    if (filename && filename[0] != '/') {
        char path[512];
        snprintf(path, sizeof(path), "%s%s", DATA_PATH, filename);
        void *buf = read_loose_file(path, size);
        if (!buf) {
            snprintf(path, sizeof(path), "%sData_960_576/%s", DATA_PATH, filename);
            buf = read_loose_file(path, size);
        }
        if (buf) {
            l_info("hook_getFileData: served \"%s\" loose from %s", filename, path);
            return buf;
        }
        l_warn("hook_getFileData: \"%s\" not found loose (tried %s%s and Data_960_576/), falling back to apk/obb",
               filename, DATA_PATH, filename);
    }

    so_unhook(&gGetFileDataHook);
    void *ret = real_getFileData(filename, mode, size);
    gGetFileDataHook = hook_addr((uintptr_t) real_getFileData, (uintptr_t) hook_getFileData);
    return ret;
}

#define SET_CONTROLS_POS_SYM "_ZN13ControlsLayer14setControlsPosEb"
#define SHARED_CONTROLS_LAYER_SYM "_ZN13ControlsLayer19sharedControlsLayerEv"
#define CCSPRITE_SET_OPACITY_SYM "_ZN7cocos2d8CCSprite10setOpacityEh"
#define CCMENUITEMSPRITE_SET_OPACITY_SYM "_ZN7cocos2d16CCMenuItemSprite10setOpacityEh"

static void *(*real_sharedControlsLayer)(void) = NULL;
static void (*real_CCSprite_setOpacity)(void *self, unsigned char opacity) = NULL;
static void (*real_CCMenuItemSprite_setOpacity)(void *self, unsigned char opacity) = NULL;

static so_hook gSetControlsPosHook;
static void (*real_setControlsPos)(void *this, int param) = NULL;

static int g_controlsVisible = 0;

// Fixed 1% opacity (~2/255 = 0.78%) for hidden virtual controls so that even if
// checkpoint reload after death re-enables their visibility, they remain completely
// invisible on screen while preserving internal touch/engine mechanics.
#define CONTROLS_OPACITY_HIDDEN  ((unsigned char) 2)
#define CONTROLS_OPACITY_VISIBLE ((unsigned char) 255)

void controls_set_visible(int visible) {
    g_controlsVisible = visible;
}

int controls_is_visible(void) {
    return g_controlsVisible;
}

void *controls_get_layer(void) {
    if (real_sharedControlsLayer) {
        return real_sharedControlsLayer();
    }
    return NULL;
}

void controls_update_opacity(void *controlsLayer, int visible) {
    if (!controlsLayer) return;

    unsigned char opacity = visible ? CONTROLS_OPACITY_VISIBLE : CONTROLS_OPACITY_HIDDEN;

    if (real_CCSprite_setOpacity) {
        // 0x158-0x164 are cocos2d::CCSprite (move_slider_base, move_slider,
        // joystick_base, joystick).
        static const int kSpriteOffsets[4] = {0x158, 0x15c, 0x160, 0x164};
        for (int b = 0; b < 4; b++) {
            void *sprite = *(void **)((char *)controlsLayer + kSpriteOffsets[b]);
            if (sprite) real_CCSprite_setOpacity(sprite, opacity);
        }
    }

    if (real_CCMenuItemSprite_setOpacity) {
        // 0x168/0x16c (control_arrow_left/right) and 0x174-0x188 (crouch, jump,
        // interact, attack, defend, sheath) are all cocos2d::CCMenuItemImage
        // (which inherits from CCMenuItemSprite).
        static const int kButtonOffsets[8] = {0x168, 0x16c,
                                              0x174, 0x178, 0x17c, 0x180, 0x184, 0x188};
        for (int b = 0; b < 8; b++) {
            void *item = *(void **)((char *)controlsLayer + kButtonOffsets[b]);
            if (item) real_CCMenuItemSprite_setOpacity(item, opacity);
        }
    }
}

static void hook_setControlsPos(void *this, int param) {
    if (real_setControlsPos) {
        so_unhook(&gSetControlsPosHook);
        real_setControlsPos(this, param);
        gSetControlsPosHook = hook_addr((uintptr_t) real_setControlsPos, (uintptr_t) hook_setControlsPos);
    }

    // After setControlsPos (called by ControlsLayer::reset() upon death / checkpoint reload),
    // movement controls at 0x158..0x16c have their visibility restored by the engine.
    // Re-apply current opacity (fixed 1% if hidden) to ensure they stay invisible if controls are disabled.
    controls_update_opacity(this, g_controlsVisible);
}

void so_patch(void) {
    uintptr_t addr = so_symbol(&cocos2d_mod, GETFILEDATA_SYM);
    if (!addr) {
        l_warn("so_patch: %s not found, apk/obb-less loose-file loading disabled (original.apk/.obb still required)", GETFILEDATA_SYM);
    } else {
        real_getFileData = (void *(*)(const char *, const char *, unsigned long *)) addr;
        gGetFileDataHook = hook_addr(addr, (uintptr_t) hook_getFileData);
        l_info("so_patch: hooked %s at 0x%08x", GETFILEDATA_SYM, (unsigned) addr);
    }

    uintptr_t achv_addr = so_symbol(&game_mod, UNLOCK_ACHIEVEMENT_SYM);
    if (!achv_addr) {
        l_warn("so_patch: %s not found in libgame_logic", UNLOCK_ACHIEVEMENT_SYM);
    } else {
        real_UnLockAchievement = (void (*)(void *, int, int)) achv_addr;
        gUnLockAchievementHook = hook_addr(achv_addr, (uintptr_t) hook_UnLockAchievement);
        l_info("so_patch: hooked %s at 0x%08x", UNLOCK_ACHIEVEMENT_SYM, (unsigned) achv_addr);
    }

    real_sharedControlsLayer = (void *(*)(void)) so_symbol(&game_mod, SHARED_CONTROLS_LAYER_SYM);
    real_CCSprite_setOpacity = (void (*)(void *, unsigned char)) so_symbol(&cocos2d_mod, CCSPRITE_SET_OPACITY_SYM);
    real_CCMenuItemSprite_setOpacity = (void (*)(void *, unsigned char)) so_symbol(&cocos2d_mod, CCMENUITEMSPRITE_SET_OPACITY_SYM);

    uintptr_t set_controls_pos_addr = so_symbol(&game_mod, SET_CONTROLS_POS_SYM);
    if (!set_controls_pos_addr) {
        l_warn("so_patch: %s not found in libgame_logic", SET_CONTROLS_POS_SYM);
    } else {
        real_setControlsPos = (void (*)(void *, int)) set_controls_pos_addr;
        gSetControlsPosHook = hook_addr(set_controls_pos_addr, (uintptr_t) hook_setControlsPos);
        l_info("so_patch: hooked %s at 0x%08x", SET_CONTROLS_POS_SYM, (unsigned) set_controls_pos_addr);
    }
}

