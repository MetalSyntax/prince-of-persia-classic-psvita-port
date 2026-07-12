// Cutscene playback via the Vita's native SceAvPlayer, wired into
// Cocos2dxActivity_playVideo (java.c). The original Android engine has no
// native video codec on this port, so this fully replaces that path instead
// of trying to bridge to anything Android-side.
//
// Design, matching the discipline established for audio (Docs/Fixes_Log.md
// #10/#11):
//  * File I/O stays inside SceAvPlayer (plain path on ux0:) -- no stdio of
//    ours anywhere near it. An earlier revision wired a
//    SceAvPlayerFileReplacement over sceIo; dropped as unnecessary for
//    standalone files.
//  * Never hangs and never leaves the screen stuck: video_play() always
//    returns (on natural end, user skip, or any failure to open/init), so
//    the caller can unconditionally fire onVideoCompleted() afterwards --
//    that callback is what unblocks VideoLayer (see plan §9.20; the exact
//    hang this guards against if it's ever skipped).
//  * Frame data format: confirmed on real hardware to be NV12 (Y plane,
//    then interleaved U/V, each subsampled 2x2) -- the vitasdk header
//    doesn't spell this out. An earlier revision assumed fully-planar
//    I420 (separate U and V planes), which produced a green tint.

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
#include <psp2/kernel/sysmem.h>
#include <psp2/gxm.h>

#include <vitaGL.h>

#include <malloc.h>
#include <string.h>
#include <string>

static bool gModuleLoaded = false;
static GLuint gVideoTex = 0;
static unsigned char *gRgbBuf = NULL;
static unsigned gRgbBufCap = 0;

// --- SceAvPlayer file I/O (restored, with full visibility) ---
//
// Internal file I/O was in use while the player kept dying with zero
// decoded frames and no error surfaced anywhere (log_000028) -- with it,
// the file access is a black box. Routing I/O through sceIo again both
// bypasses anything odd in the player's internal FIOS2 usage (our process
// initializes FIOS with its own overlay at boot -- see lib/fios/fios.c)
// and lets the log show exactly how far the player got into the file
// before giving up.
struct AvFileCtx {
    SceUID fd;
    uint64_t total_read;
    unsigned read_calls;
};
static AvFileCtx gAvFileCtx = { -1, 0, 0 };

static int av_file_open(void *p, const char *filename) {
    AvFileCtx *ctx = (AvFileCtx *) p;
    ctx->fd = sceIoOpen(filename, SCE_O_RDONLY, 0);
    ctx->total_read = 0;
    ctx->read_calls = 0;
    l_info("video: file open(%s) -> fd=0x%08X", filename, (unsigned) ctx->fd);
    return ctx->fd < 0 ? -1 : 0;
}

static int av_file_close(void *p) {
    AvFileCtx *ctx = (AvFileCtx *) p;
    l_info("video: file close (reads=%u, total_bytes=%llu)", ctx->read_calls,
           (unsigned long long) ctx->total_read);
    if (ctx->fd >= 0) sceIoClose(ctx->fd);
    ctx->fd = -1;
    return 0;
}

static int av_file_read(void *p, uint8_t *buffer, uint64_t position, uint32_t length) {
    AvFileCtx *ctx = (AvFileCtx *) p;
    int n = sceIoPread(ctx->fd, buffer, length, (SceOff) position);
    ctx->read_calls++;
    // First few reads and any failure tell the story; per-read logging
    // beyond that is exactly the high-frequency-logging trap from
    // Fixes_Log.md #12, so it's capped.
    if (ctx->read_calls <= 5 || n < 0)
        l_info("video: file read #%u pos=%llu len=%u -> %d", ctx->read_calls,
               (unsigned long long) position, length, n);
    if (n > 0) ctx->total_read += (uint64_t) n;
    return n;
}

static uint64_t av_file_size(void *p) {
    AvFileCtx *ctx = (AvFileCtx *) p;
    SceOff end = sceIoLseek(ctx->fd, 0, SCE_SEEK_END);
    l_info("video: file size -> %llu", (unsigned long long) end);
    return (uint64_t) end;
}

