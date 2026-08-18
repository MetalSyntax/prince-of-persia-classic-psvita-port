/**
 * @file  video.cpp
 * @brief Cutscene playback via the Vita's native SceAvPlayer: YUV->RGB565
 *        conversion (LUTs + hand-written NEON), GL state save/restore
 *        around the video draw call, and a dedicated cutscene-audio thread.
 *        @note See docs/comments/video.cpp.md for design rationale.
 */

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
#include <psp2/kernel/processmgr.h>
#include <psp2/gxm.h>

#include <vitaGL.h>

#include <arm_neon.h>
#include <malloc.h>
#include <pthread.h>
#include <string.h>
#include <string>

static bool gModuleLoaded = false;
static GLuint gVideoTex = 0;
static unsigned gVideoTexW = 0;
static unsigned gVideoTexH = 0;
static unsigned short *gRgbBuf = NULL;
static unsigned gRgbBufCap = 0;
static unsigned char *gYuvScratch = NULL;
static unsigned gYuvScratchCap = 0;

//! @see docs/comments/video.cpp.md#gpu-shader-nv12-rgb-path-reverted

// --- SceAvPlayer file I/O (restored, with full visibility) ---
//! @see docs/comments/video.cpp.md#sceavplayer-file-io--restored-with-full-visibility
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
    //! @see docs/comments/video.cpp.md#av_file_read--capped-per-read-logging
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
//! @see docs/comments/video.cpp.md#sceavplayer-event-callback--diagnostic-channel
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

// --- SceAvPlayer memory: general vs. texture allocators ---
//! @see docs/comments/video.cpp.md#sceavplayer-memory--general-vs-texture-allocators
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

