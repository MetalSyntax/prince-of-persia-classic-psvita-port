#include "utils/logger.h"
#include <falso_jni/FalsoJNI_Impl.h>
#include <falso_jni/FalsoJNI.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <so_util/so_util.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

#include "audio.h"
#include "video.h"

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

    //! @see docs/comments/java.c.md#cocos2dxactivity--animation-interval-and-online-integrations
    { 60, "setAnimationInterval", METHOD_TYPE_VOID },
    { 61, "startFlurry", METHOD_TYPE_VOID },
    { 62, "initializePapayaFramework", METHOD_TYPE_VOID },

    //! @see docs/comments/java.c.md#cocos2dxhelper--rewards-coins
    { 63, "getRewardsCoins", METHOD_TYPE_INT },

    //! @see docs/comments/java.c.md#introtextlayer--playvideo-no-op
    { 64, "playVideo", METHOD_TYPE_VOID },
};

jobject Cocos2dxActivity_getDeviceName(jmethodID id, va_list args) {
    l_debug("Cocos2dxActivity_getDeviceName()");
    JNIEnv *jniEnv = &jni;
    return (*jniEnv)->NewStringUTF(jniEnv, "PSVita");
}

jobject Cocos2dxActivity_getCurrentLanguage(jmethodID id, va_list args) {
    l_debug("Cocos2dxActivity_getCurrentLanguage()");
    JNIEnv *jniEnv = &jni;
    return (*jniEnv)->NewStringUTF(jniEnv, "en");
}

void Cocos2dxHelper_showMessageBox(jmethodID id, va_list args) {
    JNIEnv *jniEnv = &jni;
    jstring j_title = va_arg(args, jstring);
    jstring j_message = va_arg(args, jstring);
    const char *title = j_title ? (*jniEnv)->GetStringUTFChars(jniEnv, j_title, NULL) : "";
    const char *message = j_message ? (*jniEnv)->GetStringUTFChars(jniEnv, j_message, NULL) : "";
    l_debug("Cocos2dxHelper_showMessageBox(%s, %s)\n", title, message);
}

//! @see docs/comments/java.c.md#createtextbitmap--stb_truetype-rationale
#define MAX_FONTS 4
typedef struct {
    char name[64]; // basename this slot was loaded for, e.g. "UbiGameTextLReg.ttf"
    stbtt_fontinfo info;
    int state; // 0=empty, 1=ready, -1=failed
} LoadedFont;
static LoadedFont g_fonts[MAX_FONTS];

static unsigned char *read_font_file_sceio(const char *path, int *out_size) {
    *out_size = 0;
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return NULL;
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (size <= 0 || size > 16 * 1024 * 1024) {
        sceIoClose(fd);
        return NULL;
    }
    unsigned char *buf = malloc((size_t) size);
    if (!buf) {
        sceIoClose(fd);
        return NULL;
    }
    int off = 0;
    while (off < (int) size) {
        int n = sceIoRead(fd, buf + off, (SceSize) ((int) size - off));
        if (n <= 0) {
            free(buf);
            sceIoClose(fd);
            return NULL;
        }
        off += n;
    }
    sceIoClose(fd);
    *out_size = (int) size;
    return buf;
}

static const char *font_basename(const char *fontName) {
    if (!fontName) return "";
    const char *slash = strrchr(fontName, '/');
    return slash ? slash + 1 : fontName;
}

/** @brief Loads (or reuses a cached) stbtt_fontinfo for the font the game requested. */
static stbtt_fontinfo *get_font(const char *fontName) {
    const char *base = font_basename(fontName);
    if (!*base) base = "DejaVuSans.ttf";

    for (int i = 0; i < MAX_FONTS; i++) {
        if (g_fonts[i].state != 0 && strcmp(g_fonts[i].name, base) == 0)
            return g_fonts[i].state > 0 ? &g_fonts[i].info : NULL;
    }

    int slot = -1;
    for (int i = 0; i < MAX_FONTS; i++) {
        if (g_fonts[i].state == 0) { slot = i; break; }
    }
    if (slot < 0) slot = MAX_FONTS - 1; // reuse the last slot rather than fail silently

    snprintf(g_fonts[slot].name, sizeof(g_fonts[slot].name), "%s", base);
    g_fonts[slot].state = -1;

    char path[160];
    snprintf(path, sizeof(path), DATA_PATH "Data/font/%s", base);
    int size = 0;
    unsigned char *data = read_font_file_sceio(path, &size);

    if (!data && strcmp(base, "DejaVuSans.ttf") != 0) {
        l_warn("createTextBitmap: font \"%s\" not found on card, falling back to DejaVuSans.ttf", base);
        snprintf(g_fonts[slot].name, sizeof(g_fonts[slot].name), "DejaVuSans.ttf");
        FILE *f = fopen("app0:/DejaVuSans.ttf", "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            data = malloc((size_t) fsize);
            if (data && fread(data, 1, (size_t) fsize, f) == (size_t) fsize) {
                size = (int) fsize;
            } else {
                free(data);
                data = NULL;
            }
            fclose(f);
        }
    }

    if (!data || !stbtt_InitFont(&g_fonts[slot].info, data, 0)) {
        free(data);
        return NULL;
    }
    //! data is intentionally never freed — see docs/comments/java.c.md#font-data-lifetime
    g_fonts[slot].state = 1;
    return &g_fonts[slot].info;
}

