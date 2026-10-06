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

    gl_init();
    l_success("gl_init() done.");

    trophies_init();
    
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

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

        //! @see docs/comments/main.c.md#virtual-finger-slots-and-the-cc_max_touches-limit
        int reportHwId[5], reportX[5], reportY[5], reportCount = 0;
        for (int r = 0; r < touch.reportNum && reportCount < 5; r++) {
            reportHwId[reportCount] = touch.report[r].id;
            reportX[reportCount] = (int)((float)touch.report[r].x * 960.0f / 1920.0f);
            reportY[reportCount] = (int)((float)touch.report[r].y * 544.0f / 1088.0f);
            reportCount++;
        }

        // Map Left Analog Stick to D-Pad buttons so they share the same logic
        if (pad.lx < 90) current_pad |= SCE_CTRL_LEFT;
        if (pad.lx > 165) current_pad |= SCE_CTRL_RIGHT;
        if (pad.ly < 90) current_pad |= SCE_CTRL_UP;
        if (pad.ly > 165) current_pad |= SCE_CTRL_DOWN;

        //! @see docs/comments/main.c.md#native-xperia-play-control-path
        // Movement no longer injects synthetic touches at guessed screen coordinates.
        // Left/Right go through keycodes 21/22 -> ControlsLayer::keyLeft/keyRightClicked,
        // which is the same entry point the on-screen arrows use, so the touch report
        // below is real fingers only.

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

        if (nativeKeyDown && nativeKeyUp) {
            // MAP KEYS (Based on analysis)
            // START/SELECT -> KEYCODE_BACK (4) / KEYCODE_MENU (82)
            if ((current_pad & SCE_CTRL_START) && !(oldpad & SCE_CTRL_START)) nativeKeyDown(jniEnv, NULL, 4);
            if (!(current_pad & SCE_CTRL_START) && (oldpad & SCE_CTRL_START)) nativeKeyUp(jniEnv, NULL, 4);

            if ((current_pad & SCE_CTRL_SELECT) && !(oldpad & SCE_CTRL_SELECT)) nativeKeyDown(jniEnv, NULL, 82);
            if (!(current_pad & SCE_CTRL_SELECT) && (oldpad & SCE_CTRL_SELECT)) nativeKeyUp(jniEnv, NULL, 82);

            // DPAD UP and DOWN keep their keycodes to allow proper menu scrolling.
            if ((current_pad & SCE_CTRL_UP) && !(oldpad & SCE_CTRL_UP)) nativeKeyDown(jniEnv, NULL, 19);
            if (!(current_pad & SCE_CTRL_UP) && (oldpad & SCE_CTRL_UP)) nativeKeyUp(jniEnv, NULL, 19);

            //! @see docs/comments/main.c.md#native-xperia-play-control-path
            // Left/Right: keycodes 21/22 reach ControlsLayer::keyLeft/keyRightClicked, which
            // promote walk (event 1) to run (event 2) the *second* time they are called while
            // the first event is still set. The on-screen arrows get that second call from a
            // CCSequence(CCDelayTime(0.25f), CCCallFunc(setPrinceRun)) started on first touch,
            // so the same 250 ms threshold is replayed here: tap for one careful step, hold to
            // run. keyRemoveLeft/Right on release clear both events and the run flag.
            const uint64_t nowUs = sceKernelGetProcessTimeWide();
            for (int d = 0; d < 2; d++) {
                const uint32_t mask = d == 0 ? SCE_CTRL_LEFT : SCE_CTRL_RIGHT;
                const int keycode = d == 0 ? 21 : 22;
                if ((current_pad & mask) && !(oldpad & mask)) {
                    nativeKeyDown(jniEnv, NULL, keycode); // first call -> AddEvent(WALK)
                    dirHeldSince[d] = nowUs;
                    dirRunSent[d] = 0;
                } else if ((current_pad & mask) && !dirRunSent[d]
                           && nowUs - dirHeldSince[d] >= RUN_PROMOTE_US) {
                    nativeKeyDown(jniEnv, NULL, keycode); // second call -> AddEvent(RUN)
                    dirRunSent[d] = 1;
                } else if (!(current_pad & mask) && (oldpad & mask)) {
                    nativeKeyUp(jniEnv, NULL, keycode);
                    dirRunSent[d] = 0;
                }
            }

            //! @see docs/comments/main.c.md#crouch--shared-keycode-for-down-and-circle
            int wantCrouch = (current_pad & (SCE_CTRL_DOWN | SCE_CTRL_CIRCLE)) != 0;
            int wantedCrouch = (oldpad & (SCE_CTRL_DOWN | SCE_CTRL_CIRCLE)) != 0;
            if (wantCrouch && !wantedCrouch) nativeKeyDown(jniEnv, NULL, 20);
            if (!wantCrouch && wantedCrouch) nativeKeyUp(jniEnv, NULL, 20);

            // ACTIONS - Keyboard simulated actions
            // Face buttons
            //! @see docs/comments/main.c.md#cross--keycode-23-already-is-the-jump-event
            if ((current_pad & SCE_CTRL_CROSS) && !(oldpad & SCE_CTRL_CROSS)) {
                nativeKeyDown(jniEnv, NULL, 23); // DPAD_CENTER -> ControlsLayer::keyXClicked (jump / attack / menu confirm)
            }
            if (!(current_pad & SCE_CTRL_CROSS) && (oldpad & SCE_CTRL_CROSS)) {
                nativeKeyUp(jniEnv, NULL, 23);   // -> ControlsLayer::keyRemoveX
            }
            
            // Square -> keySqrClicked: crouch in platform mode, defend in combat.
            if ((current_pad & SCE_CTRL_SQUARE) && !(oldpad & SCE_CTRL_SQUARE)) nativeKeyDown(jniEnv, NULL, 99); // BUTTON_C
            if (!(current_pad & SCE_CTRL_SQUARE) && (oldpad & SCE_CTRL_SQUARE)) nativeKeyUp(jniEnv, NULL, 99);

            // Triangle -> keyTriClicked: interact in platform mode, sheath in combat.
            if ((current_pad & SCE_CTRL_TRIANGLE) && !(oldpad & SCE_CTRL_TRIANGLE)) nativeKeyDown(jniEnv, NULL, 100); // BUTTON_Z
            if (!(current_pad & SCE_CTRL_TRIANGLE) && (oldpad & SCE_CTRL_TRIANGLE)) nativeKeyUp(jniEnv, NULL, 100);

            // Keycodes 102/103 are deliberately not sent: keyLeftKeyClicked has an empty body
            // and keyRightKeyClicked only duplicates Triangle's interact flag. The physical
            // shoulders are SCE_CTRL_LTRIGGER/RTRIGGER on a handheld Vita anyway (SCE_CTRL_L1/R1
            // are PSTV/DualShock only), and they are used for the toggle below.

            //! @see docs/comments/main.c.md#controls-visibility-toggle--l1r1-combo
            uint32_t comboMask = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER;
            int comboHeldNow = (current_pad & comboMask) == comboMask;
            int comboHeldBefore = (oldpad & comboMask) == comboMask;
            if (comboHeldNow && !comboHeldBefore && SetControlVisible && SetControlInVisible) {
                //! @see docs/comments/main.c.md#native-xperia-play-control-path
                // Both stubs just write CCDirector+0xac; ControlsLayer::tick() compares that
                // byte against its cached m_ControlsXVisible next frame and applies the change
                // through the game's own show/hide code. No member offsets, no opacity trick.
                controlsVisible = !controlsVisible;
                if (controlsVisible) SetControlVisible(jniEnv, NULL);
                else                 SetControlInVisible(jniEnv, NULL);
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