// --- Frame-buffer allocation: dedicated memblock per allocation (OpenFMV pattern) ---
//! @see docs/comments/video.cpp.md#frame-buffer-allocation--dedicated-memblock-openfmv-pattern
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
    SceUID usedType = SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW;
    if (blk < 0) {
        //! @see docs/comments/video.cpp.md#av_alloc_texture--cdram-failure-and-phycont-fallback
        uint32_t freeCdram = (unsigned) vglMemFree(VGL_MEM_VRAM);
        SceUID blk2 = sceKernelAllocMemBlock("av_tex_phycont", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, size, &opt);
        if (blk2 < 0) {
            l_error("video: texture memblock alloc FAILED on both CDRAM (0x%08X) and PHYCONT (0x%08X) (req align=%u size=%u -> size=%u) -- free CDRAM (vitaGL pool)=%u bytes, free PHYCONT (vitaGL pool)=%u bytes",
                    (unsigned) blk, (unsigned) blk2, req_align, req_size, size, freeCdram, (unsigned) vglMemFree(VGL_MEM_SLOW));
            return NULL;
        }
        l_warn("video: CDRAM alloc failed (0x%08X, free CDRAM=%u bytes) -- fell back to PHYCONT for this frame buffer",
               (unsigned) blk, freeCdram);
        blk = blk2;
        usedType = SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW;
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
    l_info("video: texture memblock ok (%s) (req align=%u size=%u -> size=%u) base=%p uid=0x%08X gxm_map=0x%08X",
           usedType == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW ? "CDRAM" : "PHYCONT",
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

/** @brief Packs converted pixels straight to RGB565, and backs the
 *         hand-written NEON vectorization used by yuv420p_to_rgb565() below.
 *  @note See docs/comments/video.cpp.md#rgb565-packing-and-neon-vectorization-rationale
 */
static inline void store_rgb565_8(unsigned short *dst, uint8x8_t r, uint8x8_t g, uint8x8_t b) {
    uint16x8_t rw = vmovl_u8(r);
    uint16x8_t gw = vmovl_u8(g);
    uint16x8_t bw = vmovl_u8(b);
    uint16x8_t rr = vshlq_n_u16(vandq_u16(rw, vdupq_n_u16(0xF8)), 8);
    uint16x8_t gg = vshlq_n_u16(vandq_u16(gw, vdupq_n_u16(0xFC)), 3);
    uint16x8_t bb = vshrq_n_u16(bw, 3);
    vst1q_u16((uint16_t *) dst, vorrq_u16(vorrq_u16(rr, gg), bb));
}

static void yuv420p_to_rgb565(const unsigned char *src, unsigned w, unsigned h, unsigned short *dst) {
    init_yuv_tables();
    const unsigned char *yp = src;
    const unsigned char *uvp = src + (size_t) w * h;
    //! @see docs/comments/video.cpp.md#yuv420p_to_rgb565--chroma-reuse-across-both-rows
    for (unsigned y = 0; y < h; y += 2) {
        const unsigned char *yrow0 = yp + (size_t) y * w;
        const unsigned char *yrow1 = yrow0 + w;
        const unsigned char *uvrow = uvp + (size_t) (y / 2) * w;
        unsigned short *drow0 = dst + (size_t) y * w;
        unsigned short *drow1 = drow0 + w;

        unsigned x = 0;
        for (; x + 16 <= w; x += 16) {
            uint8x8x2_t uv = vld2_u8(uvrow + x); // 8 pairs -> uv.val[0]=U[8], uv.val[1]=V[8]
            int16x8_t Uc = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(uv.val[0])), vdupq_n_s16(128));
            int16x8_t Vc = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(uv.val[1])), vdupq_n_s16(128));

            int32x4_t Uc_lo = vmovl_s16(vget_low_s16(Uc));
            int32x4_t Uc_hi = vmovl_s16(vget_high_s16(Uc));
            int32x4_t Vc_lo = vmovl_s16(vget_low_s16(Vc));
            int32x4_t Vc_hi = vmovl_s16(vget_high_s16(Vc));

            int16x4_t r_add_lo = vmovn_s32(vshrq_n_s32(vmulq_n_s32(Vc_lo, 91881), 16));
            int16x4_t r_add_hi = vmovn_s32(vshrq_n_s32(vmulq_n_s32(Vc_hi, 91881), 16));

            int32x4_t cu_g_lo = vshrq_n_s32(vmulq_n_s32(Uc_lo, 22554), 16);
            int32x4_t cu_g_hi = vshrq_n_s32(vmulq_n_s32(Uc_hi, 22554), 16);
            int32x4_t cv_g_lo = vshrq_n_s32(vmulq_n_s32(Vc_lo, 46802), 16);
            int32x4_t cv_g_hi = vshrq_n_s32(vmulq_n_s32(Vc_hi, 46802), 16);
            int16x4_t g_add_lo = vneg_s16(vmovn_s32(vaddq_s32(cu_g_lo, cv_g_lo)));
            int16x4_t g_add_hi = vneg_s16(vmovn_s32(vaddq_s32(cu_g_hi, cv_g_hi)));

            int16x4_t b_add_lo = vmovn_s32(vshrq_n_s32(vmulq_n_s32(Uc_lo, 116130), 16));
            int16x4_t b_add_hi = vmovn_s32(vshrq_n_s32(vmulq_n_s32(Uc_hi, 116130), 16));

            // One add-term per chroma pair so far (8 lanes); duplicate each
            // across the 2 luma columns it covers (vzipq of a vector with
            // itself: lane i appears at 2i and 2i+1 across val[0]/val[1]),
            // matching the scalar loop's r_add/g_add/b_add reuse for x/x+1.
            int16x8_t r_add8 = vcombine_s16(r_add_lo, r_add_hi);
            int16x8_t g_add8 = vcombine_s16(g_add_lo, g_add_hi);
            int16x8_t b_add8 = vcombine_s16(b_add_lo, b_add_hi);
            int16x8x2_t r_dup = vzipq_s16(r_add8, r_add8);
            int16x8x2_t g_dup = vzipq_s16(g_add8, g_add8);
            int16x8x2_t b_dup = vzipq_s16(b_add8, b_add8);

            for (int half = 0; half < 2; half++) {
                const unsigned char *yr0 = yrow0 + x + half * 8;
                const unsigned char *yr1 = yrow1 + x + half * 8;
                int16x8_t r_add = half == 0 ? r_dup.val[0] : r_dup.val[1];
                int16x8_t g_add = half == 0 ? g_dup.val[0] : g_dup.val[1];
                int16x8_t b_add = half == 0 ? b_dup.val[0] : b_dup.val[1];

                int16x8_t Y0 = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(yr0)));
                int16x8_t Y1 = vreinterpretq_s16_u16(vmovl_u8(vld1_u8(yr1)));

                uint8x8_t r0 = vqmovun_s16(vaddq_s16(Y0, r_add));
                uint8x8_t g0 = vqmovun_s16(vaddq_s16(Y0, g_add));
                uint8x8_t b0 = vqmovun_s16(vaddq_s16(Y0, b_add));
                uint8x8_t r1 = vqmovun_s16(vaddq_s16(Y1, r_add));
                uint8x8_t g1 = vqmovun_s16(vaddq_s16(Y1, g_add));
                uint8x8_t b1 = vqmovun_s16(vaddq_s16(Y1, b_add));

                store_rgb565_8(drow0 + x + half * 8, r0, g0, b0);
                store_rgb565_8(drow1 + x + half * 8, r1, g1, b1);
            }
        }

        for (; x < w; x += 2) {
            unsigned char U = uvrow[x + 0];
            unsigned char V = uvrow[x + 1];

            int r_add = CV_R[V];
            int g_add = -(CU_G[U] + CV_G[V]);
            int b_add = CU_B[U];

            int Y00 = yrow0[x];
            unsigned char r00 = CLIP(Y00 + r_add), g00 = CLIP(Y00 + g_add), b00 = CLIP(Y00 + b_add);
            drow0[x] = (unsigned short) (((r00 & 0xF8) << 8) | ((g00 & 0xFC) << 3) | (b00 >> 3));

            int Y01 = yrow0[x+1];
            unsigned char r01 = CLIP(Y01 + r_add), g01 = CLIP(Y01 + g_add), b01 = CLIP(Y01 + b_add);
            drow0[x+1] = (unsigned short) (((r01 & 0xF8) << 8) | ((g01 & 0xFC) << 3) | (b01 >> 3));

            int Y10 = yrow1[x];
            unsigned char r10 = CLIP(Y10 + r_add), g10 = CLIP(Y10 + g_add), b10 = CLIP(Y10 + b_add);
            drow1[x] = (unsigned short) (((r10 & 0xF8) << 8) | ((g10 & 0xFC) << 3) | (b10 >> 3));

            int Y11 = yrow1[x+1];
            unsigned char r11 = CLIP(Y11 + r_add), g11 = CLIP(Y11 + g_add), b11 = CLIP(Y11 + b_add);
            drow1[x+1] = (unsigned short) (((r11 & 0xF8) << 8) | ((g11 & 0xFC) << 3) | (b11 >> 3));
        }
    }
}