static int utf8_decode(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    int c = *s++;
    if (c < 0x80) { *p = (const char *)s; return c; }
    if ((c & 0xE0) == 0xC0) {
        c = ((c & 0x1F) << 6) | (*s++ & 0x3F);
    } else if ((c & 0xF0) == 0xE0) {
        c = ((c & 0x0F) << 12) | ((*s & 0x3F) << 6); s++;
        c |= *s++ & 0x3F;
    } else if ((c & 0xF8) == 0xF0) {
        c = ((c & 0x07) << 18) | ((*s & 0x3F) << 12); s++;
        c |= ((*s & 0x3F) << 6); s++;
        c |= *s++ & 0x3F;
    }
    *p = (const char *)s;
    return c;
}

void Cocos2dxBitmap_createTextBitmap(jmethodID id, va_list args) {
    jstring j_text = va_arg(args, jstring);
    jstring j_fontName = va_arg(args, jstring);
    jint fontSize = va_arg(args, jint);
    jint alignment = va_arg(args, jint);
    jint width = va_arg(args, jint);
    jint height = va_arg(args, jint);

    const char *fontName = (const char *) j_fontName;
    l_debug("Cocos2dxBitmap_createTextBitmap(\"%s\", font=%s, size=%i, align=%i, %ix%i)",
            j_text ? (const char *) j_text : "(null)", fontName ? fontName : "(null)",
            (int)fontSize, (int)alignment, (int)width, (int)height);

    JNIEnv *jniEnv = &jni;
    stbtt_fontinfo *font = get_font(fontName);

    if (!j_text || !font) {
        void (* nativeInitBitmapDC)(JNIEnv *, jobject, jint, jint, jbyteArray) =
            (void *) so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxBitmap_nativeInitBitmapDC");
        if (nativeInitBitmapDC) nativeInitBitmapDC(jniEnv, NULL, 0, 0, NULL);
        return;
    }

    const char *text = (*jniEnv)->GetStringUTFChars(jniEnv, j_text, NULL);
    float scale = stbtt_ScaleForPixelHeight(font, (float) fontSize);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(font, &ascent, &descent, &lineGap);
    int line_height = (int)((ascent - descent + lineGap) * scale);
    if (line_height <= 0) line_height = 1;

    int max_lines = 128;
    const char **line_starts = calloc(max_lines, sizeof(char*));
    int *line_widths = calloc(max_lines, sizeof(int));
    int num_lines = 0;
    
    int max_w = 0;
    const char *p = text;
    while (*p && num_lines < max_lines) {
        line_starts[num_lines] = p;
        int cur_w = 0;
        const char *last_space = NULL;
        int w_at_last_space = 0;
        
        while (*p && *p != '\n') {
            const char *prev_p = p;
            int cp = utf8_decode(&p);
            
            int advance, lsb;
            stbtt_GetCodepointHMetrics(font, cp, &advance, &lsb);
            int char_w = (int)(advance * scale);
            
            if (width > 0 && cur_w + char_w > width) {
                if (last_space) {
                    p = last_space + 1;
                    cur_w = w_at_last_space;
                } else {
                    p = prev_p;
                }
                break;
            }
            if (cp == ' ') {
                last_space = prev_p;
                w_at_last_space = cur_w;
            }
            cur_w += char_w;
        }
        line_widths[num_lines] = cur_w;
        if (cur_w > max_w) max_w = cur_w;
        num_lines++;
        if (*p == '\n') p++; 
    }

    if (width <= 0) width = max_w;
    if (height <= 0) height = num_lines * line_height;

    size_t size = (size_t) width * (size_t) height * 4;
    jbyte *buf = calloc(1, size);

    if (buf) {
        int h_align = alignment & 0x0F;
        int v_align = (alignment >> 4) & 0x0F;
        int total_text_h = num_lines * line_height;
        int start_y = 0;
        if (v_align == 3) start_y = (height - total_text_h) / 2;
        else if (v_align == 2) start_y = height - total_text_h;
        
        for (int i = 0; i < num_lines; i++) {
            int pen_x = 0;
            if (h_align == 3) pen_x = (width - line_widths[i]) / 2;
            else if (h_align == 2) pen_x = width - line_widths[i];
            
            int baseline = start_y + i * line_height + (int)(ascent * scale);
            
            const char *cp_ptr = line_starts[i];
            const char *line_end = (i+1 < num_lines) ? line_starts[i+1] : text + strlen(text);
            
            while (cp_ptr < line_end && *cp_ptr && *cp_ptr != '\n') {
                int cp = utf8_decode(&cp_ptr);
                
                int advance, lsb;
                stbtt_GetCodepointHMetrics(font, cp, &advance, &lsb);
                
                int x0, y0, x1, y1;
                stbtt_GetCodepointBitmapBox(font, cp, scale, scale, &x0, &y0, &x1, &y1);
                int glyph_w = x1 - x0, glyph_h = y1 - y0;
                
                if (glyph_w > 0 && glyph_h > 0) {
                    unsigned char *glyph = calloc(1, (size_t) glyph_w * glyph_h);
                    if (glyph) {
                        stbtt_MakeCodepointBitmap(font, glyph, glyph_w, glyph_h, glyph_w, scale, scale, cp);
                        
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
                                px[0] = (jbyte) a; 
                                px[1] = (jbyte) a;
                                px[2] = (jbyte) a;
                                px[3] = (jbyte) a;
                            }
                        }
                        free(glyph);
                    }
                }
                pen_x += (int)(advance * scale);
            }
        }
    }
    
    free(line_starts);
    free(line_widths);
    if (text) (*jniEnv)->ReleaseStringUTFChars(jniEnv, j_text, (char *) text);

    jbyteArray pixels = NULL;
    if (size > 0) {
        pixels = (*jniEnv)->NewByteArray(jniEnv, (jsize) size);
        if (pixels && buf) {
            (*jniEnv)->SetByteArrayRegion(jniEnv, pixels, 0, (jsize) size, buf);
        }
    }
    free(buf);

    void (* nativeInitBitmapDC)(JNIEnv *env, jobject thiz, jint width, jint height, jbyteArray pixels) =
        (void *) so_symbol(&cocos2d_mod, "Java_org_cocos2dx_lib_Cocos2dxBitmap_nativeInitBitmapDC");
    if (nativeInitBitmapDC) {
        nativeInitBitmapDC(jniEnv, NULL, width, height, pixels);
    }
}


