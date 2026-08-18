/**
 * @file  audio.cpp
 * @brief Cocos2dxMusic / Cocos2dxSound JNI surface backed by a small
 *        self-contained mixer over sceAudioOut, replacing SoLoud.
 *        @note See docs/comments/audio.cpp.md for design rationale.
 */

#include "audio.h"
#include "audio_path.h"
#include "utils/logger.h"

#define MINIMP3_IMPLEMENTATION
#include <minimp3/minimp3_ex.h>

#include <psp2/audioout.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <map>
#include <new>
#include <string>

#define MIX_RATE     44100
//! @see docs/comments/audio.cpp.md#mix_grain-buffer-size
#define MIX_GRAIN    2048
#define MAX_VOICES   12
#define BGM_WIN      2048 // decoded BGM window, in frames

//! @see docs/comments/audio.cpp.md#soft_clip_threshold-limiter
#define SOFT_CLIP_THRESHOLD 0.92f

//! @see docs/comments/audio.cpp.md#engine-state-section

static pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;
static bool gAudioReady = false;
static volatile int gQuit = 0;
static int gPort = -1;
static SceUID gThread = -1;

struct SfxSample {
    short *pcm;       // interleaved, native rate/channels, malloc'd by minimp3
    unsigned frames;
    int channels;     // 1 or 2
    int rate;
};

struct Voice {
    SfxSample *smp;   // NULL = free slot
    double pos;       // fractional frame position into smp->pcm
    double step;      // pitch * rate/MIX_RATE
    float gl, gr;
    bool loop;
    bool paused;
    jint id;
};

static std::map<std::string, SfxSample *> gSfxCache;
static Voice gVoices[MAX_VOICES];
static float gSfxVolume = 1.0f;
static jint gNextHandle = 1;

//! @see docs/comments/audio.cpp.md#bgm-streaming-state-section
static unsigned char *gBgmMp3Buf = NULL; //! @see docs/comments/audio.cpp.md#bgm-state--gbgmmp3buf-lifetime
static mp3dec_ex_t gBgmMp3;
static bool gBgmMp3Open = false;
static int gBgmChannels = 2; // source channel count (1 or 2) of gBgmMp3
static std::string gBgmPath;
static double gBgmStep = 1.0, gBgmReadPos = 0.0;
static short gBgmWin[BGM_WIN * 2]; // always stereo-interleaved, upmixed from mono if needed
static int gBgmAvail = 0;   // valid frames in gBgmWin
static bool gBgmPlaying = false, gBgmPaused = false, gBgmLoop = false, gBgmEnded = false;
static float gBgmVolume = 1.0f;

//! @see docs/comments/audio.cpp.md#file-loading--sceio-only

static bool audio_file_exists(const std::string &path) {
    SceIoStat st;
    return sceIoGetstat(path.c_str(), &st) >= 0 && !SCE_S_ISDIR(st.st_mode);
}

static std::string resolve_audio_file(const char *raw) {
    std::string path = sanitize_audio_path(raw);
    if (!audio_file_exists(path)) {
        std::string alt = audio_fallback_path(path);
        if (!audio_file_exists(alt)) {
            l_error("Audio file not found: %s (raw request: %s)", path.c_str(), raw ? raw : "(null)");
            return "";
        }
        path = alt;
    }
    return path;
}

//! @see docs/comments/audio.cpp.md#file-loading--sceio-only
static unsigned char *read_entire_file(const std::string &path, int *out_len) {
    *out_len = 0;
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) {
        l_error("sceIoOpen failed for %s (0x%08X)", path.c_str(), (unsigned)fd);
        return NULL;
    }
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (size <= 0 || size > 32 * 1024 * 1024) { // biggest game .mp3 is ~1MB
        l_error("Bad audio file size for %s: %lld", path.c_str(), (long long)size);
        sceIoClose(fd);
        return NULL;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)size);
    if (!buf) {
        sceIoClose(fd);
        return NULL;
    }
    int off = 0;
    while (off < (int)size) {
        int n = sceIoRead(fd, buf + off, (SceSize)((int)size - off));
        if (n <= 0) {
            l_error("sceIoRead failed for %s at %d (0x%08X)", path.c_str(), off, (unsigned)n);
            free(buf);
            sceIoClose(fd);
            return NULL;
        }
        off += n;
    }
    sceIoClose(fd);
    *out_len = (int)size;
    return buf;
}