// --- fullscreen quad draw ---

static void draw_video_frame(const unsigned short *rgb565, unsigned w, unsigned h) {
    //! @see docs/comments/video.cpp.md#draw_video_frame--texture-storage-allocated-once-per-resolution
    if (!gVideoTex || gVideoTexW != w || gVideoTexH != h) {
        if (!gVideoTex) glGenTextures(1, &gVideoTex);
        glBindTexture(GL_TEXTURE_2D, gVideoTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, (GLsizei) w, (GLsizei) h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
        gVideoTexW = w;
        gVideoTexH = h;
    }
    glBindTexture(GL_TEXTURE_2D, gVideoTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei) w, (GLsizei) h, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, rgb565);

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

// --- cutscene audio: dedicated output thread ---
//! @see docs/comments/video.cpp.md#cutscene-audio--dedicated-output-thread
static pthread_mutex_t gCutAudioLock = PTHREAD_MUTEX_INITIALIZER;
static unsigned char *gCutAudioBuf[2] = { NULL, NULL };
static unsigned gCutAudioBufCap = 0;
static unsigned gCutAudioLen[2] = { 0, 0 }; // >0 = slot has audio ready to play
static int gCutAudioWriteSlot = 0;
static int gCutAudioPort = -1;
static volatile bool gCutAudioQuit = false;

static int cutscene_audio_thread(SceSize args, void *argp) {
    (void) args; (void) argp;
    int slot = 0;
    for (;;) {
        pthread_mutex_lock(&gCutAudioLock);
        unsigned len = gCutAudioLen[slot];
        bool quit = gCutAudioQuit;
        pthread_mutex_unlock(&gCutAudioLock);

        if (len == 0) {
            if (quit)
                break;
            sceKernelDelayThread(500); // ~1/40th of a block period (1024 frames @ 48kHz = ~21ms)
            continue;
        }

        if (gCutAudioPort >= 0)
            sceAudioOutOutput(gCutAudioPort, gCutAudioBuf[slot]);
        pthread_mutex_lock(&gCutAudioLock);
        gCutAudioLen[slot] = 0;
        pthread_mutex_unlock(&gCutAudioLock);
        slot ^= 1;
    }
    return 0;
}

/** @brief Hands one decoded audio block to the cutscene audio thread via a
 *         double-buffered slot, growing the buffers on demand; drops the
 *         block on allocation failure instead of crashing. */
static void cutscene_audio_submit(const void *pData, unsigned bytes) {
    if (bytes > gCutAudioBufCap) {
        free(gCutAudioBuf[0]);
        free(gCutAudioBuf[1]);
        gCutAudioBuf[0] = (unsigned char *) malloc(bytes);
        gCutAudioBuf[1] = (unsigned char *) malloc(bytes);
        gCutAudioBufCap = (gCutAudioBuf[0] && gCutAudioBuf[1]) ? bytes : 0;
    }
    if (!gCutAudioBuf[0] || !gCutAudioBuf[1] || gCutAudioBufCap < bytes)
        return;

    for (;;) {
        pthread_mutex_lock(&gCutAudioLock);
        bool free_slot = gCutAudioLen[gCutAudioWriteSlot] == 0;
        if (free_slot) {
            memcpy(gCutAudioBuf[gCutAudioWriteSlot], pData, bytes);
            gCutAudioLen[gCutAudioWriteSlot] = bytes;
        }
        pthread_mutex_unlock(&gCutAudioLock);
        if (free_slot)
            break;
        sceKernelDelayThread(500);
    }
    gCutAudioWriteSlot ^= 1;
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
        gVideoTexW = 0;
        gVideoTexH = 0;
    }
    free(gRgbBuf);
    gRgbBuf = NULL;
    gRgbBufCap = 0;
    free(gYuvScratch);
    gYuvScratch = NULL;
    gYuvScratchCap = 0;
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
    //! @see docs/comments/video.cpp.md#video_play--basepriority-0xa0
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

    //! @see docs/comments/video.cpp.md#video_play--dedicated-cutscene-audio-port-design
    int audioPort = -1;
    int audioChannels = 0;
    unsigned audioFrameLen = 0; // frames/channel per sceAudioOutOutput() call, derived below
    SceUID cutAudioThreadUid = -1;

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
    uint64_t play_start_time = sceKernelGetProcessTimeWide();

    int frame_count = 0;
    int video_frames = 0, audio_frames = 0;
    bool audioOpenAttempted = false;

    //! @see docs/comments/video.cpp.md#video_play--per-stage-timing-investigation-log_000050
    uint64_t t_yuv_total = 0, t_draw_total = 0, t_audioout_total = 0, t_cdram_copy_total = 0;

    if (!sceAvPlayerIsActive(handle)) {
        l_warn("video: timed out waiting for video decoder to become active (%s)", path.c_str());
    }

    //! @see docs/comments/video.cpp.md#video_play--gl-state-saverestore-around-playback
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
            unsigned need = w * h * sizeof(unsigned short);
            if (need > gRgbBufCap) {
                free(gRgbBuf);
                gRgbBuf = (unsigned short *) malloc(need);
                gRgbBufCap = gRgbBuf ? need : 0;
            }
            //! @see docs/comments/video.cpp.md#video_play--cdram-copy-before-yuv-conversion-log_000051
            unsigned yuvNeed = w * h + w * h / 2; // NV12: Y plane + half-res interleaved UV
            if (yuvNeed > gYuvScratchCap) {
                free(gYuvScratch);
                gYuvScratch = (unsigned char *) malloc(yuvNeed);
                gYuvScratchCap = gYuvScratch ? yuvNeed : 0;
            }
            if (gRgbBuf && gRgbBufCap >= need && gYuvScratch && gYuvScratchCap >= yuvNeed) {
                uint64_t t0 = sceKernelGetProcessTimeWide();
                memcpy(gYuvScratch, video.pData, yuvNeed);
                uint64_t t1 = sceKernelGetProcessTimeWide();
                yuv420p_to_rgb565(gYuvScratch, w, h, gRgbBuf);
                uint64_t t2 = sceKernelGetProcessTimeWide();
                draw_video_frame(gRgbBuf, w, h);
                uint64_t t3 = sceKernelGetProcessTimeWide();
                t_cdram_copy_total += (t1 - t0);
                t_yuv_total += (t2 - t1);
                t_draw_total += (t3 - t2);
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
                //! @see docs/comments/video.cpp.md#video_play--deriving-audioframelen-from-the-real-frame-size
                audioFrameLen = audio.details.audio.size / (audioChannels * sizeof(int16_t));
                l_info("video: cutscene audio port: %u frames/channel (size=%u bytes, ch=%u)",
                       audioFrameLen, (unsigned) audio.details.audio.size, audioChannels);
                //! @see docs/comments/video.cpp.md#video_play--voice-port-type-vs-main
                audioPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_VOICE, audioFrameLen,
                                                (int) audio.details.audio.sampleRate, mode);
                if (audioPort < 0) {
                    //! @see docs/comments/video.cpp.md#video_play--avoiding-the-sceaudiooutopenport-retry-storm
                    l_warn("video: sceAudioOutOpenPort for cutscene audio failed (0x%08X) -- cutscene audio disabled",
                           (unsigned) audioPort);
                } else {
                    gCutAudioPort = audioPort;
                    gCutAudioWriteSlot = 0;
                    gCutAudioLen[0] = 0;
                    gCutAudioLen[1] = 0;
                    gCutAudioQuit = false;
                    cutAudioThreadUid = sceKernelCreateThread("cutscene audio out", cutscene_audio_thread,
                                                               0x10000100, 0x4000, 0, 0, NULL);
                    if (cutAudioThreadUid >= 0) {
                        sceKernelStartThread(cutAudioThreadUid, 0, NULL);
                    } else {
                        l_warn("video: cutscene audio thread creation failed (0x%08X) -- cutscene audio disabled",
                               (unsigned) cutAudioThreadUid);
                        sceAudioOutReleasePort(audioPort);
                        audioPort = -1;
                        gCutAudioPort = -1;
                    }
                }
            }
            if (audioPort >= 0) {
                //! @see docs/comments/video.cpp.md#video_play--cutscene-audio-hand-off-timing-note
                uint64_t t0 = sceKernelGetProcessTimeWide();
                cutscene_audio_submit(audio.pData, (unsigned) audio.details.audio.size);
                uint64_t t1 = sceKernelGetProcessTimeWide();
                t_audioout_total += (t1 - t0);
            }
        }

        frame_count++;
        if (frame_count == 1) {
            l_info("video: successfully completed first loop iteration!");
        }

        sceKernelDelayThread(1000); // avoid a tight spin when neither frame type is ready yet
    }
    
    uint64_t play_end_time = sceKernelGetProcessTimeWide();
    double elapsed_sec = (double) (play_end_time - play_start_time) / 1000000.0;
    double avg_fps = elapsed_sec > 0.0 ? (double) video_frames / elapsed_sec : 0.0;
    l_info("video: loop exited! active=%d, iterations=%d, video_frames=%d, audio_frames=%d, elapsed=%.2fs, avg_fps=%.1f",
           sceAvPlayerIsActive(handle), frame_count, video_frames, audio_frames, elapsed_sec, avg_fps);
    l_info("video: stage totals -- cdram_copy=%.2fs, yuv_convert=%.2fs, gl_upload_draw=%.2fs, audio_output_block=%.2fs (of %.2fs elapsed)",
           (double) t_cdram_copy_total / 1000000.0, (double) t_yuv_total / 1000000.0,
           (double) t_draw_total / 1000000.0, (double) t_audioout_total / 1000000.0, elapsed_sec);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    if (savedBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (savedDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);

    if (cutAudioThreadUid >= 0) {
        pthread_mutex_lock(&gCutAudioLock);
        gCutAudioQuit = true;
        pthread_mutex_unlock(&gCutAudioLock);
        sceKernelWaitThreadEnd(cutAudioThreadUid, NULL, NULL);
        sceKernelDeleteThread(cutAudioThreadUid);
    }
    gCutAudioPort = -1;

    if (audioPort >= 0)
        sceAudioOutReleasePort(audioPort);

    sceAvPlayerStop(handle);
    sceAvPlayerClose(handle);
    audio_resume_bgm_after_video();

    l_info("video: %s (%s)", skipped ? "skipped" : "finished", path.c_str());
}
