// Cutscene playback via the Vita's native SceAvPlayer, wired into
// Cocos2dxActivity_playVideo (java.c). The original Android engine has no
// native video codec on this port, so this fully replaces that path instead
// of trying to bridge to anything Android-side.
//
// Design, matching the discipline established for audio (Docs/Fixes_Log.md
// #10/#11):
//  * NO stdio: SceAvPlayer's file I/O is wired directly to sceIo via a
//    SceAvPlayerFileReplacement, same as audio's sceIoOpen/Read/Lseek.
//  * Never hangs and never leaves the screen stuck: video_play() always
//    returns (on natural end, user skip, or any failure to open/init), so
//    the caller can unconditionally fire onVideoCompleted() afterwards --
//    that callback is what unblocks VideoLayer (see plan §9.20; the exact
//    hang this guards against if it's ever skipped).
//  * Frame data format: the Vita video decoder delivers YUV420 PLANAR (Y
//    plane, then U, then V, each subsampled 2x2) at details.video.width x
//    height -- this isn't spelled out in the vitasdk header, so if colors
//    ever come out wrong on real hardware, try NV12 (interleaved UV) first.

#include "video.h"
#include "video_path.h"
#include "audio.h"
#include "utils/logger.h"

#include <psp2/avplayer.h>
#include <psp2/sysmodule.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/ctrl.h>
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>

#include <vitaGL.h>

#include <malloc.h>
#include <string.h>
#include <string>

static bool gModuleLoaded = false;
static GLuint gVideoTex = 0;
static unsigned char *gRgbBuf = NULL;
static unsigned gRgbBufCap = 0;

// --- SceAvPlayer file I/O: sceIo only, never stdio ---
//
// SceAvPlayerFileReplacement's objectPointer is the only per-instance state
// it gives us, so the actual fd lives there and every callback below reads
// it back out via that SAME pointer AvPlayer threads through each call.
struct AvFileCtx {
    SceUID fd;
};

static int av_ctx_open(void *p, const char *filename) {
    AvFileCtx *ctx = (AvFileCtx *) p;
    SceUID fd = sceIoOpen(filename, SCE_O_RDONLY, 0);
    if (fd < 0) {
        l_error("video: sceIoOpen(%s) failed (0x%08X)", filename, (unsigned) fd);
        return -1;
    }
    ctx->fd = fd;
    return 0;
}

static int av_ctx_close(void *p) {
    AvFileCtx *ctx = (AvFileCtx *) p;
    if (ctx->fd >= 0) sceIoClose(ctx->fd);
    ctx->fd = -1;
    return 0;
}

static int av_ctx_read(void *p, uint8_t *buffer, uint64_t position, uint32_t length) {
    AvFileCtx *ctx = (AvFileCtx *) p;
    int n = sceIoPread(ctx->fd, buffer, length, (SceOff) position);
    return n;
}

static uint64_t av_ctx_size(void *p) {
    AvFileCtx *ctx = (AvFileCtx *) p;
    SceOff cur = sceIoLseek(ctx->fd, 0, SCE_SEEK_CUR);
    SceOff end = sceIoLseek(ctx->fd, 0, SCE_SEEK_END);
    sceIoLseek(ctx->fd, cur, SCE_SEEK_SET);
    return (uint64_t) end;
}

static void *av_alloc(void *arg, uint32_t alignment, uint32_t size) {
    (void) arg;
    return memalign(alignment, size);
}

static void av_free(void *arg, void *ptr) {
    (void) arg;
    free(ptr);
}

// --- YUV420 planar -> RGB888, BT.601, plain integer math ---

static inline unsigned char clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (unsigned char) v;
}

static void yuv420p_to_rgb(const unsigned char *src, unsigned w, unsigned h, unsigned char *dst) {
    const unsigned char *yp = src;
    const unsigned char *up = src + (size_t) w * h;
    const unsigned char *vp = up + (size_t) (w / 2) * (h / 2);
    for (unsigned y = 0; y < h; y++) {
        const unsigned char *yrow = yp + (size_t) y * w;
        const unsigned char *urow = up + (size_t) (y / 2) * (w / 2);
        const unsigned char *vrow = vp + (size_t) (y / 2) * (w / 2);
        unsigned char *drow = dst + (size_t) y * w * 3;
        for (unsigned x = 0; x < w; x++) {
            int Y = yrow[x];
            int U = urow[x / 2] - 128;
            int V = vrow[x / 2] - 128;
            drow[x*3+0] = clamp_u8(Y + ((91881 * V) >> 16));
            drow[x*3+1] = clamp_u8(Y - ((22554 * U + 46802 * V) >> 16));
            drow[x*3+2] = clamp_u8(Y + ((116130 * U) >> 16));
        }
    }
}

