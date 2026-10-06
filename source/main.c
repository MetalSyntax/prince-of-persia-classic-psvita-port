#include <vitasdk.h>
#include <vitaGL.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include "utils/init.h"
#include "utils/glutil.h"
#include "utils/utils.h"
#include "utils/dialog.h"
#include "utils/logger.h"
#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include "audio.h"
#include "video.h"
#include "trophies.h"
#include "input.h"
#include "vita_menu.h"
#include "overlay.h"
#include "utils/settings.h"
#include "patch.h"

int _newlib_heap_size_user = 256 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif

extern so_module denshion_mod;
extern so_module cocos2d_mod;
extern so_module game_mod;

int main() {
    soloader_init_all();
    l_success("soloader_init_all() done -- entering game bring-up sequence.");
    audio_init();
    video_init();

    // Touch sampling is off by default -- sceTouchPeek() below always
    // reports reportNum=0 (no touches, ever) until this is called.
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

    JNIEnv *jniEnv = &jni;

    //! @see docs/comments/main.c.md#jni_onload--calling-every-module-that-exports-it
    so_module *jni_onload_mods[] = { &denshion_mod, &cocos2d_mod, &game_mod };
    const char *jni_onload_names[] = { "libcocosdenshion", "libcocos2d", "libgame_logic" };
    for (int i = 0; i < 3; i++) {
        int (* JNI_OnLoad)(void *jvm, void *reserved) = (void *)so_symbol(jni_onload_mods[i], "JNI_OnLoad");
        if (JNI_OnLoad) {
            JNI_OnLoad(&jvm, NULL);
            l_debug("JNI_OnLoad(%s) called.", jni_onload_names[i]);
        }
    }

    // Resolve Native methods
    void (* nativeSetPaths)(JNIEnv *env, jobject obj, jstring apkFilePath, jstring apkSourceDir, jstring device) = (void *)so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetPaths");
    void (* nativeSetPackageName)(JNIEnv *env, jobject obj, jstring packageName) = (void *)so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetPackageName");
    void (* nativeSetIsGoogleLauncherBuild)(JNIEnv *env, jobject obj, jboolean isGoogleLauncherBuild) = (void *)so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetIsGoogleLauncherBuild");
    void (* nativeSetNumOfCPUCores)(JNIEnv *env, jobject obj, jint cores) = (void *)so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetNumOfCPUCores");
    void (* nativeSetDensityScaleValue)(JNIEnv *env, jobject obj, jfloat scale) = (void *)so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetDensityScaleValue");
    void (* nativeSetDevicePixelsPerInch)(JNIEnv *env, jobject obj, jfloat ydpi) = (void *)so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetDevicePixelsPerInch");
    void (* SetControlInVisible)(JNIEnv *env, jobject obj) = (void *)so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxActivity_SetControlInVisible");
    
    void (* nativeInit)(JNIEnv *env, jobject obj, jint width, jint height) = (void *)so_symbol(&game_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit");
    void (* nativeRender)(JNIEnv *env, jobject obj) = (void *)so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender");
    
    void (* nativeTouchesBegin)(JNIEnv *env, jobject obj, jint id, jfloat x, jfloat y) = (void *)so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin");
    void (* nativeTouchesMove)(JNIEnv *env, jobject obj, jint id, jfloat x, jfloat y) = (void *)so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesMove");
    void (* nativeTouchesEnd)(JNIEnv *env, jobject obj, jint id, jfloat x, jfloat y) = (void *)so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd");
    
    void (* nativeKeyDown)(JNIEnv *env, jobject obj, jint keyCode) = (void *)so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown");
    void (* nativeKeyUp)(JNIEnv *env, jobject obj, jint keyCode) = (void *)so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyUp");

    //! @see docs/comments/main.c.md#controls-visibility-toggle--l1r1-combo
    void (* SetControlVisible)(JNIEnv *env, jobject obj) = (void *)so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxActivity_SetControlVisible");

    // Initialize Cocos2d-x environment
    if (nativeSetPaths) {
        //! @see docs/comments/main.c.md#nativesetpaths--argument-semantics-and-originalapk-fallback
        if (!file_exists(DATA_PATH "original.apk") &&
            !file_exists(DATA_PATH "Data_960_576/appConfig.txt") &&
            !file_exists(DATA_PATH "appConfig.txt")) {
            fatal_error("Error: no original.apk AND no loose appConfig.txt found.\n"
                        "Either copy the original game's .apk to " DATA_PATH "original.apk "
                        "(see INSTALL_HARDWARE.md step 2.2), or place a loose copy of "
                        "appConfig.txt at " DATA_PATH "Data_960_576/appConfig.txt or "
                        DATA_PATH "appConfig.txt (hook_getFileData in source/patch.c "
                        "will pick it up from either).");
        } else if (!file_exists(DATA_PATH "original.apk")) {
            l_info("original.apk missing, but a loose appConfig.txt was found -- "
                   "continuing without original.apk (relying on hook_getFileData, source/patch.c).");
        }
        jstring apkFilePathStr = (*jniEnv)->NewStringUTF(jniEnv, DATA_PATH);
        jstring apkSourceDirStr = (*jniEnv)->NewStringUTF(jniEnv, DATA_PATH "original.apk");
        //! @see docs/comments/main.c.md#native-xperia-play-control-path
        // The device name is used for exactly one thing: strcmp(device, "R800i")
        // sets CCDirector+0xad (IsXperia). "R800i" is the Xperia PLAY's model number,
        // and it unlocks the game's own physical-gamepad code path.
        jstring deviceStr = (*jniEnv)->NewStringUTF(jniEnv, "R800i");
        nativeSetPaths(jniEnv, NULL, apkFilePathStr, apkSourceDirStr, deviceStr);
        l_success("nativeSetPaths(%s, %soriginal.apk, R800i) done -- IsXperia set.", DATA_PATH, DATA_PATH);
    }

    if (nativeSetPackageName) {
        jstring pkgStr = (*jniEnv)->NewStringUTF(jniEnv, "org.ubisoft.premium.POPClassic");
        nativeSetPackageName(jniEnv, NULL, pkgStr);
    }
    if (nativeSetIsGoogleLauncherBuild) nativeSetIsGoogleLauncherBuild(jniEnv, NULL, JNI_TRUE);
    if (nativeSetNumOfCPUCores) nativeSetNumOfCPUCores(jniEnv, NULL, 4);
    if (nativeSetDensityScaleValue) nativeSetDensityScaleValue(jniEnv, NULL, 1.3f);
    if (nativeSetDevicePixelsPerInch) nativeSetDevicePixelsPerInch(jniEnv, NULL, 240.0f);
    if (SetControlInVisible) SetControlInVisible(jniEnv, NULL);
    controls_set_visible(0);

    gl_init();
    l_success("gl_init() done.");

    trophies_init();
    
    settings_load();
    input_init();

    if (nativeInit) {
        nativeInit(jniEnv, NULL, 960, 544);
        l_success("nativeInit(960, 544) done -- game should now be constructing its first scene.");
    }

    l_success("Entering main loop.");
    int lastX[5] = {-1, -1, -1, -1, -1};
    int lastY[5] = {-1, -1, -1, -1, -1};
    //! @see docs/comments/main.c.md#touch-slot-registry--hardware-id-vs-engine-slot-index
    int slotHwId[5] = {-1, -1, -1, -1, -1};
    uint32_t oldpad = 0;
    int frame = 0;
    int last_logged_report_num = -1;
    uint32_t last_logged_pad_buttons = 0;
    // main() calls SetControlInVisible() above, so the on-screen controls start hidden.
    int controlsVisible = 0;
    //! @see docs/comments/main.c.md#native-xperia-play-control-path
    // Per-direction hold tracking for the walk -> run promotion. Index 0 = Left, 1 = Right.
    const uint64_t RUN_PROMOTE_US = 250000; // matches CCDelayTime(0.25f) in ControlsLayer::tick
    uint64_t dirHeldSince[2] = {0, 0};
    int dirRunSent[2] = {0, 0};
    //! @see docs/comments/main.c.md#jump-plus-walk-diagnostic-log
    int last_logged_cross_combo = -1;


    while (1) {
        SceTouchData touch;
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

        SceCtrlData debug_pad;
        sceCtrlPeekBufferPositive(0, &debug_pad, 1);
        //! @see docs/comments/main.c.md#edge-triggered-input-logging
        if ((frame++ % 120) == 0
            || touch.reportNum != last_logged_report_num
            || debug_pad.buttons != last_logged_pad_buttons) {
            l_debug("input tick: touch.reportNum=%i pad.buttons=0x%08x",
                    touch.reportNum, (unsigned int) debug_pad.buttons);
            last_logged_report_num = touch.reportNum;
            last_logged_pad_buttons = debug_pad.buttons;
        }

        SceCtrlData pad;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        uint32_t current_pad = pad.buttons;

        // Poll Rear Touch (4 quadrants: L2, R2, L3, R3)
        SceTouchData touch_back;
        if (sceTouchPeek(SCE_TOUCH_PORT_BACK, &touch_back, 1) > 0) {
            for (int r = 0; r < touch_back.reportNum && r < SCE_TOUCH_MAX_REPORT; r++) {
                int rx = touch_back.report[r].x;
                int ry = touch_back.report[r].y;
                if (ry < 544) {
                    if (rx < 960) current_pad |= CUSTOM_BTN_L2;
                    else         current_pad |= CUSTOM_BTN_R2;
                } else {
                    if (rx < 960) current_pad |= CUSTOM_BTN_L3;
                    else         current_pad |= CUSTOM_BTN_R3;
                }
            }
        }

        // PSTV DualShock 3/4 hardware L2/R2 triggers
        if (current_pad & SCE_CTRL_L2) current_pad |= CUSTOM_BTN_L2;
        if (current_pad & SCE_CTRL_R2) current_pad |= CUSTOM_BTN_R2;

        int dz = setting_analogDeadzone > 0 ? setting_analogDeadzone : 38;
        if (pad.lx < (128 - dz)) current_pad |= SCE_CTRL_LEFT;
        if (pad.lx > (128 + dz)) current_pad |= SCE_CTRL_RIGHT;
        if (pad.ly < (128 - dz)) current_pad |= SCE_CTRL_UP;
        if (pad.ly > (128 + dz)) current_pad |= SCE_CTRL_DOWN;

        uint32_t pressed = current_pad & ~oldpad;
        uint32_t released = oldpad & ~current_pad;

        static int start_combo = 0;
        static int start_tap = 0;

        // Release BACK key if START was tapped last frame
        if (start_tap && nativeKeyUp) {
            nativeKeyUp(jniEnv, NULL, 4);
            start_tap = 0;
        }

        if (vita_menu_active()) {
            if (!(current_pad & SCE_CTRL_START))
                start_combo = 0;
            vita_menu_update(current_pad, pressed);
            vita_menu_render();
            if (!vita_menu_active() && (current_pad & SCE_CTRL_START))
                start_combo = 1;
            oldpad = current_pad;
            continue;
        }

        // START + SELECT (either order) opens the port menu
        if (((current_pad & SCE_CTRL_START) && (pressed & SCE_CTRL_SELECT)) ||
            ((current_pad & SCE_CTRL_SELECT) && (pressed & SCE_CTRL_START))) {
            start_combo = 1;
            vita_menu_open();
            oldpad = current_pad;
            continue;
        }

        if (released & SCE_CTRL_START) {
            if (!start_combo && nativeKeyDown) {
                nativeKeyDown(jniEnv, NULL, 4);
                start_tap = 1;
            }
            start_combo = 0;
        }

        //! @see docs/comments/main.c.md#virtual-finger-slots-and-the-cc_max_touches-limit
        int reportHwId[5], reportX[5], reportY[5], reportCount = 0;
        for (int r = 0; r < touch.reportNum && reportCount < 5; r++) {
            reportHwId[reportCount] = touch.report[r].id;
            reportX[reportCount] = (int)((float)touch.report[r].x * 960.0f / 1920.0f);
            reportY[reportCount] = (int)((float)touch.report[r].y * 544.0f / 1088.0f);
            reportCount++;
        }

        int seenThisFrame[5] = {0, 0, 0, 0, 0};

        for (int k = 0; k < reportCount; k++) {
            int hwId = reportHwId[k];
            int x = reportX[k];
            int y = reportY[k];

            int slot = -1;
            for (int s = 0; s < 5; s++) {
                if (slotHwId[s] == hwId) { slot = s; break; }
            }
            if (slot == -1) {
                for (int s = 0; s < 5; s++) {
                    if (slotHwId[s] == -1) { slot = s; break; }
                }
                if (slot == -1) continue; // more than 5 simultaneous touches -- drop it
                slotHwId[slot] = hwId;
                lastX[slot] = -1;
                lastY[slot] = -1;
            }
            seenThisFrame[slot] = 1;

            if (lastX[slot] == -1 || lastY[slot] == -1) {
                if (nativeTouchesBegin) nativeTouchesBegin(jniEnv, NULL, slot, (jfloat)x, (jfloat)y);
            } else if (lastX[slot] != x || lastY[slot] != y) {
                if (nativeTouchesMove) nativeTouchesMove(jniEnv, NULL, slot, (jfloat)x, (jfloat)y);
            }
            lastX[slot] = x;
            lastY[slot] = y;
        }

        for (int s = 0; s < 5; s++) {
            if (slotHwId[s] != -1 && !seenThisFrame[s]) {
                if (nativeTouchesEnd) nativeTouchesEnd(jniEnv, NULL, s, (jfloat)lastX[s], (jfloat)lastY[s]);
                lastX[s] = -1;
                lastY[s] = -1;
                slotHwId[s] = -1;
            }
        }

        void *ctrlLayer = controls_get_layer();
        static void *lastCtrlLayer = NULL;
        static int lastVisible = -1;
        if (ctrlLayer && (ctrlLayer != lastCtrlLayer || controlsVisible != lastVisible)) {
            controls_update_opacity(ctrlLayer, controlsVisible);
            lastCtrlLayer = ctrlLayer;
            lastVisible = controlsVisible;
        }

        if (nativeKeyDown && nativeKeyUp) {
            // SELECT alone sends KEYCODE_MENU (82)
            if (!(current_pad & SCE_CTRL_START)) {
                if ((current_pad & SCE_CTRL_SELECT) && !(oldpad & SCE_CTRL_SELECT)) nativeKeyDown(jniEnv, NULL, 82);
                if (!(current_pad & SCE_CTRL_SELECT) && (oldpad & SCE_CTRL_SELECT)) nativeKeyUp(jniEnv, NULL, 82);
            }

            // DPAD UP (ACT_UP): Keycode 19
            uint32_t btnUp = input_action_buttons(ACT_UP);
            if ((current_pad & btnUp) && !(oldpad & btnUp)) nativeKeyDown(jniEnv, NULL, 19);
            if (!(current_pad & btnUp) && (oldpad & btnUp)) nativeKeyUp(jniEnv, NULL, 19);

            // Left/Right: walk tap / run hold promotion
            const uint64_t nowUs = sceKernelGetProcessTimeWide();
            for (int d = 0; d < 2; d++) {
                const int act = (d == 0) ? ACT_LEFT : ACT_RIGHT;
                const int keycode = (d == 0) ? 21 : 22;
                uint32_t btnDir = input_action_buttons(act);
                if ((current_pad & btnDir) && !(oldpad & btnDir)) {
                    nativeKeyDown(jniEnv, NULL, keycode); // first call -> AddEvent(WALK)
                    dirHeldSince[d] = nowUs;
                    dirRunSent[d] = 0;
                } else if ((current_pad & btnDir) && !dirRunSent[d]
                           && nowUs - dirHeldSince[d] >= RUN_PROMOTE_US) {
                    nativeKeyDown(jniEnv, NULL, keycode); // second call -> AddEvent(RUN)
                    dirRunSent[d] = 1;
                } else if (!(current_pad & btnDir) && (oldpad & btnDir)) {
                    nativeKeyUp(jniEnv, NULL, keycode);
                    dirRunSent[d] = 0;
                }
            }

            // Roll / Crouch Down (ACT_ROLL): Keycode 20
            uint32_t btnRoll = input_action_buttons(ACT_ROLL);
            int wantRoll = (current_pad & btnRoll) != 0;
            int wantedRoll = (oldpad & btnRoll) != 0;
            if (wantRoll && !wantedRoll) nativeKeyDown(jniEnv, NULL, 20);
            if (!wantRoll && wantedRoll) nativeKeyUp(jniEnv, NULL, 20);

            // Jump / Attack / Confirm (ACT_JUMP): Keycode 23
            uint32_t btnJump = input_action_buttons(ACT_JUMP);
            if ((current_pad & btnJump) && !(oldpad & btnJump)) {
                nativeKeyDown(jniEnv, NULL, 23); // DPAD_CENTER -> ControlsLayer::keyXClicked (jump / attack / menu confirm)
            }
            if (!(current_pad & btnJump) && (oldpad & btnJump)) {
                nativeKeyUp(jniEnv, NULL, 23);   // -> ControlsLayer::keyRemoveX
            }
            
            // Crouch / Defend (ACT_CROUCH): Keycode 99 (BUTTON_C)
            uint32_t btnCrouch = input_action_buttons(ACT_CROUCH);
            if ((current_pad & btnCrouch) && !(oldpad & btnCrouch)) nativeKeyDown(jniEnv, NULL, 99);
            if (!(current_pad & btnCrouch) && (oldpad & btnCrouch)) nativeKeyUp(jniEnv, NULL, 99);

            // Interact / Sheathe (ACT_INTERACT): Keycode 100 (BUTTON_Z)
            uint32_t btnInteract = input_action_buttons(ACT_INTERACT);
            if ((current_pad & btnInteract) && !(oldpad & btnInteract)) nativeKeyDown(jniEnv, NULL, 100);
            if (!(current_pad & btnInteract) && (oldpad & btnInteract)) nativeKeyUp(jniEnv, NULL, 100);

            // Toggle on-screen HUD (ACT_TOGGLE_HUD)
            uint32_t hudMask = input_action_buttons(ACT_TOGGLE_HUD);
            int comboHeldNow = hudMask && ((current_pad & hudMask) == hudMask);
            int comboHeldBefore = hudMask && ((oldpad & hudMask) == hudMask);
            if (comboHeldNow && !comboHeldBefore && SetControlVisible && SetControlInVisible) {
                controlsVisible = !controlsVisible;
                if (controlsVisible) SetControlVisible(jniEnv, NULL);
                else                 SetControlInVisible(jniEnv, NULL);
                controls_set_visible(controlsVisible);
                if (ctrlLayer) controls_update_opacity(ctrlLayer, controlsVisible);
                l_debug("controls visibility toggled: %s", controlsVisible ? "visible" : "hidden");
            }
        }
        oldpad = current_pad;

        //! @see docs/comments/main.c.md#jump-plus-walk-diagnostic-log
        int cross_combo = ((current_pad & SCE_CTRL_CROSS) != 0) << 2
                         | ((current_pad & SCE_CTRL_LEFT) != 0) << 1
                         | ((current_pad & SCE_CTRL_RIGHT) != 0);
        if ((current_pad & SCE_CTRL_CROSS) && cross_combo != last_logged_cross_combo) {
            l_debug("cross+dpad tick: cross=1 left=%i right=%i",
                    (current_pad & SCE_CTRL_LEFT) != 0, (current_pad & SCE_CTRL_RIGHT) != 0);
        }
        last_logged_cross_combo = cross_combo;

        if (nativeRender) {
            nativeRender(jniEnv, NULL);
        }
        
        gl_swap();
    }

    audio_shutdown();
    video_shutdown();
    sceKernelExitDeleteThread(0);
}
