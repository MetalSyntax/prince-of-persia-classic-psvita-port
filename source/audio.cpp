// Cocos2dxMusic / Cocos2dxSound JNI surface implemented on a small
// self-contained mixer over sceAudioOut, following the reference ports:
// deadspace-vita drives sceAudioOut from its own output thread
// (loader/android/EAAudioCore.c) and pop2-vita keeps a single C runtime for
// everything. SoLoud was dropped after three crash rounds: its vendored
// WavStream casts SoLoud::File* to FILE* and expects an stb_vorbis compiled
// with its "file hack" (soloud_file_hack_on.h) -- our tree compiled stb_vorbis
// raw, so every BGM load ended in newlib fseek/ftell on a fake FILE and a
// branch into garbage (the whole psp2core family of 2026-07-07, see
// Docs/Fixes_Log.md #10 and plan §9.30).
//
// Hard rules kept from that debugging:
//  * NO stdio anywhere here: files are read with sceIo, decoding uses only
//    stb_vorbis' *_memory APIs (built with STB_VORBIS_NO_STDIO so the file
//    API doesn't even exist to be misused).
//  * No failure path ever executes a pointer: failed loads log + return a
//    valid dummy handle (never 0 -- Cocos2d-x loops on 0, the "infinite
//    jump" bug).
//  * Sample data referenced by the mixer thread is only freed after the
//    voices using it are silenced under the mixer lock (no use-after-free).
//
// Assets are stereo .ogg at 22050/32000/44100 Hz (measured); the mixer
// output runs at 44100 stereo and every voice resamples linearly with
// step = pitch * (src_rate / 44100).

#include "audio.h"
#include "audio_path.h"
#include "utils/logger.h"

#define STB_VORBIS_HEADER_ONLY
#include <stb/stb_vorbis.c>

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
// Frames per sceAudioOutOutput block (~46 ms). Matches the buffer size the
// removed SoLoud vita_homebrew backend used (Soloud::init's AUTO default is
// 2048) -- doubled from an initial 1024 to give the mixer thread (which does
// vorbis decode + resample + N-voice mixing, not just a memcpy) more slack
// before the hardware needs the next block, reducing underrun-driven
// crackle. See Docs/Fixes_Log.md #11.
#define MIX_GRAIN    2048
#define MAX_VOICES   12
#define BGM_WIN      2048 // decoded BGM window, in frames

// Soft-knee limiter onset, as a fraction of full scale. Below this the
// output is bit-identical to the mixed input (verified via a host-side
// bit-exact resampling test -- normal single/dual-voice playback never
// reaches this band). Above it, peaks are compressed smoothly toward but
// never past full scale instead of hard-clipping, so summing several
// simultaneously loud voices (BGM + footsteps + a sword hit, say) saturates
// gracefully instead of producing harsh digital clipping -- the reported
// "distorted" sound.
#define SOFT_CLIP_THRESHOLD 0.92f

// --- engine state (gLock protects everything the mixer thread reads) ---

static pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;
static bool gAudioReady = false;
static volatile int gQuit = 0;
static int gPort = -1;
static SceUID gThread = -1;