//! @see docs/comments/audio.cpp.md#mixing--mixer-thread-and-glock

/** @brief Soft-knee limiter: identity below SOFT_CLIP_THRESHOLD, compresses
 *         smoothly toward (but never past) full scale above it.
 *  @note See docs/comments/audio.cpp.md#soft_clip16--identity-below-threshold
 */
static inline short soft_clip16(int v) {
    const float full = 32768.0f;
    float x = (float) v / full;
    float ax = fabsf(x);
    if (ax <= SOFT_CLIP_THRESHOLD) {
        if (v > 32767) return 32767;
        if (v < -32768) return -32768;
        return (short) v;
    }
    float sign = (x < 0.0f) ? -1.0f : 1.0f;
    float over = (ax - SOFT_CLIP_THRESHOLD) / (1.0f - SOFT_CLIP_THRESHOLD);
    float compressed = SOFT_CLIP_THRESHOLD + (1.0f - SOFT_CLIP_THRESHOLD) * tanhf(over);
    float outv = sign * compressed * full;
    if (outv > 32767.0f) outv = 32767.0f;
    if (outv < -32768.0f) outv = -32768.0f;
    return (short) outv;
}

/** @brief Refills gBgmWin from gBgmMp3, upmixing mono source to stereo.
 *  @note See docs/comments/audio.cpp.md#bgm_refill_from_decoder--mono-upmix
 */
static int bgm_refill_from_decoder(int frames_wanted) {
    int space = BGM_WIN - gBgmAvail;
    if (frames_wanted > space) frames_wanted = space;
    if (frames_wanted <= 0) return 0;

    if (gBgmChannels == 2) {
        size_t got = mp3dec_ex_read(&gBgmMp3, gBgmWin + gBgmAvail * 2, (size_t) frames_wanted * 2);
        int gotFrames = (int)(got / 2);
        gBgmAvail += gotFrames;
        return gotFrames;
    }

    static short mono[BGM_WIN]; // scratch, frames_wanted <= BGM_WIN
    size_t got = mp3dec_ex_read(&gBgmMp3, mono, (size_t) frames_wanted);
    for (size_t i = 0; i < got; i++) {
        gBgmWin[(gBgmAvail + (int) i) * 2 + 0] = mono[i];
        gBgmWin[(gBgmAvail + (int) i) * 2 + 1] = mono[i];
    }
    gBgmAvail += (int) got;
    return (int) got;
}

/** @brief Refills gBgmWin so at least 2 frames are readable from gBgmReadPos.
 *  @note See docs/comments/audio.cpp.md#bgm_ensure_window--stream-refill
 */
static bool bgm_ensure_window(void) {
    for (;;) {
        int idx = (int)gBgmReadPos;
        if (idx + 1 < gBgmAvail)
            return true;

        // keep the last frame for interpolation continuity
        int keep = (gBgmAvail > 0) ? 1 : 0;
        if (keep && idx < gBgmAvail) {
            gBgmWin[0] = gBgmWin[(gBgmAvail - 1) * 2];
            gBgmWin[1] = gBgmWin[(gBgmAvail - 1) * 2 + 1];
        }
        gBgmReadPos -= (gBgmAvail > keep) ? (double)(gBgmAvail - keep) : 0.0;
        if (gBgmReadPos < 0.0) gBgmReadPos = 0.0;
        gBgmAvail = keep;

        int got = bgm_refill_from_decoder(BGM_WIN - gBgmAvail);
        if (got > 0) {
            continue;
        }
        if (gBgmLoop) {
            mp3dec_ex_seek(&gBgmMp3, 0);
            continue;
        }
        return false; // stream exhausted
    }
}

static void mix_bgm(int *acc, int frames) {
    if (!gBgmMp3Open || !gBgmPlaying || gBgmPaused || gBgmEnded)
        return;
    for (int i = 0; i < frames; i++) {
        if (!bgm_ensure_window()) {
            gBgmEnded = true;
            return;
        }
        int idx = (int)gBgmReadPos;
        float frac = (float)(gBgmReadPos - idx);
        const short *a = &gBgmWin[idx * 2];
        const short *b = &gBgmWin[(idx + 1) * 2];
        float l = a[0] + (b[0] - a[0]) * frac;
        float r = a[1] + (b[1] - a[1]) * frac;
        acc[i * 2]     += (int)(l * gBgmVolume);
        acc[i * 2 + 1] += (int)(r * gBgmVolume);
        gBgmReadPos += gBgmStep;
    }
}