void Cocos2dxActivity_setAnimationInterval(jmethodID id, va_list args) {
    jdouble interval = va_arg(args, jdouble); // unused: our loop drives its own timing
    l_debug("Cocos2dxActivity_setAnimationInterval(%f) (ignored)", (double) interval);
}

/** @brief No-op analytics stub. @note See docs/comments/java.c.md#startflurry--no-op */
void Cocos2dxActivity_startFlurry(jmethodID id, va_list args) {
    l_debug("Cocos2dxActivity_startFlurry() (no-op: ENABLE_FLURRY=NO)");
}

/** @brief No-op ad/social framework stub. @note See docs/comments/java.c.md#initializepapayaframework--no-op */
void Cocos2dxActivity_initializePapayaFramework(jmethodID id, va_list args) {
    l_debug("Cocos2dxActivity_initializePapayaFramework() (no-op: ENABLE_PAPAYA=NO)");
}

jint Cocos2dxHelper_getRewardsCoins(jmethodID id, va_list args) {
    l_debug("Cocos2dxHelper_getRewardsCoins() -> 0");
    return 0;
}

/** @brief Plays an FMV cutscene via video_play(), then fires the completion callback.
 *  @note See docs/comments/java.c.md#ccvideoutilsplayvideo--argument-handling
 *  @note See docs/comments/java.c.md#video-completion-callback */
void Cocos2dxActivity_playVideo(jmethodID id, va_list args) {
    //! @see docs/comments/java.c.md#ccvideoutilsplayvideo--argument-handling
    jstring j_path = va_arg(args, jstring);
    const char *path = (uintptr_t) j_path > 0x1000 ? (const char *) j_path : NULL;
    l_debug("Cocos2dxActivity_playVideo(\"%s\")", path ? path : "(unreadable arg, skipping)");

    if (path) {
        video_play(path);
    }

    //! @see docs/comments/java.c.md#video-completion-callback
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
    //! @see docs/comments/java.c.md#preloadeffect-double-registration
    { 27, (void (*)(jmethodID, va_list)) Cocos2dxSound_preloadEffect },
    { 28, Cocos2dxSound_unloadEffect },
    { 30, Cocos2dxSound_setEffectsVolume },
};

/*
 * JNI Fields
*/

//! @see docs/comments/java.c.md#window_service-and-sdk_int-fields
// https://developer.android.com/reference/android/content/Context.html#WINDOW_SERVICE
char WINDOW_SERVICE[] = "window";

//! @see docs/comments/java.c.md#window_service-and-sdk_int-fields
// https://developer.android.com/reference/android/os/Build.VERSION.html#SDK_INT
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