struct SfxSample {
    short *pcm;       // interleaved, native rate/channels, malloc'd by stb_vorbis
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

// --- BGM: streamed decode from the compressed ogg kept in RAM ---
static unsigned char *gBgmOgg = NULL; // malloc'd; must outlive gBgmV
static stb_vorbis *gBgmV = NULL;
static std::string gBgmPath;
static double gBgmStep = 1.0, gBgmReadPos = 0.0;
static short gBgmWin[BGM_WIN * 2];
static int gBgmAvail = 0;   // valid frames in gBgmWin
static bool gBgmPlaying = false, gBgmPaused = false, gBgmLoop = false, gBgmEnded = false;
static float gBgmVolume = 1.0f;

// --- file loading (sceIo only) ---

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

// malloc'd buffer (stb_vorbis_open_memory needs it alive while decoding).
static unsigned char *read_entire_file(const std::string &path, int *out_len) {
    *out_len = 0;
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) {
        l_error("sceIoOpen failed for %s (0x%08X)", path.c_str(), (unsigned)fd);
        return NULL;
    }
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (size <= 0 || size > 32 * 1024 * 1024) { // biggest game .ogg is <1MB
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

// --- mixing (mixer thread only, gLock held) ---

// Identity below SOFT_CLIP_THRESHOLD (verified bit-exact against ground
// truth for normal single/dual-voice playback); above it, compresses
// smoothly toward but never past full scale instead of hard-clipping.
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

// Refill gBgmWin so at least 2 frames are readable from gBgmReadPos.
// Returns false when the stream is over (and not looping).
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

        int got = stb_vorbis_get_samples_short_interleaved(
            gBgmV, 2, gBgmWin + gBgmAvail * 2, (BGM_WIN - gBgmAvail) * 2);
        if (got > 0) {
            gBgmAvail += got;
            continue;
        }
        if (gBgmLoop) {
            stb_vorbis_seek_start(gBgmV);
            continue;
        }
        return false; // stream exhausted
    }
}

