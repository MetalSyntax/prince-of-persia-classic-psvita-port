#include <falso_jni/FalsoJNI_Impl.h>
#include <falso_jni/FalsoJNI.h>
#include <psp2/kernel/clib.h>
#include <so_util/so_util.h>
#include <stdio.h>
#include <stdlib.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

#include "audio.h"

extern so_module cocos2d_mod;

/*
 * JNI Methods
*/

NameToMethodID nameToMethodId[] = {
    // Cocos2dxMusic
    { 10, "playBackgroundMusic", METHOD_TYPE_VOID },
    { 11, "stopBackgroundMusic", METHOD_TYPE_VOID },
    { 12, "pauseBackgroundMusic", METHOD_TYPE_VOID },
    { 13, "resumeBackgroundMusic", METHOD_TYPE_VOID },
    { 14, "rewindBackgroundMusic", METHOD_TYPE_VOID },
    { 15, "isBackgroundMusicPlaying", METHOD_TYPE_BOOLEAN },
    { 16, "getBackgroundMusicVolume", METHOD_TYPE_FLOAT },
    { 17, "setBackgroundMusicVolume", METHOD_TYPE_VOID },
    { 18, "preloadBackgroundMusic", METHOD_TYPE_VOID },

    // Cocos2dxSound
    { 20, "playEffect", METHOD_TYPE_INT },
    { 21, "stopEffect", METHOD_TYPE_VOID },
    { 22, "stopAllEffects", METHOD_TYPE_VOID },
    { 23, "pauseEffect", METHOD_TYPE_VOID },
    { 24, "resumeEffect", METHOD_TYPE_VOID },
    { 25, "pauseAllEffects", METHOD_TYPE_VOID },
    { 26, "resumeAllEffects", METHOD_TYPE_VOID },
    { 27, "preloadEffect", METHOD_TYPE_INT },
    { 28, "unloadEffect", METHOD_TYPE_VOID },
    { 29, "getEffectsVolume", METHOD_TYPE_FLOAT },
    { 30, "setEffectsVolume", METHOD_TYPE_VOID },

    // Cocos2dxActivity / Cocos2dxHelper
    { 40, "getDeviceName", METHOD_TYPE_OBJECT },
    { 41, "showMessageBox", METHOD_TYPE_VOID },
    { 42, "getCurrentLanguage", METHOD_TYPE_OBJECT },

    // Cocos2dxBitmap
    { 50, "createTextBitmap", METHOD_TYPE_VOID },

    // Cocos2dxActivity: animation loop timing, and the online integrations
    // appConfig.txt already asks to disable (ENABLE_FLURRY/ENABLE_PAPAYA,
    // see appConfig.txt) -- but nothing in this loader actually calls the
    // native GetConfig() that would make the engine itself honor those
    // flags (real Android calls it from Java's Activity.onCreate, which we
    // don't have), so the engine still tries to reach them regardless. No-op
    // stubs are the correct behavior here either way, since none of them
    // should do anything on a Vita port.
    { 60, "setAnimationInterval", METHOD_TYPE_VOID },
    { 61, "startFlurry", METHOD_TYPE_VOID },
    { 62, "initializePapayaFramework", METHOD_TYPE_VOID },

    // Cocos2dxHelper: cross-promotion/rewards currency, tied to the same
    // disabled online integrations as above -- no rewards system on Vita.
    { 63, "getRewardsCoins", METHOD_TYPE_INT },

    // IntroTextLayer plays an FMV cutscene via this static native call --
    // no video codec/player on this port, so it's a no-op and the game
    // continues straight past it (matches the void return the caller expects).
    { 64, "playVideo", METHOD_TYPE_VOID },
};

jobject Cocos2dxActivity_getDeviceName(jmethodID id, va_list args) {
    JNIEnv *jniEnv = &jni;
    return (*jniEnv)->NewStringUTF(jniEnv, "PSVita");
}

jobject Cocos2dxActivity_getCurrentLanguage(jmethodID id, va_list args) {
    JNIEnv *jniEnv = &jni;
    return (*jniEnv)->NewStringUTF(jniEnv, "en");
}

void Cocos2dxHelper_showMessageBox(jmethodID id, va_list args) {
    JNIEnv *jniEnv = &jni;
    jstring j_title = va_arg(args, jstring);
    jstring j_message = va_arg(args, jstring);
    const char *title = j_title ? (*jniEnv)->GetStringUTFChars(jniEnv, j_title, NULL) : "";
    const char *message = j_message ? (*jniEnv)->GetStringUTFChars(jniEnv, j_message, NULL) : "";
    sceClibPrintf("Cocos2dxHelper_showMessageBox(%s, %s)\n", title, message);
}