// --- SceAvPlayer event callback: the player's own diagnostic channel ---
//
// Every state transition (and, crucially, warning/error codes) arrives
// here. Without it, a playback abort is silent -- IsActive just flips to
// false. This is what finally tells us WHY.
static const char *av_event_name(int32_t id) {
    switch (id) {
        case 0x01: return "STATE_STOP";
        case 0x02: return "STATE_READY";
        case 0x03: return "STATE_PLAY";
        case 0x04: return "STATE_PAUSE";
        case 0x05: return "STATE_BUFFERING";
        case 0x10: return "TIMED_TEXT_DELIVERY";
        case 0x20: return "WARNING_ID";
        default:   return "?";
    }
}

static void av_event_cb(void *p, int32_t eventId, int32_t sourceId, void *eventData) {
    (void) p;
    if (eventId == 0x20 && eventData) {
        l_error("video: event WARNING_ID source=%d code=0x%08X", sourceId,
                (unsigned) *(int32_t *) eventData);
    } else {
        l_info("video: event %s (0x%02X) source=%d data=%p", av_event_name(eventId),
               (unsigned) eventId, sourceId, eventData);
    }
}

// --- SceAvPlayer memory: the pattern proven on real hardware by the
// so-loader ports that ship working video (gtasa_vita's movie.c and its
// many descendants). Two DIFFERENT allocators, and the difference matters:
//
//  * allocate/deallocate (general): plain memalign/free from the newlib
//    heap. AvPlayer makes MANY small internal allocations (demuxer state,
//    stream read buffers, queues) through this pair. A previous revision
//    of this file backed EVERY one of these with its own
//    sceKernelAllocMemBlock -- that exhausts the process's memblock limit
//    within the player's startup burst, at which point an internal
//    allocation returns NULL and the player silently transitions to
//    inactive right after activating: exactly the observed
//    "active=1, then dead within one loop iteration, frames=0".
//
//  * allocateTexture/deallocateTexture: the hardware AVC decoder writes
//    decoded frames here, which requires physically contiguous memory
//    (PHYCONT), not ordinary heap. vglAlloc(VGL_MEM_SLOW) is vitaGL's
//    PHYCONT pool. Only a handful of these allocations ever happen (the
//    numOutputVideoFrameBuffers frame buffers), so pool pressure is not a
//    concern. 256KB minimum alignment per the same reference ports.
#define AV_FB_ALIGNMENT 0x40000
#define AV_ALIGN_MEM(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

static void *av_alloc(void *arg, uint32_t alignment, uint32_t size) {
    (void) arg;
    void *p = memalign(alignment, size);
    if (!p)
        l_error("video: general alloc FAILED (align=%u size=%u)", alignment, size);
    return p;
}

static void av_free(void *arg, void *ptr) {
    (void) arg;
    free(ptr);
}

// Frame-buffer allocations now replicate OpenFMV's gpu_alloc EXACTLY: a
// DEDICATED kernel memblock per allocation (CDRAM, kernel-guaranteed
// alignment via SceKernelAllocMemBlockOpt, sceGxmMapMemory'd), not a slice
// of vitaGL's big pre-mapped pool. This is the last structural difference
// left standing after log_000029/30: pool-served buffers -- PHYCONT or
// CDRAM, correctly aligned, every allocation succeeding -- still ended in a
// silent STATE_STOP with zero frames. The plausible mechanism: the AVC
// decoder identifies/pins the memblock that OWNS the address it's given
// (sceKernelFindMemBlockByAddr-style); an address in the middle of vitaGL's
// giant pool block resolves to a block with the wrong size/owner/flags and
// gets rejected -- silently, as observed. A dedicated block per allocation
// is exactly what OpenFMV ships with on real hardware.
#define AV_TEX_MAX_BLOCKS 8
static struct { void *base; SceUID uid; } gAvTexBlocks[AV_TEX_MAX_BLOCKS];

