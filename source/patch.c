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

// EXPERIMENTAL (branch experimental/loose-appconfig-localization): disassociate
// appConfig.txt and Localization/*.loc from original.apk/the .obb.
//
// cocos2d::CCFileUtils::getFileData() (so_decompiled/libcocos2d/out_ghidra.c,
// confirmed against the real bin/libcocos2d.so symbol table) hardcodes exactly
// two relative-path cases straight into a ZIP read with NO loose-file check
// ever attempted at this level: a name equal to "appConfig.txt" always reads
// "assets/appConfig.txt" from original.apk, and any other relative name (incl.
// "Localization/*.loc") always reads from the .obb. Every other asset type
// (textures, maps, animations...) reaches the memory card through a
// completely different path that already goes through this project's own
// fopen_soloader()/resolve_data_path() (source/reimpl/io.c), which is why
// those already work loose today -- these two are the only files stuck
// depending on the apk/obb no matter what's on the card.
//
// This hooks getFileData() itself (via the existing, previously-unused
// hook_addr() mechanism) rather than trying to patch every call site: it
// patches the function's own entry once, so it doesn't matter how many
// places inside libcocos2d.so/libgame_logic.so call it. For the two known
// cases, it tries loose files (Data_960_576/<name>, then bare <name> under
// DATA_PATH, matching what's already laid out on the card); if found, it
// returns that directly. Anything else -- and a miss on those two cases --
// falls through UNCHANGED to the real engine function, via a temporary
// unhook/call/rehook (hook_addr() overwrites the target's own instructions,
// so calling "through" the hooked address again would just re-enter this
// hook; so_unhook()/hook_addr() restore/reapply around the real call
// instead of reimplementing the ZIP path by hand). Not thread-safe against
// a concurrent call to the same function on another thread -- acceptable
// for this experimental test since asset loading is sequential in every
// log captured so far, but worth a real hook_addr()-return-value swap
// (atomic pointer, not unhook/call/rehook) before considering this
// permanent.
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

static void *hook_getFileData(const char *filename, const char *mode, unsigned long *size) {
    if (filename && filename[0] != '/' &&
        (strcmp(filename, "appConfig.txt") == 0 || strncmp(filename, "Localization/", 13) == 0)) {
        char path[512];
        snprintf(path, sizeof(path), "%sData_960_576/%s", DATA_PATH, filename);
        void *buf = read_loose_file(path, size);
        if (!buf) {
            snprintf(path, sizeof(path), "%s%s", DATA_PATH, filename);
            buf = read_loose_file(path, size);
        }
        if (buf) {
            l_info("hook_getFileData: served \"%s\" loose from %s", filename, path);
            return buf;
        }
        l_warn("hook_getFileData: \"%s\" not found loose (tried Data_960_576/ and %s), falling back to apk/obb",
               filename, DATA_PATH);
    }

    so_unhook(&gGetFileDataHook);
    void *ret = real_getFileData(filename, mode, size);
    gGetFileDataHook = hook_addr((uintptr_t) real_getFileData, (uintptr_t) hook_getFileData);
    return ret;
}

void so_patch(void) {
    uintptr_t addr = so_symbol(&cocos2d_mod, GETFILEDATA_SYM);
    if (!addr) {
        l_warn("so_patch: %s not found, appConfig.txt/Localization loose-file experiment disabled", GETFILEDATA_SYM);
        return;
    }
    real_getFileData = (void *(*)(const char *, const char *, unsigned long *)) addr;
    gGetFileDataHook = hook_addr(addr, (uintptr_t) hook_getFileData);
    l_info("so_patch: hooked %s at 0x%08x", GETFILEDATA_SYM, (unsigned) addr);
}