// Real Android calls back into native (Cocos2dxBitmap_nativeInitBitmapDC,
// exported by libcocos2d.so) with the pixels of the text actually rasterized
// via android.graphics.Canvas/Paint. ScePvf and ScePgf (the Vita's two
// built-in system font APIs) were tried first, but both are entirely
// UNIMPLEMENTED in Vita3K (confirmed by reading vita3k/modules/ScePvf/ScePgf
// source: every exported function is a stub) -- no combination of arguments
// can produce real glyph data under this emulator, though the same code
// would very likely work on real hardware where the firmware actually
// implements them. This rasterizes with stb_truetype instead (public domain,
// vendored at lib/stb/stb_truetype.h) against DejaVuSans.ttf (Bitstream Vera
// license, bundled into the .vpk itself as app0:/DejaVuSans.ttf via
// CMakeLists.txt's vita_create_vpk FILE list -- see extras/fonts/), which
// doesn't depend on any Sony system library and so works the same under
// Vita3K and on real hardware.
static stbtt_fontinfo g_stb_font;
static int g_stb_font_state = 0; // 0=not loaded, 1=ready, -1=failed

static int stb_font_ready(void) {
    if (g_stb_font_state != 0) return g_stb_font_state > 0;
    g_stb_font_state = -1;

    FILE *f = fopen("app0:/DejaVuSans.ttf", "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long font_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char *font_data = malloc((size_t) font_size);
    if (!font_data) {
        fclose(f);
        return 0;
    }
    fread(font_data, 1, (size_t) font_size, f);
    fclose(f);

    if (!stbtt_InitFont(&g_stb_font, font_data, 0)) {
        free(font_data);
        return 0;
    }
    // font_data is intentionally never freed: stbtt_fontinfo keeps pointers
    // into it for the lifetime of the process, and there's only ever one
    // font loaded here.
    g_stb_font_state = 1;
    return 1;
}

void Cocos2dxBitmap_createTextBitmap(jmethodID id, va_list args) {
    jstring j_text = va_arg(args, jstring);
    va_arg(args, jstring); // fontName, unused: always draws with the bundled DejaVuSans.ttf
    jint fontSize = va_arg(args, jint);
    va_arg(args, jint);    // alignment, unused: text is always drawn left-aligned on one line
    jint width = va_arg(args, jint);
    jint height = va_arg(args, jint);

    JNIEnv *jniEnv = &jni;
    size_t size = (size_t) width * (size_t) height * 4;
    jbyte *buf = calloc(1, size);

    if (buf && j_text && width > 0 && height > 0 && stb_font_ready()) {
        float scale = stbtt_ScaleForPixelHeight(&g_stb_font, (float) fontSize);
        int ascent;
        stbtt_GetFontVMetrics(&g_stb_font, &ascent, NULL, NULL);
        int baseline = (int) (ascent * scale);

        // ASCII-only: each UTF-8 byte is treated as its own codepoint, which
        // only lines up with the real codepoint for the 0-127 range. Fine
        // for now since this game's UI text is English.
        const char *text = (*jniEnv)->GetStringUTFChars(jniEnv, j_text, NULL);
        int pen_x = 0;
        for (const unsigned char *p = (const unsigned char *) text; text && *p && pen_x < width; p++) {
            int advance, lsb;
            stbtt_GetCodepointHMetrics(&g_stb_font, *p, &advance, &lsb);

            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&g_stb_font, *p, scale, scale, &x0, &y0, &x1, &y1);
            int glyph_w = x1 - x0, glyph_h = y1 - y0;

            if (glyph_w > 0 && glyph_h > 0) {
                unsigned char *glyph = calloc(1, (size_t) glyph_w * glyph_h);
                if (glyph) {
                    stbtt_MakeCodepointBitmap(&g_stb_font, glyph, glyph_w, glyph_h, glyph_w, scale, scale, *p);

                    int origin_x = pen_x + x0;
                    int origin_y = baseline + y0;
                    for (int gy = 0; gy < glyph_h; gy++) {
                        int dy = origin_y + gy;
                        if (dy < 0 || dy >= height) continue;
                        for (int gx = 0; gx < glyph_w; gx++) {
                            int dx = origin_x + gx;
                            if (dx < 0 || dx >= width) continue;
                            unsigned char a = glyph[gy * glyph_w + gx];
                            if (!a) continue;
                            jbyte *px = buf + ((size_t) dy * width + dx) * 4;
                            px[0] = (jbyte) 0xFF; // white text
                            px[1] = (jbyte) 0xFF;
                            px[2] = (jbyte) 0xFF;
                            px[3] = (jbyte) a;
                        }
                    }
                    free(glyph);
                }
            }

            pen_x += (int) (advance * scale);
            if (*(p + 1)) {
                pen_x += (int) (stbtt_GetCodepointKernAdvance(&g_stb_font, *p, *(p + 1)) * scale);
            }
        }
        if (text) {
            (*jniEnv)->ReleaseStringUTFChars(jniEnv, j_text, (char *) text);
        }
    }

    jbyteArray pixels = (*jniEnv)->NewByteArray(jniEnv, (jsize) size);
    if (pixels && buf) {
        (*jniEnv)->SetByteArrayRegion(jniEnv, pixels, 0, (jsize) size, buf);
    }
    free(buf);

    void (* nativeInitBitmapDC)(JNIEnv *env, jobject thiz, jint width, jint height, jbyteArray pixels) =
        (void *) so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxBitmap_nativeInitBitmapDC");
    if (nativeInitBitmapDC) {
        nativeInitBitmapDC(jniEnv, NULL, width, height, pixels);
    }

    sceClibPrintf("Cocos2dxBitmap_createTextBitmap(%ix%i)\n", (int) width, (int) height);
}