static void *av_alloc_texture(void *arg, uint32_t alignment, uint32_t size) {
    (void) arg;
    uint32_t req_align = alignment, req_size = size;
    if (alignment < AV_FB_ALIGNMENT)
        alignment = AV_FB_ALIGNMENT;
    size = AV_ALIGN_MEM(size, alignment);

    SceKernelAllocMemBlockOpt opt;
    memset(&opt, 0, sizeof(opt));
    opt.size = sizeof(opt);
    opt.attr = 0x00000004U; // SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT
    opt.alignment = alignment;
    SceUID blk = sceKernelAllocMemBlock("av_tex", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size, &opt);
    if (blk < 0) {
        l_error("video: texture memblock alloc FAILED 0x%08X (req align=%u size=%u -> size=%u) -- CDRAM budget likely claimed by vitaGL",
                (unsigned) blk, req_align, req_size, size);
        return NULL;
    }
    void *base = NULL;
    sceKernelGetMemBlockBase(blk, &base);
    int map = sceGxmMapMemory(base, size, (SceGxmMemoryAttribFlags)(SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE));

    int slot = -1;
    for (int i = 0; i < AV_TEX_MAX_BLOCKS; i++) {
        if (!gAvTexBlocks[i].base) { slot = i; break; }
    }
    if (slot >= 0) {
        gAvTexBlocks[slot].base = base;
        gAvTexBlocks[slot].uid = blk;
    }
    l_info("video: texture memblock ok (req align=%u size=%u -> size=%u) base=%p uid=0x%08X gxm_map=0x%08X",
           req_align, req_size, size, base, (unsigned) blk, (unsigned) map);
    return base;
}

static void av_free_texture(void *arg, void *ptr) {
    (void) arg;
    if (!ptr) return;
    glFinish();
    for (int i = 0; i < AV_TEX_MAX_BLOCKS; i++) {
        if (gAvTexBlocks[i].base == ptr) {
            l_info("video: texture memblock free %p uid=0x%08X", ptr, (unsigned) gAvTexBlocks[i].uid);
            sceGxmUnmapMemory(ptr);
            sceKernelFreeMemBlock(gAvTexBlocks[i].uid);
            gAvTexBlocks[i].base = NULL;
            gAvTexBlocks[i].uid = -1;
            return;
        }
    }
    l_warn("video: texture free for unknown ptr %p (leaking it)", ptr);
}

// --- YUV420 planar -> RGB888, BT.601, plain integer math ---

static int CV_R[256];
static int CV_G[256];
static int CU_G[256];
static int CU_B[256];
static unsigned char clip_table[768];
static bool tables_init = false;

static void init_yuv_tables() {
    if (tables_init) return;
    for (int i = 0; i < 256; i++) {
        int V = i - 128;
        int U = i - 128;
        CV_R[i] = (91881 * V) >> 16;
        CV_G[i] = (46802 * V) >> 16;
        CU_G[i] = (22554 * U) >> 16;
        CU_B[i] = (116130 * U) >> 16;
    }
    for (int i = 0; i < 768; i++) {
        int v = i - 256;
        clip_table[i] = (v < 0) ? 0 : ((v > 255) ? 255 : v);
    }
    tables_init = true;
}

#define CLIP(X) (clip_table[(X) + 256])

static void yuv420p_to_rgba(const unsigned char *src, unsigned w, unsigned h, unsigned char *dst) {
    init_yuv_tables();
    const unsigned char *yp = src;
    const unsigned char *uvp = src + (size_t) w * h;
    for (unsigned y = 0; y < h; y++) {
        const unsigned char *yrow = yp + (size_t) y * w;
        const unsigned char *uvrow = uvp + (size_t) (y / 2) * w;
        unsigned char *drow = dst + (size_t) y * w * 4;
        for (unsigned x = 0; x < w; x += 2) {
            unsigned char U = uvrow[x + 0];
            unsigned char V = uvrow[x + 1];
            
            int r_add = CV_R[V];
            int g_add = -(CU_G[U] + CV_G[V]);
            int b_add = CU_B[U];

            int Y0 = yrow[x];
            drow[x*4+0] = CLIP(Y0 + r_add);
            drow[x*4+1] = CLIP(Y0 + g_add);
            drow[x*4+2] = CLIP(Y0 + b_add);
            drow[x*4+3] = 255;

            int Y1 = yrow[x+1];
            drow[x*4+4] = CLIP(Y1 + r_add);
            drow[x*4+5] = CLIP(Y1 + g_add);
            drow[x*4+6] = CLIP(Y1 + b_add);
            drow[x*4+7] = 255;
        }
    }
}