static void mix_voice(Voice *v, int *acc, int frames) {
    SfxSample *s = v->smp;
    for (int i = 0; i < frames; i++) {
        unsigned idx = (unsigned)v->pos;
        if (idx + 1 >= s->frames) {
            if (v->loop && s->frames > 1) {
                v->pos -= (double)(s->frames - 1);
                if (v->pos < 0.0) v->pos = 0.0;
                idx = (unsigned)v->pos;
            } else {
                v->smp = NULL; // finished; slot is free again
                return;
            }
        }
        float frac = (float)(v->pos - idx);
        float l, r;
        if (s->channels == 2) {
            const short *a = &s->pcm[idx * 2];
            const short *b = &s->pcm[(idx + 1) * 2];
            l = a[0] + (b[0] - a[0]) * frac;
            r = a[1] + (b[1] - a[1]) * frac;
        } else {
            float m = s->pcm[idx] + (s->pcm[idx + 1] - s->pcm[idx]) * frac;
            l = r = m;
        }
        acc[i * 2]     += (int)(l * v->gl);
        acc[i * 2 + 1] += (int)(r * v->gr);
        v->pos += v->step;
    }
}

static int mixer_thread(SceSize args, void *argp) {
    static short out[2][MIX_GRAIN * 2];
    int acc[MIX_GRAIN * 2];
    int bufId = 0;

    while (!gQuit) {
        memset(acc, 0, sizeof(acc));

        pthread_mutex_lock(&gLock);
        mix_bgm(acc, MIX_GRAIN);
        for (int i = 0; i < MAX_VOICES; i++) {
            if (gVoices[i].smp && !gVoices[i].paused)
                mix_voice(&gVoices[i], acc, MIX_GRAIN);
        }
        pthread_mutex_unlock(&gLock);

        for (int i = 0; i < MIX_GRAIN * 2; i++)
            out[bufId][i] = soft_clip16(acc[i]);

        sceAudioOutOutput(gPort, out[bufId]); // blocks until the block is queued
        bufId ^= 1;
    }
    return 0;
}

//! @see docs/comments/audio.cpp.md#init--shutdown-section

void audio_init() {
    gPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, MIX_GRAIN, MIX_RATE,
                                SCE_AUDIO_OUT_MODE_STEREO);
    if (gPort < 0) {
        l_error("sceAudioOutOpenPort failed (0x%08X) -- audio disabled, game continues silent", (unsigned)gPort);
        return;
    }
    //! @see docs/comments/audio.cpp.md#audio_init--sceaudiooutopenport-volume-note

    //! @see docs/comments/audio.cpp.md#audio_init--mixer-thread-stack-size
    gThread = sceKernelCreateThread("audio mixer", mixer_thread, 0x10000100, 0x20000, 0, 0, NULL);
    if (gThread < 0) {
        l_error("audio mixer thread creation failed (0x%08X) -- audio disabled", (unsigned)gThread);
        sceAudioOutReleasePort(gPort);
        gPort = -1;
        return;
    }
    sceKernelStartThread(gThread, 0, NULL);
    gAudioReady = true;

    if (audio_file_exists(DATA_PATH "Data/Audio/Music/POP_BGM_Menu.mp3")) {
        l_info("Audio initialized (sceAudioOut mixer), assets present at " DATA_PATH "Data/Audio/");
    } else {
        l_warn("Audio initialized but " DATA_PATH "Data/Audio/ seems missing -- copy Data/Audio to the memory card or everything will be silent");
    }
}

void audio_shutdown() {
    if (!gAudioReady)
        return;
    gAudioReady = false;
    gQuit = 1;
    sceKernelWaitThreadEnd(gThread, NULL, NULL);
    sceKernelDeleteThread(gThread);
    gThread = -1;
    sceAudioOutReleasePort(gPort);
    gPort = -1;

    for (int i = 0; i < MAX_VOICES; i++)
        gVoices[i].smp = NULL;
    for (std::map<std::string, SfxSample *>::iterator it = gSfxCache.begin(); it != gSfxCache.end(); ++it) {
        free(it->second->pcm);
        delete it->second;
    }
    gSfxCache.clear();
    if (gBgmMp3Open) { mp3dec_ex_close(&gBgmMp3); gBgmMp3Open = false; }
    free(gBgmMp3Buf); gBgmMp3Buf = NULL;
}