void Cocos2dxActivity_setAnimationInterval(jmethodID id, va_list args) {
    va_arg(args, jdouble); // interval, unused: our loop drives its own timing
}

void Cocos2dxActivity_startFlurry(jmethodID id, va_list args) {
    // No-op: ENABLE_FLURRY=NO in appConfig.txt, analytics have no place here.
}

void Cocos2dxActivity_initializePapayaFramework(jmethodID id, va_list args) {
    // No-op: ENABLE_PAPAYA=NO in appConfig.txt, no ad framework on Vita.
}

jint Cocos2dxHelper_getRewardsCoins(jmethodID id, va_list args) {
    return 0;
}

void Cocos2dxActivity_playVideo(jmethodID id, va_list args) {
    // No video codec on this port -- immediately fire the completion callback
    // that Android's Java side would normally call back into native once the
    // video finishes, since VideoLayer (libgame_logic.so) blocks waiting for
    // it and would otherwise hang forever instead of just skipping the video.
    JNIEnv *jniEnv = &jni;
    void (* onVideoCompleted)(JNIEnv *env, jobject thiz) =
        (void *) so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxVideo_onVideoCompleted");
    if (onVideoCompleted) {
        onVideoCompleted(jniEnv, NULL);
    }
}

MethodsBoolean methodsBoolean[] = {
    { 15, Cocos2dxMusic_isBackgroundMusicPlaying },
};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {};
MethodsFloat methodsFloat[] = {
    { 16, Cocos2dxMusic_getBackgroundMusicVolume },
    { 29, Cocos2dxSound_getEffectsVolume },
};
MethodsInt methodsInt[] = {
    { 20, Cocos2dxSound_playEffect },
    { 27, Cocos2dxSound_preloadEffect },
    { 63, Cocos2dxHelper_getRewardsCoins },
};
MethodsLong methodsLong[] = {};
MethodsObject methodsObject[] = {
    { 40, Cocos2dxActivity_getDeviceName },
    { 42, Cocos2dxActivity_getCurrentLanguage },
};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {
    { 41, Cocos2dxHelper_showMessageBox },
    { 50, Cocos2dxBitmap_createTextBitmap },
    { 60, Cocos2dxActivity_setAnimationInterval },
    { 61, Cocos2dxActivity_startFlurry },
    { 62, Cocos2dxActivity_initializePapayaFramework },
    { 64, Cocos2dxActivity_playVideo },
    { 10, Cocos2dxMusic_playBackgroundMusic },
    { 11, Cocos2dxMusic_stopBackgroundMusic },
    { 12, Cocos2dxMusic_pauseBackgroundMusic },
    { 13, Cocos2dxMusic_resumeBackgroundMusic },
    { 14, Cocos2dxMusic_rewindBackgroundMusic },
    { 17, Cocos2dxMusic_setBackgroundMusicVolume },
    { 18, Cocos2dxMusic_preloadBackgroundMusic },
    { 21, Cocos2dxSound_stopEffect },
    { 22, Cocos2dxSound_stopAllEffects },
    { 23, Cocos2dxSound_pauseEffect },
    { 24, Cocos2dxSound_resumeEffect },
    { 25, Cocos2dxSound_pauseAllEffects },
    { 26, Cocos2dxSound_resumeAllEffects },
    { 27, Cocos2dxSound_preloadEffect },
    { 28, Cocos2dxSound_unloadEffect },
    { 30, Cocos2dxSound_setEffectsVolume },
};

/*
 * JNI Fields
*/

// System-wide constant that applications sometimes request
// https://developer.android.com/reference/android/content/Context.html#WINDOW_SERVICE
char WINDOW_SERVICE[] = "window";

// System-wide constant that's often used to determine Android version
// https://developer.android.com/reference/android/os/Build.VERSION.html#SDK_INT
// Possible values: https://developer.android.com/reference/android/os/Build.VERSION_CODES
const int SDK_INT = 19; // Android 4.4 / KitKat

NameToFieldID nameToFieldId[] = {
		{ 0, "WINDOW_SERVICE", FIELD_TYPE_OBJECT }, 
		{ 1, "SDK_INT", FIELD_TYPE_INT },
};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};
FieldsInt fieldsInt[] = {
		{ 1, SDK_INT },
};
FieldsObject fieldsObject[] = {
		{ 0, WINDOW_SERVICE },
};
FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES
