/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021-2022 Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/init.h"

#include "utils/dialog.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/settings.h"

#include <string.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/kernel/clib.h>
#include <psp2/power.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>
#include <fios/fios.h>

// Base address for the Android .so to be loaded at
#define LOAD_ADDRESS 0x98000000

so_module denshion_mod;
so_module cocos2d_mod;
so_module game_mod;

void soloader_init_all() {
    // Launch `app0:configurator.bin` on `-config` init param
    sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
    SceAppUtilAppEventParam eventParam;
    sceClibMemset(&eventParam, 0, sizeof(SceAppUtilAppEventParam));
    sceAppUtilReceiveAppEvent(&eventParam);
    if (eventParam.type == 0x05) {
        char buffer[2048];
        sceAppUtilAppEventParseLiveArea(&eventParam, buffer);
        if (strstr(buffer, "-config"))
            sceAppMgrLoadExec("app0:/configurator.bin", NULL, NULL);
    }

    // Set default overclock values
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

#ifdef USE_SCELIBC_IO
    if (fios_init(DATA_PATH) == 0)
        l_success("FIOS initialized.");
#endif

    if (!module_loaded("kubridge")) {
        l_fatal("kubridge is not loaded.");
        fatal_error("Error: kubridge.skprx is not installed.");
    }
    l_success("kubridge check passed.");

    char fname[256];

    // Load CocosDenshion
    sceClibPrintf("Loading libcocosdenshion\n");
    sprintf(fname, "%slibcocosdenshion.so", DATA_PATH);
    if (so_file_load(&denshion_mod, fname, LOAD_ADDRESS) < 0) {
        fatal_error("Error: could not load %s.", fname);
    }

    // Load Cocos2d
    sceClibPrintf("Loading libcocos2d\n");
    sprintf(fname, "%slibcocos2d.so", DATA_PATH);
    if (so_file_load(&cocos2d_mod, fname, LOAD_ADDRESS + 0x1000000) < 0) {
        fatal_error("Error: could not load %s.", fname);
    }

    // Load Game Logic
    sceClibPrintf("Loading libgame_logic\n");
    sprintf(fname, "%slibgame_logic.so", DATA_PATH);
    if (so_file_load(&game_mod, fname, LOAD_ADDRESS + 0x2000000) < 0) {
        fatal_error("Error: could not load %s.", fname);
    }

    settings_load();
    l_success("Settings loaded.");

    so_relocate(&denshion_mod);
    so_relocate(&cocos2d_mod);
    so_relocate(&game_mod);
    l_success("SOs relocated.");

    so_resolve(&denshion_mod, default_dynlib, sizeof(default_dynlib), 0);
    so_resolve(&cocos2d_mod, default_dynlib, sizeof(default_dynlib), 0);
    so_resolve(&game_mod, default_dynlib, sizeof(default_dynlib), 0);
    l_success("SO imports resolved.");

    //so_patch();
    l_success("SO patched.");

    so_flush_caches(&denshion_mod);
    so_flush_caches(&cocos2d_mod);
    so_flush_caches(&game_mod);
    l_success("SO caches flushed.");

    so_initialize(&denshion_mod);
    so_initialize(&cocos2d_mod);
    so_initialize(&game_mod);
    l_success("SOs initialized.");

    gl_preload();
    l_success("OpenGL preloaded.");

    jni_init();
    l_success("FalsoJNI initialized.");
}