// --- fullscreen quad draw ---

static void draw_video_frame(const unsigned char *rgba, unsigned w, unsigned h) {
    if (!gVideoTex) {
        glGenTextures(1, &gVideoTex);
        glBindTexture(GL_TEXTURE_2D, gVideoTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glBindTexture(GL_TEXTURE_2D, gVideoTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei) w, (GLsizei) h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

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
    l_info("video: SceAvPlayer module loaded. [vitaGL pools free: PHYCONT=%u CDRAM=%u RAM=%u]",
           (unsigned) vglMemFree(VGL_MEM_SLOW), (unsigned) vglMemFree(VGL_MEM_VRAM),
           (unsigned) vglMemFree(VGL_MEM_RAM));
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

    SceAvPlayerInitData init;
    memset(&init, 0, sizeof(init));
    init.memoryReplacement.allocate = av_alloc;
    init.memoryReplacement.deallocate = av_free;
    init.memoryReplacement.allocateTexture = av_alloc_texture;
    init.memoryReplacement.deallocateTexture = av_free_texture;
    init.fileReplacement.objectPointer = &gAvFileCtx;
    init.fileReplacement.open = av_file_open;
    init.fileReplacement.close = av_file_close;
    init.fileReplacement.readOffset = av_file_read;
    init.fileReplacement.size = av_file_size;
    init.eventReplacement.objectPointer = NULL;
    init.eventReplacement.eventCallback = av_event_cb;
    // basePriority 0xA0: the value the known-working reference ports use.
    // The previous 0x10000100 (SCE_KERNEL_DEFAULT_PRIORITY_USER) is a
    // special sentinel, and AvPlayer derives its INTERNAL thread priorities
    // by offsetting from this base -- offsets from the sentinel are not
    // valid priorities.
    init.basePriority = 0xA0;
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
    unsigned audioFrameLen = 0; // frames/channel per sceAudioOutOutput() call, derived below

    SceCtrlData pad_start;
    sceCtrlPeekBufferPositive(0, &pad_start, 1);
    uint32_t old_pad_buttons = pad_start.buttons;

    bool skipped = false;

    // autoStart=SCE_TRUE already starts playback inside AddSource -- no
    // explicit sceAvPlayerStart, matching the reference ports.

    // Wait for the asynchronous video decoder to become active
    int wait_count = 0;
    while (!sceAvPlayerIsActive(handle) && wait_count < 500) {
        sceKernelDelayThread(10000); // 10ms
        wait_count++;
    }
    
    l_info("video: loop starting. active=%d, wait_count=%d", sceAvPlayerIsActive(handle), wait_count);

    int frame_count = 0;
    int video_frames = 0, audio_frames = 0;
    bool audioOpenAttempted = false;

    if (!sceAvPlayerIsActive(handle)) {
        l_warn("video: timed out waiting for video decoder to become active (%s)", path.c_str());
    }

    // draw_video_frame() overwrites the projection/modelview matrices and
    // disables blend/depth-test for its fullscreen quad -- save them here
    // and restore below so cocos2d's own rendering isn't left corrupted
    // once the cutscene ends (previously invisible because the decoder
    // always died before a single frame was ever drawn).
    GLint savedViewport[4];
    glGetIntegerv(GL_VIEWPORT, savedViewport);
    GLboolean savedBlend = glIsEnabled(GL_BLEND);
    GLboolean savedDepthTest = glIsEnabled(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();

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
            if (++video_frames == 1)
                l_info("video: first video frame decoded (%ux%u, pData=%p)", w, h, video.pData);
            unsigned need = w * h * 4;
            if (need > gRgbBufCap) {
                free(gRgbBuf);
                gRgbBuf = (unsigned char *) malloc(need);
                gRgbBufCap = gRgbBuf ? need : 0;
            }
            if (gRgbBuf && gRgbBufCap >= need) {
                yuv420p_to_rgba(video.pData, w, h, gRgbBuf);
                draw_video_frame(gRgbBuf, w, h);
            }
        }

        SceAvPlayerFrameInfo audio;
        if (sceAvPlayerGetAudioData(handle, &audio)) {
            if (++audio_frames == 1)
                l_info("video: first audio frame decoded (ch=%u rate=%u)",
                       (unsigned) audio.details.audio.channelCount,
                       (unsigned) audio.details.audio.sampleRate);
            if (audioPort < 0 && !audioOpenAttempted) {
                audioOpenAttempted = true; // one attempt only -- see below
                audioChannels = audio.details.audio.channelCount;
                SceAudioOutMode mode = (audioChannels >= 2) ? SCE_AUDIO_OUT_MODE_STEREO : SCE_AUDIO_OUT_MODE_MONO;
                // sceAudioOutOutput() takes NO length argument -- it always
                // outputs exactly the port's `len` (frames/channel) from
                // whatever buffer it's given. A prior revision hardcoded
                // len=1024 as "the common AvPlayer chunk size"; if the
                // decoder's actual chunk size differs (details.audio.size is
                // the REAL per-frame byte size AvPlayer reports), every
                // single sceAudioOutOutput call over/under-reads audio.pData
                // by the mismatch -- exactly the "choppy/cut" playback
                // reported (log_000039), as opposed to an occasional
                // scheduling hiccup. Derive it from the frame instead of
                // assuming it.
                audioFrameLen = audio.details.audio.size / (audioChannels * sizeof(int16_t));
                l_info("video: cutscene audio port: %u frames/channel (size=%u bytes, ch=%u)",
                       audioFrameLen, (unsigned) audio.details.audio.size, audioChannels);
                //
                // Port type is VOICE, not MAIN: the vitasdk header spells out
                // that MAIN "must be set to 48000 Hz" -- every 44100Hz
                // cutscene (most of them; only PoP_V1_1 is 48000) was hitting
                // exactly that with MAIN, confirmed by log_000032/38's
                // 0x80260008 = SCE_AUDIO_OUT_ERROR_INVALID_SAMPLE_FREQ (not a
                // port-full/contention error as first assumed). VOICE has no
                // such restriction and is a different type than our own
                // mixer's single BGM port (audio.cpp), so it can't collide
                // with it either.
                audioPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_VOICE, audioFrameLen,
                                                (int) audio.details.audio.sampleRate, mode);
                if (audioPort < 0)
                    // Retrying this every audio frame (as a prior revision
                    // did) hammers a real, fairly expensive driver call --
                    // confirmed in log_000032 firing 400+ times in one
                    // cutscene and stalling the frame pump (the "video is
                    // slow" symptom). Fail silent (video keeps playing,
                    // just muted) instead.
                    l_warn("video: sceAudioOutOpenPort for cutscene audio failed (0x%08X) -- cutscene audio disabled",
                           (unsigned) audioPort);
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
    
    l_info("video: loop exited! active=%d, iterations=%d, video_frames=%d, audio_frames=%d",
           sceAvPlayerIsActive(handle), frame_count, video_frames, audio_frames);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    if (savedBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (savedDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);

    if (audioPort >= 0)
        sceAudioOutReleasePort(audioPort);

    sceAvPlayerStop(handle);
    sceAvPlayerClose(handle);
    audio_resume_bgm_after_video();

    l_info("video: %s (%s)", skipped ? "skipped" : "finished", path.c_str());
}