//! @see docs/comments/audio.cpp.md#background-music-section

/** @brief Loads (or reuses) the BGM decoder for `raw`; never touches stdio.
 *  @note See docs/comments/audio.cpp.md#bgm_prepare--load--reuse-bgm-decoder
 */
static bool bgm_prepare(const char *raw) {
    std::string path = resolve_audio_file(raw);
    if (path.empty())
        return false;
    if (gBgmMp3Open && path == gBgmPath)
        return true;

    int len = 0;
    unsigned char *mp3 = read_entire_file(path, &len);
    if (!mp3)
        return false;

    mp3dec_ex_t dec;
    if (mp3dec_ex_open_buf(&dec, mp3, (size_t) len, MP3D_SEEK_TO_SAMPLE) != 0) {
        l_error("Failed to open BGM: %s", path.c_str());
        free(mp3);
        return false;
    }
    if (dec.info.channels < 1 || dec.info.channels > 2 || dec.info.hz == 0) {
        l_error("Unsupported BGM format: %s (rate=%d ch=%d)", path.c_str(), dec.info.hz, dec.info.channels);
        mp3dec_ex_close(&dec);
        free(mp3);
        return false;
    }

    pthread_mutex_lock(&gLock);
    bool hadOld = gBgmMp3Open;
    mp3dec_ex_t oldDec = gBgmMp3;
    unsigned char *oldBuf = gBgmMp3Buf;
    gBgmMp3 = dec;
    gBgmMp3Buf = mp3;
    gBgmMp3Open = true;
    gBgmChannels = dec.info.channels;
    gBgmPath = path;
    gBgmStep = (double) dec.info.hz / (double) MIX_RATE;
    gBgmReadPos = 0.0;
    gBgmAvail = 0;
    gBgmPlaying = false;
    gBgmPaused = false;
    gBgmEnded = false;
    pthread_mutex_unlock(&gLock);

    if (hadOld) mp3dec_ex_close(&oldDec); // mixer can't touch it anymore
    free(oldBuf);
    return true;
}

void Cocos2dxMusic_playBackgroundMusic(jmethodID id, va_list args) {
    jstring j_path = va_arg(args, jstring);
    jboolean isLoop = (jboolean)va_arg(args, int);
    if (!gAudioReady)
        return;
    if (!bgm_prepare((const char *)j_path))
        return; // silence, never a crash

    pthread_mutex_lock(&gLock);
    mp3dec_ex_seek(&gBgmMp3, 0);
    gBgmReadPos = 0.0;
    gBgmAvail = 0;
    gBgmLoop = isLoop ? true : false;
    gBgmPlaying = true;
    gBgmPaused = false;
    gBgmEnded = false;
    pthread_mutex_unlock(&gLock);
    l_info("BGM playing: %s (loop=%d)", gBgmPath.c_str(), (int)isLoop);
}

void Cocos2dxMusic_stopBackgroundMusic(jmethodID id, va_list args) {
    if (!gAudioReady) return;
    pthread_mutex_lock(&gLock);
    gBgmPlaying = false;
    gBgmEnded = false;
    if (gBgmMp3Open) { mp3dec_ex_seek(&gBgmMp3, 0); gBgmReadPos = 0.0; gBgmAvail = 0; }
    pthread_mutex_unlock(&gLock);
}

void Cocos2dxMusic_pauseBackgroundMusic(jmethodID id, va_list args) {
    if (!gAudioReady) return;
    pthread_mutex_lock(&gLock);
    gBgmPaused = true;
    pthread_mutex_unlock(&gLock);
}

void audio_pause_bgm_for_video() {
    if (!gAudioReady) return;
    pthread_mutex_lock(&gLock);
    gBgmPaused = true;
    pthread_mutex_unlock(&gLock);
}

void audio_resume_bgm_after_video() {
    if (!gAudioReady) return;
    pthread_mutex_lock(&gLock);
    if (gBgmMp3Open && gBgmPlaying)
        gBgmPaused = false;
    pthread_mutex_unlock(&gLock);
}

void Cocos2dxMusic_resumeBackgroundMusic(jmethodID id, va_list args) {
    if (!gAudioReady) return;
    pthread_mutex_lock(&gLock);
    if (gBgmMp3Open && gBgmPlaying)
        gBgmPaused = false;
    pthread_mutex_unlock(&gLock);
}

