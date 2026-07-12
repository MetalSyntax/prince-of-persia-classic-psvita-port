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

    // Each loaded .so that exports JNI_OnLoad caches the JavaVM* it's given in
    // its own internal global for later use (e.g. SimpleAudioEngine in
    // libcocosdenshion.so attaches a background thread and calls
    // (*jvm)->GetEnv(...) on it) -- so it must be called on every module that
    // exports it, not just whichever module happens to export it first.
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

    // Initialize Cocos2d-x environment
    if (nativeSetPaths) {
        // Confirmed by testing (two different native codepaths, matching real
        // Android semantics):
        // - apkFilePath (arg 1) is treated as the *folder* that would hold the
        //   real Android /Android/obb/<package>/ directory: the engine appends
        //   the known obb filename to it directly (e.g. reads
        //   <apkFilePath>/main.1.org.ubisoft.premium.POPClassic.obb as a zip)
        //   to pull Data*/ files such as Localization/*.loc.
        // - apkSourceDir (arg 3) is opened natively (via zlib) *directly* as a
        //   zip/apk file, to read assets/appConfig.txt -- it must point straight
        //   at the .apk, not at a folder.
        // Data/* loose assets under DATA_PATH are unaffected either way, since
        // those are read via plain fopen(), not through either of these.

        if (!file_exists(DATA_PATH "original.apk")) {
            fatal_error("Error: " DATA_PATH "original.apk not found.\n"
                        "Copy the original game's .apk there (renamed to "
                        "\"original.apk\") -- see INSTALL_HARDWARE.md step 2.2. "
                        "Without it, the game reads assets/appConfig.txt from a "
                        "NULL handle and crashes with a Data abort instead of "
                        "this message.");
        }
        jstring apkFilePathStr = (*jniEnv)->NewStringUTF(jniEnv, DATA_PATH);
        jstring apkSourceDirStr = (*jniEnv)->NewStringUTF(jniEnv, DATA_PATH "original.apk");
        jstring deviceStr = (*jniEnv)->NewStringUTF(jniEnv, "PSVita");
        nativeSetPaths(jniEnv, NULL, apkFilePathStr, apkSourceDirStr, deviceStr);
        l_success("nativeSetPaths(%s, %soriginal.apk, PSVita) done.", DATA_PATH, DATA_PATH);
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
    
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    if (nativeInit) {
        nativeInit(jniEnv, NULL, 960, 544);
        l_success("nativeInit(960, 544) done -- game should now be constructing its first scene.");
    }

    l_success("Entering main loop.");
    int lastX[5] = {-1, -1, -1, -1, -1};
    int lastY[5] = {-1, -1, -1, -1, -1};
    // Vita hardware touch id currently occupying each slot, -1 = free. This is
    // NOT the id we hand to the engine -- SceTouchReport::id is an 8-bit
    // counter that keeps growing for the whole session (not a small 0-4 range
    // like Android's pointer ids), and nativeTouchesBegin/Move/End index a
    // fixed-size array with it internally. Passing the raw hardware id writes
    // out of bounds and corrupts the heap (confirmed via vita-parse-core on
    // two real crash dumps -- both showed the crash inside/downstream of
    // nativeTouchesEnd). The slot index (0-4) is what actually gets sent.
    int slotHwId[5] = {-1, -1, -1, -1, -1};
    uint32_t oldpad = 0;
    int frame = 0;
    int last_logged_report_num = -1;
    uint32_t last_logged_pad_buttons = 0;


    while (1) {
        SceTouchData touch;
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

        SceCtrlData debug_pad;
        sceCtrlPeekBufferPositive(0, &debug_pad, 1);
        // Edge-triggered: log only when the touch/pad state actually CHANGES
        // from the previous frame, plus a periodic heartbeat while idle.
        // The old level-triggered condition (fired every single frame with
        // any active touch) meant a sustained drag logged 60 identical
        // lines/sec -- the logger's consecutive-duplicate suppression
        // (Fixes_Log.md #13) collapsed exact repeats, but any jitter in
        // reportNum/buttons between frames defeated that, since those are
        // distinct messages each time, not repeats.
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

        // Build one combined list of "virtual fingers" for this frame: the
        // real touches plus (if held) synthetic ones for the D-Pad-driven
        // joystick drag and the combat/action buttons below. They all
        // compete for the SAME 5 engine touch slots (0-4), never a 6th one:
        // cocos2d-x's Android touch dispatch is sized for CC_MAX_TOUCHES ==
        // 5, so id 5 is already one past the end of its internal array --
        // confirmed the hard way, that exact off-by-one corrupted the heap
        // on real hardware.
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

        int dpadWantLeft = (current_pad & SCE_CTRL_LEFT) != 0;
        int dpadWantRight = (current_pad & SCE_CTRL_RIGHT) != 0;

        int wantWalk = (current_pad & SCE_CTRL_SQUARE) != 0;

        const int ARROW_Y = 400;
        const int WALK_X_LEFT = 95, RUN_X_LEFT = 30;
        const int WALK_X_RIGHT = 155, RUN_X_RIGHT = 220;
        
        // Square is walk toggle button
        if (wantWalk && reportCount < 5) {
            reportHwId[reportCount] = -7;
            reportX[reportCount] = 780; // Virtual Walk button
            reportY[reportCount] = 450;
            reportCount++;
        }

        if (dpadWantLeft && reportCount < 5) {
            reportHwId[reportCount] = -2; // Virtual ID for Left Arrow
            reportX[reportCount] = wantWalk ? 95 : 30;
            reportY[reportCount] = ARROW_Y;
            reportCount++;
        }

        if (dpadWantRight && reportCount < 5) {
            reportHwId[reportCount] = -3; // Virtual ID for Right Arrow
            reportX[reportCount] = wantWalk ? 155 : 220;
            reportY[reportCount] = ARROW_Y;
            reportCount++;
        }
        
        // DPAD DOWN is handled via nativeKeyDown to preserve menu scrolling

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
            // Left and Right must remain exclusively touch-based to prevent forced walking.
            if ((current_pad & SCE_CTRL_UP) && !(oldpad & SCE_CTRL_UP)) nativeKeyDown(jniEnv, NULL, 19);
            if (!(current_pad & SCE_CTRL_UP) && (oldpad & SCE_CTRL_UP)) nativeKeyUp(jniEnv, NULL, 19);
            
            if ((current_pad & SCE_CTRL_DOWN) && !(oldpad & SCE_CTRL_DOWN)) nativeKeyDown(jniEnv, NULL, 20);
            if (!(current_pad & SCE_CTRL_DOWN) && (oldpad & SCE_CTRL_DOWN)) nativeKeyUp(jniEnv, NULL, 20);

            // ACTIONS - Keyboard simulated actions
            // Face buttons
            if ((current_pad & SCE_CTRL_CROSS) && !(oldpad & SCE_CTRL_CROSS)) {
                nativeKeyDown(jniEnv, NULL, 23); // DPAD CENTER (Menu Select / Attack)
                nativeKeyDown(jniEnv, NULL, 96); // BUTTON_A (Jump in Android TV)
            }
            if (!(current_pad & SCE_CTRL_CROSS) && (oldpad & SCE_CTRL_CROSS)) {
                nativeKeyUp(jniEnv, NULL, 23);
                nativeKeyUp(jniEnv, NULL, 96);
            }

            if ((current_pad & SCE_CTRL_CIRCLE) && !(oldpad & SCE_CTRL_CIRCLE)) {
                nativeKeyDown(jniEnv, NULL, 4);  // BACK (Menu Back)
                nativeKeyDown(jniEnv, NULL, 97); // BUTTON_B (Action/Drop in Android TV)
            }
            if (!(current_pad & SCE_CTRL_CIRCLE) && (oldpad & SCE_CTRL_CIRCLE)) {
                nativeKeyUp(jniEnv, NULL, 4);
                nativeKeyUp(jniEnv, NULL, 97);
            }
            
            if ((current_pad & SCE_CTRL_TRIANGLE) && !(oldpad & SCE_CTRL_TRIANGLE)) nativeKeyDown(jniEnv, NULL, 100); // BUTTON_Y
            if (!(current_pad & SCE_CTRL_TRIANGLE) && (oldpad & SCE_CTRL_TRIANGLE)) nativeKeyUp(jniEnv, NULL, 100);
            
            if ((current_pad & SCE_CTRL_L1) && !(oldpad & SCE_CTRL_L1)) nativeKeyDown(jniEnv, NULL, 102); // L1
            if (!(current_pad & SCE_CTRL_L1) && (oldpad & SCE_CTRL_L1)) nativeKeyUp(jniEnv, NULL, 102);
            
            if ((current_pad & SCE_CTRL_R1) && !(oldpad & SCE_CTRL_R1)) nativeKeyDown(jniEnv, NULL, 103); // R1
            if (!(current_pad & SCE_CTRL_R1) && (oldpad & SCE_CTRL_R1)) nativeKeyUp(jniEnv, NULL, 103);
        }
        oldpad = current_pad;

        if (nativeRender) {
            nativeRender(jniEnv, NULL);
        }
        
        gl_swap();
    }

    audio_shutdown();
    video_shutdown();
    sceKernelExitDeleteThread(0);
}
