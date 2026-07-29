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

// original.apk/the .obb are no longer required (see Fixes_Log #19): every
// relative file request is served loose from the memory card when
// present, falling back to the apk/obb ZIP only for whatever isn't.
//
// cocos2d::CCFileUtils::getFileData() (so_decompiled/libcocos2d/out_ghidra.c,
// confirmed against the real bin/libcocos2d.so symbol table) sends every
// relative (non-'/'-prefixed) path straight into a ZIP read with NO
// loose-file check ever attempted at this level: a name equal to
// "appConfig.txt" always reads from original.apk, anything else always
// reads from the .obb. Textures/maps/animations reach the memory card
// through a DIFFERENT path that already goes through this project's own
// fopen_soloader()/resolve_data_path() (source/reimpl/io.c), which is why
// those already work loose today -- but this function itself is a second,
// narrower choke point that other assets go through too, one at a time,
// as each gets discovered the hard way on real hardware:
//   - psp2core-1785297093: "Data_960_576/Localization/Spanish/Localizable.loc"
//     (first attempt only matched a "Localization/" PREFIX; the engine
//     already bakes the resolution folder into the string for this
//     request, so it never matched and fell through to the missing .obb)
//   - psp2core-1785297502: "Data_960_576/Logo/logo.png" (LogoScene::init(),
//     a totally different asset -- not appConfig.txt, not Localization,
//     same crash: engine has no graceful handling for getFileData()
//     returning NULL, it always assumes the read succeeded and crashes
//     downstream dereferencing/strlen()-ing the NULL buffer, R0=0xFFFFFFF8).
// Rather than keep special-casing one newly-discovered filename per
// hardware round-trip, this now tries a loose file for ANY relative path,
// falling back to the real apk/obb-zip logic only when no loose copy
// exists -- covering whatever the next surprise turns out to be too.
//
// This hooks getFileData() itself (via the existing, previously-unused
// hook_addr() mechanism) rather than trying to patch every call site: it
// patches the function's own entry once, so it doesn't matter how many
// places inside libcocos2d.so/libgame_logic.so call it. Falls through
// UNCHANGED to the real engine function via a temporary unhook/call/rehook
// (hook_addr() overwrites the target's own instructions, so calling
// "through" the hooked address again would just re-enter this hook;
// so_unhook()/hook_addr() restore/reapply around the real call instead of
// reimplementing the ZIP path by hand). Not thread-safe against a
// concurrent call to the same function on another thread -- asset loading
// is sequential in every log captured so far, but a real hook_addr()
// return-value swap (atomic pointer, not unhook/call/rehook) would be
// worth doing if that ever changes.
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
    // Try loose first for ANY relative path (see the file-level comment
    // above for why this isn't narrowed to specific filenames anymore).
    // The requested name sometimes already includes its resolution folder
    // (e.g. "Data_960_576/Logo/logo.png") and sometimes doesn't (e.g. the
    // bare "appConfig.txt") -- try it as given first, then with
    // Data_960_576/ prepended as a second guess.
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