void Cocos2dxMusic_rewindBackgroundMusic(jmethodID id, va_list args) {
    if (!gAudioReady) return;
    pthread_mutex_lock(&gLock);
    if (gBgmMp3Open) {
        mp3dec_ex_seek(&gBgmMp3, 0);
        gBgmReadPos = 0.0;
        gBgmAvail = 0;
        gBgmEnded = false;
        gBgmPaused = false;
    }
    pthread_mutex_unlock(&gLock);
}

jboolean Cocos2dxMusic_isBackgroundMusicPlaying(jmethodID id, va_list args) {
    return (gAudioReady && gBgmMp3Open && gBgmPlaying && !gBgmPaused && !gBgmEnded) ? JNI_TRUE : JNI_FALSE;
}

jfloat Cocos2dxMusic_getBackgroundMusicVolume(jmethodID id, va_list args) {
    return gBgmVolume;
}

void Cocos2dxMusic_setBackgroundMusicVolume(jmethodID id, va_list args) {
    jfloat volume = (jfloat)va_arg(args, double);
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    gBgmVolume = volume; // single float store: safe without the lock
}

void Cocos2dxMusic_preloadBackgroundMusic(jmethodID id, va_list args) {
    jstring j_path = va_arg(args, jstring);
    if (!gAudioReady)
        return;
    bgm_prepare((const char *)j_path); // decoder ready so play starts without a hitch
}

//! @see docs/comments/audio.cpp.md#sound-effects-section

static SfxSample *sfx_get(const char *raw) {
    std::string path = resolve_audio_file(raw);
    if (path.empty())
        return NULL;

    std::map<std::string, SfxSample *>::iterator it = gSfxCache.find(path);
    if (it != gSfxCache.end())
        return it->second;

    int len = 0;
    unsigned char *mp3 = read_entire_file(path, &len);
    if (!mp3)
        return NULL;

    mp3dec_t dec;
    mp3dec_file_info_t info;
    memset(&info, 0, sizeof(info));
    int ret = mp3dec_load_buf(&dec, mp3, (size_t) len, &info, NULL, NULL);
    free(mp3);
    if (ret != 0 || !info.buffer || info.samples == 0 || info.channels < 1 || info.channels > 2 || info.hz <= 0) {
        l_error("Failed to decode SFX: %s (ret=%d samples=%zu ch=%d rate=%d)",
                path.c_str(), ret, info.samples, info.channels, info.hz);
        free(info.buffer);
        return NULL;
    }

    SfxSample *s = new (std::nothrow) SfxSample;
    if (!s) {
        free(info.buffer);
        return NULL;
    }
    s->pcm = info.buffer;
    s->frames = (unsigned)(info.samples / (size_t) info.channels);
    s->channels = info.channels;
    s->rate = info.hz;

    pthread_mutex_lock(&gLock);
    gSfxCache[path] = s;
    pthread_mutex_unlock(&gLock);
    return s;
}

jint Cocos2dxSound_playEffect(jmethodID id, va_list args) {
    jstring j_path = va_arg(args, jstring);
    jboolean isLoop = (jboolean)va_arg(args, int);
    //! @see docs/comments/audio.cpp.md#cocos2dxsound_playeffect--jni-signature-warning
    jfloat pitch = 1.0f;
    jfloat pan = 0.0f;
    jfloat gain = 1.0f;
    
    l_debug("playEffect: path=%s loop=%d pitch=%f pan=%f gain=%f", (const char*)j_path, (int)isLoop, (double)pitch, (double)pan, (double)gain);

    jint handle = gNextHandle++;
    if (handle <= 0) handle = gNextHandle = 1; // paranoid wrap-around
    if (!gAudioReady)
        return handle;

    SfxSample *s = sfx_get((const char *)j_path);
    if (!s)
        return handle; // valid-looking handle, engine logic keeps moving

    if (pitch <= 0.0f) pitch = 1.0f;
    if (pitch < 0.25f) pitch = 0.25f;
    if (pitch > 4.0f) pitch = 4.0f;
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;

    float vol = gSfxVolume * gain;
    float gl = vol * (pan > 0.0f ? 1.0f - pan : 1.0f);
    float gr = vol * (pan < 0.0f ? 1.0f + pan : 1.0f);

    pthread_mutex_lock(&gLock);
    Voice *v = NULL;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!gVoices[i].smp) { v = &gVoices[i]; break; }
    }
    if (v) {
        v->pos = 0.0;
        v->step = (double)pitch * ((double)s->rate / (double)MIX_RATE);
        v->gl = gl;
        v->gr = gr;
        v->loop = isLoop ? true : false;
        v->paused = false;
        v->id = handle;
        v->smp = s; // set last: marks the slot in use for the mixer
    }
    pthread_mutex_unlock(&gLock);
    return handle;
}

