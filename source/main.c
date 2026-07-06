#include <vitasdk.h>
#include <vitaGL.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include "utils/init.h"
#include "utils/glutil.h"
#include "utils/utils.h"
#include "utils/dialog.h"
#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include "audio.h"

int _newlib_heap_size_user = 256 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif

extern so_module denshion_mod;
extern so_module cocos2d_mod;
extern so_module game_mod;

int main() {
    soloader_init_all();
    audio_init();

    JNIEnv *jniEnv = &jni;

    // Each loaded .so that exports JNI_OnLoad caches the JavaVM* it's given in
    // its own internal global for later use (e.g. SimpleAudioEngine in
    // libcocosdenshion.so attaches a background thread and calls
    // (*jvm)->GetEnv(...) on it) -- so it must be called on every module that
    // exports it, not just whichever module happens to export it first.
    so_module *jni_onload_mods[] = { &denshion_mod, &cocos2d_mod, &game_mod };
    for (int i = 0; i < 3; i++) {
        int (* JNI_OnLoad)(void *jvm, void *reserved) = (void *)so_symbol(jni_onload_mods[i], "JNI_OnLoad");
        if (JNI_OnLoad) {
            JNI_OnLoad(&jvm, NULL);
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

    if (nativeInit) {
        nativeInit(jniEnv, NULL, 960, 544);
    }

    int lastX[5] = {-1, -1, -1, -1, -1};
    int lastY[5] = {-1, -1, -1, -1, -1};
    uint32_t oldpad = 0;
    int frame = 0;

    while (1) {
        SceTouchData touch;
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

        SceCtrlData debug_pad;
        sceCtrlPeekBufferPositive(0, &debug_pad, 1);
        if ((frame++ % 120) == 0 || touch.reportNum > 0 || debug_pad.buttons != 0) {
            sceClibPrintf("input tick: touch.reportNum=%i pad.buttons=0x%08x\n",
                           touch.reportNum, (unsigned int) debug_pad.buttons);
        }

        for (int i = 0; i < 5; i++) {
            if (i < touch.reportNum) {
                int x = (int)((float)touch.report[i].x * 960.0f / 1920.0f);
                int y = (int)((float)touch.report[i].y * 544.0f / 1088.0f);

                if (lastX[i] == -1 || lastY[i] == -1) {
                    if (nativeTouchesBegin) nativeTouchesBegin(jniEnv, NULL, touch.report[i].id, (jfloat)x, (jfloat)y);
                } else {
                    if (lastX[i] != x || lastY[i] != y)
                        if (nativeTouchesMove) nativeTouchesMove(jniEnv, NULL, touch.report[i].id, (jfloat)x, (jfloat)y);
                }
                lastX[i] = x;
                lastY[i] = y;
            } else {
                if (lastX[i] != -1 || lastY[i] != -1) {
                    if (nativeTouchesEnd) nativeTouchesEnd(jniEnv, NULL, i, (jfloat)lastX[i], (jfloat)lastY[i]);
                    lastX[i] = -1;
                    lastY[i] = -1;
                }
            }
        }

        SceCtrlData pad;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        uint32_t current_pad = pad.buttons;

        if (nativeKeyDown && nativeKeyUp) {
            // MAP KEYS (Based on analysis)
            // START/SELECT -> KEYCODE_BACK (4) / KEYCODE_MENU (82)
            if ((current_pad & SCE_CTRL_START) && !(oldpad & SCE_CTRL_START)) nativeKeyDown(jniEnv, NULL, 4);
            if (!(current_pad & SCE_CTRL_START) && (oldpad & SCE_CTRL_START)) nativeKeyUp(jniEnv, NULL, 4);

            if ((current_pad & SCE_CTRL_SELECT) && !(oldpad & SCE_CTRL_SELECT)) nativeKeyDown(jniEnv, NULL, 82);
            if (!(current_pad & SCE_CTRL_SELECT) && (oldpad & SCE_CTRL_SELECT)) nativeKeyUp(jniEnv, NULL, 82);

            // DPAD
            if ((current_pad & SCE_CTRL_UP) && !(oldpad & SCE_CTRL_UP)) nativeKeyDown(jniEnv, NULL, 19);
            if (!(current_pad & SCE_CTRL_UP) && (oldpad & SCE_CTRL_UP)) nativeKeyUp(jniEnv, NULL, 19);
            
            if ((current_pad & SCE_CTRL_DOWN) && !(oldpad & SCE_CTRL_DOWN)) nativeKeyDown(jniEnv, NULL, 20);
            if (!(current_pad & SCE_CTRL_DOWN) && (oldpad & SCE_CTRL_DOWN)) nativeKeyUp(jniEnv, NULL, 20);
            
            if ((current_pad & SCE_CTRL_LEFT) && !(oldpad & SCE_CTRL_LEFT)) nativeKeyDown(jniEnv, NULL, 21);
            if (!(current_pad & SCE_CTRL_LEFT) && (oldpad & SCE_CTRL_LEFT)) nativeKeyUp(jniEnv, NULL, 21);
            
            if ((current_pad & SCE_CTRL_RIGHT) && !(oldpad & SCE_CTRL_RIGHT)) nativeKeyDown(jniEnv, NULL, 22);
            if (!(current_pad & SCE_CTRL_RIGHT) && (oldpad & SCE_CTRL_RIGHT)) nativeKeyUp(jniEnv, NULL, 22);

            // ACTIONS
            if ((current_pad & SCE_CTRL_CROSS) && !(oldpad & SCE_CTRL_CROSS)) nativeKeyDown(jniEnv, NULL, 23); // DPAD CENTER
            if (!(current_pad & SCE_CTRL_CROSS) && (oldpad & SCE_CTRL_CROSS)) nativeKeyUp(jniEnv, NULL, 23);

            if ((current_pad & SCE_CTRL_SQUARE) && !(oldpad & SCE_CTRL_SQUARE)) nativeKeyDown(jniEnv, NULL, 99); // BUTTON_X
            if (!(current_pad & SCE_CTRL_SQUARE) && (oldpad & SCE_CTRL_SQUARE)) nativeKeyUp(jniEnv, NULL, 99);
            
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
    sceKernelExitDeleteThread(0);
}