// --- fullscreen quad draw ---

static void draw_video_frame(const unsigned char *rgb, unsigned w, unsigned h) {
    if (!gVideoTex) {
        glGenTextures(1, &gVideoTex);
        glBindTexture(GL_TEXTURE_2D, gVideoTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glBindTexture(GL_TEXTURE_2D, gVideoTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, (GLsizei) w, (GLsizei) h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);

    glViewport(0, 0, 960, 544);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof(0, 960, 544, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    // Letterboxed fullscreen quad preserving the source aspect ratio.
    float srcAspect = (float) w / (float) h;
    float dstAspect = 960.0f / 544.0f;
    float qx0 = 0, qy0 = 0, qx1 = 960, qy1 = 544;
    if (srcAspect > dstAspect) {
        float qh = 960.0f / srcAspect;
        qy0 = (544.0f - qh) / 2.0f;
        qy1 = qy0 + qh;
    } else {
        float qw = 544.0f * srcAspect;
        qx0 = (960.0f - qw) / 2.0f;
        qx1 = qx0 + qw;
    }

    GLfloat verts[] = {
        qx0, qy0,  qx1, qy0,  qx0, qy1,  qx1, qy1,
    };
    GLfloat texcoords[] = {
        0, 0,  1, 0,  0, 1,  1, 1,
    };

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, verts);
    glTexCoordPointer(2, GL_FLOAT, 0, texcoords);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    vglSwapBuffers(GL_FALSE);
}

void video_init() {
    int ret = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
    if (ret < 0) {
        l_error("video: sceSysmoduleLoadModule(AVPLAYER) failed (0x%08X) -- cutscenes will be skipped", (unsigned) ret);
        gModuleLoaded = false;
        return;
    }
    gModuleLoaded = true;
    l_info("video: SceAvPlayer module loaded.");
}

void video_shutdown() {
    if (gVideoTex) {
        glDeleteTextures(1, &gVideoTex);
        gVideoTex = 0;
    }
    free(gRgbBuf);
    gRgbBuf = NULL;
    gRgbBufCap = 0;
    if (gModuleLoaded) {
        sceSysmoduleUnloadModule(SCE_SYSMODULE_AVPLAYER);
        gModuleLoaded = false;
    }
}

void video_play(const char *raw) {
    if (!gModuleLoaded) {
        l_warn("video: AVPLAYER module not loaded, skipping cutscene request \"%s\"", raw ? raw : "(null)");
        return;
    }

    std::string path = sanitize_video_path(raw);
    SceIoStat st;
    if (sceIoGetstat(path.c_str(), &st) < 0) {
        l_error("video: file not found: %s (raw request: %s)", path.c_str(), raw ? raw : "(null)");
        return;
    }

    AvFileCtx fileCtx = { -1 };

    SceAvPlayerInitData init;
    memset(&init, 0, sizeof(init));
    init.memoryReplacement.allocate = av_alloc;
    init.memoryReplacement.deallocate = av_free;
    init.memoryReplacement.allocateTexture = av_alloc;
    init.memoryReplacement.deallocateTexture = av_free;
    // Let SceAvPlayer handle file I/O internally. 
    // init.fileReplacement is unnecessary for standalone files on ux0:
    // and can cause thread synchronization issues or bugs.
    init.basePriority = 0x10000100;
    init.numOutputVideoFrameBuffers = 2;
    init.autoStart = SCE_TRUE;
    init.debugLevel = 0;

    SceAvPlayerHandle handle = sceAvPlayerInit(&init);
    // SceAvPlayerHandle is often a heap pointer in Vita which is > 0x81000000.
    // So treating it as a signed int makes it negative.
    // True errors are in the 0x80xxxxxx range (e.g. 0x806A0001).
    if ((unsigned)handle == 0 || (unsigned)handle == 0xFFFFFFFF || ((unsigned)handle & 0xFF000000) == 0x80000000) {
        l_error("video: sceAvPlayerInit failed (0x%08X) for %s", (unsigned) handle, path.c_str());
        return;
    }

    if (sceAvPlayerAddSource(handle, path.c_str()) < 0) {
        l_error("video: sceAvPlayerAddSource failed for %s", path.c_str());
        sceAvPlayerClose(handle);
        return;
    }

    l_info("video: playing %s", path.c_str());
    audio_pause_bgm_for_video();

    // Video audio goes through its own dedicated port, opened lazily once we
    // see the first audio frame (channel count/rate aren't known before
    // that) -- kept fully separate from the game's own mixer (audio.cpp) so
    // neither has to know about the other.
    int audioPort = -1;
    int audioChannels = 0;

    SceCtrlData pad_start;
    sceCtrlPeekBufferPositive(0, &pad_start, 1);
    uint32_t old_pad_buttons = pad_start.buttons;

    bool skipped = false;
    
    // Let autoStart handle the playback initiation.
    sceAvPlayerStart(handle); // Restore explicit start just in case!

    // Wait for the asynchronous video decoder to become active
    int wait_count = 0;
    while (!sceAvPlayerIsActive(handle) && wait_count < 500) {
        sceKernelDelayThread(10000); // 10ms
        wait_count++;
    }
    
    l_info("video: loop starting. active=%d, wait_count=%d", sceAvPlayerIsActive(handle), wait_count);
    
    int frame_count = 0;
    
    if (!sceAvPlayerIsActive(handle)) {
        l_warn("video: timed out waiting for video decoder to become active (%s)", path.c_str());
    }
    
    while (sceAvPlayerIsActive(handle)) {
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        uint32_t pressed = pad.buttons & ~old_pad_buttons;
        
        if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_START)) {
            l_info("video: skipped by user button press!");
            skipped = true;
            break;
        }
        old_pad_buttons = pad.buttons;

        SceAvPlayerFrameInfo video;
        if (sceAvPlayerGetVideoData(handle, &video)) {
            unsigned w = video.details.video.width;
            unsigned h = video.details.video.height;
            unsigned need = w * h * 3;
            if (need > gRgbBufCap) {
                free(gRgbBuf);
                gRgbBuf = (unsigned char *) malloc(need);
                gRgbBufCap = gRgbBuf ? need : 0;
            }
            if (gRgbBuf && gRgbBufCap >= need) {
                yuv420p_to_rgb(video.pData, w, h, gRgbBuf);
                draw_video_frame(gRgbBuf, w, h);
            }
        }

        SceAvPlayerFrameInfo audio;
        if (sceAvPlayerGetAudioData(handle, &audio)) {
            if (audioPort < 0) {
                audioChannels = audio.details.audio.channelCount;
                SceAudioOutMode mode = (audioChannels >= 2) ? SCE_AUDIO_OUT_MODE_STEREO : SCE_AUDIO_OUT_MODE_MONO;
                // AvPlayer delivers fixed-size chunks; 1024 frames/channel is
                // the common chunk size for this API in the homebrew
                // community and matches our own mixer's SCE_AUDIO_MIN_LEN-
                // aligned block sizing.
                audioPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 1024,
                                                (int) audio.details.audio.sampleRate, mode);
                if (audioPort < 0)
                    l_warn("video: sceAudioOutOpenPort for cutscene audio failed (0x%08X)", (unsigned) audioPort);
            }
            if (audioPort >= 0)
                sceAudioOutOutput(audioPort, audio.pData);
        }

        frame_count++;
        if (frame_count == 1) {
            l_info("video: successfully completed first loop iteration!");
        }

        sceKernelDelayThread(1000); // avoid a tight spin when neither frame type is ready yet
    }
    
    l_info("video: loop exited! active=%d, frames=%d", sceAvPlayerIsActive(handle), frame_count);

    if (audioPort >= 0)
        sceAudioOutReleasePort(audioPort);

    sceAvPlayerStop(handle);
    sceAvPlayerClose(handle);
    audio_resume_bgm_after_video();

    l_info("video: %s (%s)", skipped ? "skipped" : "finished", path.c_str());
}