void Cocos2dxSound_stopEffect(jmethodID id, va_list args) {
    jint soundId = va_arg(args, jint);
    if (!gAudioReady) return;
    pthread_mutex_lock(&gLock);
    for (int i = 0; i < MAX_VOICES; i++) {
        if (gVoices[i].smp && gVoices[i].id == soundId)
            gVoices[i].smp = NULL;
    }
    pthread_mutex_unlock(&gLock);
}

void Cocos2dxSound_stopAllEffects(jmethodID id, va_list args) {
    if (!gAudioReady) return;
    pthread_mutex_lock(&gLock);
    for (int i = 0; i < MAX_VOICES; i++)
        gVoices[i].smp = NULL;
    pthread_mutex_unlock(&gLock);
}

void Cocos2dxSound_pauseEffect(jmethodID id, va_list args) {
    jint soundId = va_arg(args, jint);
    if (!gAudioReady) return;
    pthread_mutex_lock(&gLock);
    for (int i = 0; i < MAX_VOICES; i++) {
        if (gVoices[i].smp && gVoices[i].id == soundId)
            gVoices[i].paused = true;
    }
    pthread_mutex_unlock(&gLock);
}

void Cocos2dxSound_resumeEffect(jmethodID id, va_list args) {
    jint soundId = va_arg(args, jint);
    if (!gAudioReady) return;
    pthread_mutex_lock(&gLock);
    for (int i = 0; i < MAX_VOICES; i++) {
        if (gVoices[i].smp && gVoices[i].id == soundId)
            gVoices[i].paused = false;
    }
    pthread_mutex_unlock(&gLock);
}

void Cocos2dxSound_pauseAllEffects(jmethodID id, va_list args) {
    if (!gAudioReady) return;
    pthread_mutex_lock(&gLock);
    for (int i = 0; i < MAX_VOICES; i++) {
        if (gVoices[i].smp)
            gVoices[i].paused = true;
    }
    pthread_mutex_unlock(&gLock);
}

void Cocos2dxSound_resumeAllEffects(jmethodID id, va_list args) {
    if (!gAudioReady) return;
    pthread_mutex_lock(&gLock);
    for (int i = 0; i < MAX_VOICES; i++) {
        if (gVoices[i].smp)
            gVoices[i].paused = false;
    }
    pthread_mutex_unlock(&gLock);
}

jint Cocos2dxSound_preloadEffect(jmethodID id, va_list args) {
    jstring j_path = va_arg(args, jstring);
    jint handle = gNextHandle++;
    if (!gAudioReady)
        return handle;
    sfx_get((const char *)j_path);
    return handle; // opaque non-zero token, same contract as playEffect
}

void Cocos2dxSound_unloadEffect(jmethodID id, va_list args) {
    jstring j_path = va_arg(args, jstring);
    if (!gAudioReady)
        return;

    std::string path = sanitize_audio_path((const char *)j_path);
    std::map<std::string, SfxSample *>::iterator it = gSfxCache.find(path);
    if (it == gSfxCache.end()) {
        it = gSfxCache.find(audio_fallback_path(path)); // may have loaded via fallback
        if (it == gSfxCache.end())
            return;
    }
    SfxSample *s = it->second;

    pthread_mutex_lock(&gLock);
    for (int i = 0; i < MAX_VOICES; i++) {
        if (gVoices[i].smp == s)
            gVoices[i].smp = NULL; //! @see docs/comments/audio.cpp.md#cocos2dxsound_unloadeffect--silence-before-free
    }
    gSfxCache.erase(it);
    pthread_mutex_unlock(&gLock);

    free(s->pcm);
    delete s;
}

jfloat Cocos2dxSound_getEffectsVolume(jmethodID id, va_list args) {
    return gSfxVolume;
}

void Cocos2dxSound_setEffectsVolume(jmethodID id, va_list args) {
    jfloat volume = (jfloat)va_arg(args, double);
    l_debug("setEffectsVolume: volume=%f", (double)volume);
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    gSfxVolume = volume; //! @see docs/comments/audio.cpp.md#cocos2dxsound_seteffectsvolume--gsfxvolume-scope
}
