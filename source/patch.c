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

extern so_module so_mod;
extern so_module cocos2d_mod;

//! @see docs/comments/patch.c.md#loose-file-override-for-ccfileutilsgetfiledata
#define GETFILEDATA_SYM "_ZN7cocos2d11CCFileUtils11getFileDataEPKcS2_Pm"

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

void so_patch(void) {
    uintptr_t addr = so_symbol(&cocos2d_mod, GETFILEDATA_SYM);
    if (!addr) {
        l_warn("so_patch: %s not found, apk/obb-less loose-file loading disabled (original.apk/.obb still required)", GETFILEDATA_SYM);
        return;
    }
    real_getFileData = (void *(*)(const char *, const char *, unsigned long *)) addr;
    gGetFileDataHook = hook_addr(addr, (uintptr_t) hook_getFileData);
    l_info("so_patch: hooked %s at 0x%08x", GETFILEDATA_SYM, (unsigned) addr);
}