static void mix_bgm(int *acc, int frames) {
    if (!gBgmV || !gBgmPlaying || gBgmPaused || gBgmEnded)
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

// --- init / shutdown ---

void audio_init() {
    gPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, MIX_GRAIN, MIX_RATE,
                                SCE_AUDIO_OUT_MODE_STEREO);
    if (gPort < 0) {
        l_error("sceAudioOutOpenPort failed (0x%08X) -- audio disabled, game continues silent", (unsigned)gPort);
        return;
    }
    // sceAudioOutOpenPort's own docs guarantee the port starts at
    // SCE_AUDIO_VOLUME_0DB (max) already -- no explicit sceAudioOutSetVolume
    // call needed. A previous version of this file called it anyway with an
    // untested channel-flag/array pairing; removed rather than risk it being
    // the reason output ended up quieter than the source material (verified
    // bit-exact/0dBFS-peaking via a host-side resampling test -- see
    // Docs/Fixes_Log.md #11).

    // 128KB stack: stb_vorbis decodes frames on this thread's stack and 64KB
    // was proven too small (core dump) during the SoLoud bring-up.
    gThread = sceKernelCreateThread("audio mixer", mixer_thread, 0x10000100, 0x20000, 0, 0, NULL);
    if (gThread < 0) {
        l_error("audio mixer thread creation failed (0x%08X) -- audio disabled", (unsigned)gThread);
        sceAudioOutReleasePort(gPort);
        gPort = -1;
        return;
    }
    sceKernelStartThread(gThread, 0, NULL);
    gAudioReady = true;

    if (audio_file_exists(DATA_PATH "Data/Audio/Music/POP_BGM_Menu.ogg")) {
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
    if (gBgmV) { stb_vorbis_close(gBgmV); gBgmV = NULL; }
    free(gBgmOgg); gBgmOgg = NULL;
}

// --- Background Music ---

// Loads (or reuses) the BGM decoder for `raw`. Returns false on any failure,
// leaving the previous BGM fully stopped and freed. Never touches stdio.
static bool bgm_prepare(const char *raw) {
    std::string path = resolve_audio_file(raw);
    if (path.empty())
        return false;
    if (gBgmV && path == gBgmPath)
        return true;

    int len = 0;
    unsigned char *ogg = read_entire_file(path, &len);
    if (!ogg)
        return false;

    int err = 0;
    stb_vorbis *v = stb_vorbis_open_memory(ogg, len, &err, NULL);
    if (!v) {
        l_error("Failed to open BGM: %s (stb_vorbis error %d)", path.c_str(), err);
        free(ogg);
        return false;
    }
    stb_vorbis_info info = stb_vorbis_get_info(v);
    if (info.channels < 1 || info.channels > 2 || info.sample_rate == 0) {
        l_error("Unsupported BGM format: %s (rate=%u ch=%d)", path.c_str(), info.sample_rate, info.channels);
        stb_vorbis_close(v);
        free(ogg);
        return false;
    }

    pthread_mutex_lock(&gLock);
    stb_vorbis *oldV = gBgmV;
    unsigned char *oldOgg = gBgmOgg;
    gBgmV = v;
    gBgmOgg = ogg;
    gBgmPath = path;
    gBgmStep = (double)info.sample_rate / (double)MIX_RATE;
    gBgmReadPos = 0.0;
    gBgmAvail = 0;
    gBgmPlaying = false;
    gBgmPaused = false;
    gBgmEnded = false;
    pthread_mutex_unlock(&gLock);

    if (oldV) stb_vorbis_close(oldV); // mixer can't touch it anymore
    free(oldOgg);
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
    stb_vorbis_seek_start(gBgmV);
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
    if (gBgmV) { stb_vorbis_seek_start(gBgmV); gBgmReadPos = 0.0; gBgmAvail = 0; }
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
    if (gBgmV && gBgmPlaying)
        gBgmPaused = false;
    pthread_mutex_unlock(&gLock);
}

void Cocos2dxMusic_resumeBackgroundMusic(jmethodID id, va_list args) {
    if (!gAudioReady) return;
    pthread_mutex_lock(&gLock);
    if (gBgmV && gBgmPlaying)
        gBgmPaused = false;
    pthread_mutex_unlock(&gLock);
}

void Cocos2dxMusic_rewindBackgroundMusic(jmethodID id, va_list args) {
    if (!gAudioReady) return;
    pthread_mutex_lock(&gLock);
    if (gBgmV) {
        stb_vorbis_seek_start(gBgmV);
        gBgmReadPos = 0.0;
        gBgmAvail = 0;
        gBgmEnded = false;
        gBgmPaused = false;
    }
    pthread_mutex_unlock(&gLock);
}

jboolean Cocos2dxMusic_isBackgroundMusicPlaying(jmethodID id, va_list args) {
    return (gAudioReady && gBgmV && gBgmPlaying && !gBgmPaused && !gBgmEnded) ? JNI_TRUE : JNI_FALSE;
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

// --- Sound Effects ---

static SfxSample *sfx_get(const char *raw) {
    std::string path = resolve_audio_file(raw);
    if (path.empty())
        return NULL;

    std::map<std::string, SfxSample *>::iterator it = gSfxCache.find(path);
    if (it != gSfxCache.end())
        return it->second;

    int len = 0;
    unsigned char *ogg = read_entire_file(path, &len);
    if (!ogg)
        return NULL;

    int channels = 0, rate = 0;
    short *pcm = NULL;
    int frames = stb_vorbis_decode_memory(ogg, len, &channels, &rate, &pcm);
    free(ogg);
    if (frames <= 0 || !pcm || channels < 1 || channels > 2 || rate <= 0) {
        l_error("Failed to decode SFX: %s (frames=%d ch=%d rate=%d)", path.c_str(), frames, channels, rate);
        free(pcm);
        return NULL;
    }

    SfxSample *s = new (std::nothrow) SfxSample;
    if (!s) {
        free(pcm);
        return NULL;
    }
    s->pcm = pcm;
    s->frames = (unsigned)frames;
    s->channels = channels;
    s->rate = rate;

    pthread_mutex_lock(&gLock);
    gSfxCache[path] = s;
    pthread_mutex_unlock(&gLock);
    return s;
}

jint Cocos2dxSound_playEffect(jmethodID id, va_list args) {
    jstring j_path = va_arg(args, jstring);
    jboolean isLoop = (jboolean)va_arg(args, int);
    // In older Cocos2d-x versions (like the one used in PoP Classic), the playEffect
    // JNI signature is (Ljava/lang/String;Z)I, meaning it ONLY passes path and loop.
    // If we read pitch, pan, and gain using va_arg, it reads uninitialized stack memory,
    // which results in gain=0.0f and completely mutes all sound effects.
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
            gVoices[i].smp = NULL; // silence before free: the mixer may be reading it
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
    gSfxVolume = volume; // applies to effects started from now on
}
