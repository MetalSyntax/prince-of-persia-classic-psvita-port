# video.cpp – Developer Comment Reference

Extracted developer/technical comments from
[`source/video.cpp`](../../source/video.cpp). These comments were already
written in English in the source, so no translation was needed — only
rewording into structured documentation prose, preserving every technical
fact (function names, addresses, log IDs, crash IDs, error codes, version
numbers).

---

## Table of Contents

1. [File-Level Design Overview](#file-level-design-overview)
2. [GPU-Shader NV12->RGB Path (Reverted)](#gpu-shader-nv12-rgb-path-reverted)
3. [SceAvPlayer File I/O – Restored, with Full Visibility](#sceavplayer-file-io--restored-with-full-visibility)
4. [av_file_read – Capped Per-Read Logging](#av_file_read--capped-per-read-logging)
5. [SceAvPlayer Event Callback – Diagnostic Channel](#sceavplayer-event-callback--diagnostic-channel)
6. [SceAvPlayer Memory – General vs. Texture Allocators](#sceavplayer-memory--general-vs-texture-allocators)
7. [Frame-Buffer Allocation – Dedicated Memblock (OpenFMV Pattern)](#frame-buffer-allocation--dedicated-memblock-openfmv-pattern)
8. [av_alloc_texture – CDRAM Failure and PHYCONT Fallback](#av_alloc_texture--cdram-failure-and-phycont-fallback)
9. [RGB565 Packing and NEON Vectorization Rationale](#rgb565-packing-and-neon-vectorization-rationale)
10. [yuv420p_to_rgb565 – Chroma Reuse Across Both Rows](#yuv420p_to_rgb565--chroma-reuse-across-both-rows)
11. [draw_video_frame – Texture Storage Allocated Once Per Resolution](#draw_video_frame--texture-storage-allocated-once-per-resolution)
12. [Cutscene Audio – Dedicated Output Thread](#cutscene-audio--dedicated-output-thread)
13. [video_play – basePriority 0xA0](#video_play--basepriority-0xa0)
14. [video_play – Dedicated Cutscene Audio Port Design](#video_play--dedicated-cutscene-audio-port-design)
15. [video_play – Per-Stage Timing Investigation (log_000050)](#video_play--per-stage-timing-investigation-log_000050)
16. [video_play – GL State Save/Restore Around Playback](#video_play--gl-state-saverestore-around-playback)
17. [video_play – CDRAM Copy Before YUV Conversion (log_000051)](#video_play--cdram-copy-before-yuv-conversion-log_000051)
18. [video_play – Deriving audioFrameLen from the Real Frame Size](#video_play--deriving-audioframelen-from-the-real-frame-size)
19. [video_play – VOICE Port Type vs. MAIN](#video_play--voice-port-type-vs-main)
20. [video_play – Avoiding the sceAudioOutOpenPort Retry Storm](#video_play--avoiding-the-sceaudiooutopenport-retry-storm)
21. [video_play – Cutscene Audio Hand-off Timing Note](#video_play--cutscene-audio-hand-off-timing-note)

---

## File-Level Design Overview

**Location:** File header, top of `source/video.cpp` (original lines 1–21).

This file implements cutscene playback via the Vita's native `SceAvPlayer`,
wired into `Cocos2dxActivity_playVideo` (see `java.c`). The original Android
engine has no native video codec of its own on this port, so this file fully
replaces that path instead of trying to bridge to anything Android-side.

The design follows the same discipline established for audio (see
`Docs/Fixes_Log.md` #10/#11):

- **File I/O stays inside SceAvPlayer** (plain path on `ux0:`) — no stdio of
  this project's own anywhere near it. An earlier revision wired a
  `SceAvPlayerFileReplacement` over `sceIo`; it was dropped as unnecessary for
  standalone files (see [SceAvPlayer File I/O – Restored, with Full
  Visibility](#sceavplayer-file-io--restored-with-full-visibility) for why it
  was later reinstated anyway, for diagnostics).
- **Never hangs and never leaves the screen stuck:** `video_play()` always
  returns — on natural end, user skip, or any failure to open/init — so the
  caller can unconditionally fire `onVideoCompleted()` afterwards. That
  callback is what unblocks `VideoLayer` (see `plan_portabilidad.md` §9.20);
  skipping it is exactly the hang this design guards against.
- **Frame data format:** confirmed on real hardware to be NV12 (one Y plane,
  followed by an interleaved U/V plane, each subsampled 2×2) — the vitasdk
  header does not spell this out. An earlier revision assumed fully-planar
  I420 (separate U and V planes), which produced a green tint (see
  `Docs/Fixes_Log.md` #17).

---

## GPU-Shader NV12->RGB Path (Reverted)

**Location:** Standalone comment block between the global state variables and
the `SceAvPlayer` file I/O section (original lines 55–65).

A GPU-shader NV12→RGB path (upload the raw Y/UV planes and do the color math
in a fragment shader instead of on the CPU) was tried here and reverted: it
crashed on real hardware on the very first cutscene, with the log cutting off
mid-compile — between `"video: playing"` and `"video: loop starting"`, right
where the new shader-compile call sat (no engine code normally runs there).

The root cause was not confirmed further, but this project's own
`porting_tools/translate_shaders.py` already carries a warning that vitaGL's
on-device GLSL pipeline "isn't reliable enough for this game's shaders." That
warning was taken at face value rather than retried blindly. **Do not
reintroduce a video-specific GLSL program without testing it on hardware
first.**

The fixed-function alternative that replaced it is documented in
[RGB565 Packing and NEON Vectorization Rationale](#rgb565-packing-and-neon-vectorization-rationale).

---

## SceAvPlayer File I/O – Restored, with Full Visibility

**Location:** Section header above `struct AvFileCtx` (original lines 67–75).

`SceAvPlayer`'s internal file I/O was in use for a time while the player kept
dying with zero decoded frames and no error surfaced anywhere (`log_000028`)
— with internal I/O, the file access is a black box. Routing I/O back through
`sceIo` (via `av_file_open`/`av_file_close`/`av_file_read`/`av_file_size`)
does two things at once: it bypasses anything unusual in the player's
internal FIOS2 usage (this process initializes FIOS with its own overlay at
boot — see `lib/fios/fios.c`), and it lets the log show exactly how far the
player got into the file before giving up.

---

## av_file_read – Capped Per-Read Logging

**Location:** Inside `av_file_read()`, before the `if (ctx->read_calls <= 5 ||
n < 0)` check (original lines 105–107).

The first few reads and any failure tell the whole story that's needed for
diagnosis; logging every single read beyond that is exactly the
high-frequency-logging trap documented in `Docs/Fixes_Log.md` #12, so
per-read logging is capped at the first 5 calls (plus any failing call).

---

## SceAvPlayer Event Callback – Diagnostic Channel

**Location:** Section header above `av_event_name()` (original lines 122–126).

Every `SceAvPlayer` state transition — and, crucially, warning/error codes —
arrives through this callback. Without it, a playback abort is silent:
`sceAvPlayerIsActive()` just flips to false with no indication why. This
callback is what finally reveals the reason.

---

## SceAvPlayer Memory – General vs. Texture Allocators

**Location:** Comment block above `#define AV_FB_ALIGNMENT` (original lines
151–170).

This is the allocator pattern proven on real hardware by the so-loader ports
that already ship working video (`gtasa_vita`'s `movie.c` and its many
descendants). Two **different** allocators are registered with
`SceAvPlayerInitData`, and the difference matters:

- **`allocate`/`deallocate` (general):** plain `memalign`/`free` from the
  newlib heap. `SceAvPlayer` makes many small internal allocations (demuxer
  state, stream read buffers, queues) through this pair. A previous revision
  of this file backed *every one* of these with its own
  `sceKernelAllocMemBlock`, which exhausts the process's memblock limit
  within the player's startup burst — at which point an internal allocation
  returns `NULL` and the player silently transitions to inactive right after
  activating. That is exactly the previously observed symptom: "active=1,
  then dead within one loop iteration, frames=0."
- **`allocateTexture`/`deallocateTexture`:** the hardware AVC decoder writes
  decoded frames here, which requires physically contiguous memory
  (PHYCONT), not ordinary heap. `vglAlloc(VGL_MEM_SLOW)` is vitaGL's PHYCONT
  pool. Only a handful of these allocations ever happen (the
  `numOutputVideoFrameBuffers` frame buffers), so pool pressure is not a
  concern here. A 256KB minimum alignment is used, per the same reference
  ports.

---

## Frame-Buffer Allocation – Dedicated Memblock (OpenFMV Pattern)

**Location:** Comment block above `#define AV_TEX_MAX_BLOCKS` / `gAvTexBlocks`
(original lines 187–198).

Frame-buffer allocations replicate OpenFMV's `gpu_alloc` exactly: a
**dedicated** kernel memblock per allocation (CDRAM, kernel-guaranteed
alignment via `SceKernelAllocMemBlockOpt`, mapped with `sceGxmMapMemory`),
rather than a slice of vitaGL's single big pre-mapped pool.

This was the last structural difference left standing after `log_000029`/
`log_000030`: pool-served buffers — whether PHYCONT or CDRAM, correctly
aligned, every allocation succeeding — still ended in a silent `STATE_STOP`
with zero frames. The plausible mechanism: the AVC decoder identifies/pins
the memblock that *owns* the address it's given (in the style of
`sceKernelFindMemBlockByAddr`); an address in the middle of vitaGL's giant
pool block resolves to a block with the wrong size/owner/flags and gets
rejected — silently, exactly as observed. A dedicated block per allocation is
exactly what OpenFMV ships with on real hardware.

---

## av_alloc_texture – CDRAM Failure and PHYCONT Fallback

**Location:** Inside `av_alloc_texture()`, in the `if (blk < 0)` branch
(original lines 216–225).

`log_000046`/`log_000047` (real hardware, freshly booted, first cutscene of
the session) showed this CDRAM allocation failing with a genuine
`SCE_KERNEL_ERROR_NO_MEMORY` (`0x80020005`) every single time — not just after
heavy menu navigation. `gl_init()`'s 32MB CDRAM threshold (`utils/glutil.c`)
isn't leaving enough room in practice on this hardware/build.

Rather than give up (which would mean no cutscene ever plays), the code
retries once from the PHYCONT partition — a separate physical pool from
CDRAM, not competing with the same budget — before failing for real.

---

## RGB565 Packing and NEON Vectorization Rationale

**Location:** Comment block above `store_rgb565_8()` (original lines
301–321).

Converted pixels are packed straight to RGB565 (2 bytes/pixel) instead of
RGBA8888 (4 bytes/pixel): this halves the memory writes in the conversion
itself, and halves the bytes `glTexSubImage2D` has to push to the GPU every
frame afterwards. This is the safe, fixed-function-only alternative to the
GPU-shader NV12 path (see
[GPU-Shader NV12->RGB Path (Reverted)](#gpu-shader-nv12-rgb-path-reverted)
above) — no new GL entry points, just a smaller pixel format on the exact
same upload/draw path already proven working on hardware since v01.18.

`log_000051` showed that even after the RGB565 halving, this conversion was
still the single biggest cost in the frame (`yuv_convert` dominated the
profile). The Vita's CPU (Cortex-A9) is a NEON target — already confirmed in
use by this project via the linked `mathneon` library (`CMakeLists.txt`) —
so the inner loop was hand-vectorized: 8 chroma pairs (16 luma columns, 2
rows = 32 pixels) are processed per iteration instead of one 2×2 block at a
time.

There is no gather instruction on this CPU, so the fixed-point coefficients
(the same shift-by-16 constants as `CV_R`/`CU_G`/`CV_G`/`CU_B` used by the
scalar path) are computed directly in NEON registers rather than looked up
from the scalar LUT. The per-term shift-then-add order is kept identical to
the scalar version so the two paths agree bit-for-bit, not just "close
enough." Widths that are not a multiple of 16 fall back to the original
scalar loop for the last few columns.

---

## yuv420p_to_rgb565 – Chroma Reuse Across Both Rows

**Location:** Inside `yuv420p_to_rgb565()`, above the main row loop (original
lines 336–341).

Each chroma sample is shared by a 2×2 luma block (4:2:0 subsampling), but an
earlier version of this function processed one row at a time — it looked up
and multiplied the *same* U/V pair twice (once for row `y`, once for row
`y+1`, since `y/2 == (y+1)/2` for even `y`) before ever using it. The current
version processes both rows of the block together, so each chroma lookup is
done once and applied to all 4 pixels it actually covers.

---

## draw_video_frame – Texture Storage Allocated Once Per Resolution

**Location:** Inside `draw_video_frame()`, above the texture (re)allocation
check (original lines 436–441).

`glTexImage2D` reallocates the texture's VRAM storage. Calling it every
single frame — as an earlier version of this function did — forces vitaGL to
free and re-request GPU memory roughly 30 times a second, which is a known,
large source of stalling on this driver. A cutscene's resolution never
changes mid-playback, so the storage is allocated once per `(w, h)` and only
new pixels are uploaded into it afterward (via `glTexSubImage2D`).

---

## Cutscene Audio – Dedicated Output Thread

**Location:** Section header above `gCutAudioLock` and the cutscene-audio
globals (original lines 498–528).

`log_000051` showed `sceAudioOutOutput()` alone blocking for 1.13s of a
10.44s cutscene (`audio_output_block`), all inside the *same* loop that pays
for the YUV conversion above — every frame the two costs queue up back to
back on one thread, so a slow video frame delays the next audio block too
(and vice versa).

The game's own BGM mixer (`audio.cpp`) already proves the fix for this exact
shape of problem: give the blocking `sceAudioOut` call its own thread. Here
the render loop hands a decoded audio block to a small double-buffered
producer/consumer pair (the same shape as `audio.cpp`'s `out[2]`/`bufId`)
instead of calling `sceAudioOutOutput()` directly — the render loop only
waits long enough for the *previous* block in that slot to finish playing,
never for the current one, so audio timing is no longer coupled to how long
a given frame's conversion took.

**`psp2dmp 1785292479`** (real hardware, first cutscene played after this
change went in): the "cutscene audio out" thread crashed on its very first
wait, with PC/LR both landing inside `pte_osSemaphoreCancellablePend`
(confirmed via `arm-vita-eabi-nm` on `libpthread.a`: `pthread_cond_wait` calls
exactly that function).

A first version of this file used a `pthread_cond_t` alongside the mutex for
the producer/consumer handshake. `PTHREAD_COND_INITIALIZER` is a static
sentinel (`(pthread_cond_t)-1`, see `sys/_pthreadtypes.h`), and this vitasdk
pthread port apparently doesn't lazily turn that sentinel into a real
semaphore the way it does for a statically-initialized mutex (`audio.cpp`'s
`gLock` uses that same static-mutex pattern and has never crashed in this
project's history). Rather than gamble on an explicit `pthread_cond_init()`
call fixing an otherwise never-exercised code path on this platform, the
handshake uses **only** the mutex (proven) plus a short
`sceKernelDelayThread` poll — the exact same wait style already proven on
hardware by this file's own `SceAvPlayer` polling loop.

---

## video_play – basePriority 0xA0

**Location:** Inside `video_play()`, setting `init.basePriority` (original
lines 651–655).

`basePriority` is set to `0xA0`, the value the known-working reference ports
use. The previous value, `0x10000100`
(`SCE_KERNEL_DEFAULT_PRIORITY_USER`), is a special sentinel — `SceAvPlayer`
derives its internal thread priorities by offsetting from this base value,
and offsets computed from that sentinel are not valid priorities.

---

## video_play – Dedicated Cutscene Audio Port Design

**Location:** Inside `video_play()`, above the `audioPort` local variable
declaration (original lines 680–683).

Video audio goes through its own dedicated `sceAudioOut` port, opened lazily
once the first audio frame is seen (channel count and sample rate aren't
known before that). It is kept fully separate from the game's own BGM mixer
(`audio.cpp`) so neither one has to know about the other.

---

## video_play – Per-Stage Timing Investigation (log_000050)

**Location:** Inside `video_play()`, above the `t_yuv_total`/`t_draw_total`/
`t_audioout_total`/`t_cdram_copy_total` counters (original lines 712–717).

`log_000050` (real hardware) showed an average of 9.2 FPS — no better than
before the RGB565 change. `video.pData` was confirmed, by matching addresses
in the log, to sit inside the CDRAM memblock that `av_alloc_texture` hands
back, and `sceAudioOutOutput()` (used at the time) is itself a known-blocking
call (roughly 21ms/call at 1024 frames/48000Hz). Either cost could dominate
the frame, so both are measured explicitly as separate stage totals instead
of guessing which one to optimize next.

---

## video_play – GL State Save/Restore Around Playback

**Location:** Inside `video_play()`, above `savedViewport` and the matrix
push calls, before the playback loop (original lines 724–728).

`draw_video_frame()` overwrites the projection/modelview matrices and
disables blend/depth-test for its fullscreen quad. These are saved here and
restored right after the playback loop exits, so cocos2d's own rendering
isn't left corrupted once the cutscene ends. This bug was previously
invisible because the decoder always died before a single frame was ever
drawn (see `Docs/Fixes_Log.md` #17 for the mirrored-screen/black-box-sprite
symptoms this caused once video actually started rendering).

---

## video_play – CDRAM Copy Before YUV Conversion (log_000051)

**Location:** Inside the video-frame branch of `video_play()`'s main loop,
above the `yuvNeed` buffer sizing (original lines 762–771).

`log_000051` showed `yuv_convert` alone eating 8.36s of a 10.44s cutscene.
`video.pData` sits in the CDRAM memblock `av_alloc_texture` hands back
(confirmed by matching addresses in the log), and CPU reads from CDRAM/VRAM
are far slower than RAM on this hardware — the per-pixel access pattern in
the conversion (mixed reads and arithmetic) can't hide that latency.

The fix is a single sequential `memcpy` that drains the CDRAM source in one
burst-friendly pass into `gYuvScratch`; all the per-pixel math then reads
from that normal RAM copy instead. This copy is measured as its own stage
(`t_cdram_copy_total`) so the next log proves whether it actually mattered
rather than assuming it did.

---

## video_play – Deriving audioFrameLen from the Real Frame Size

**Location:** Inside `video_play()`'s audio-frame branch, above the
`audioFrameLen` assignment (original lines 802–812).

`sceAudioOutOutput()` takes no length argument at all — it always outputs
exactly the port's configured `len` (frames/channel) from whatever buffer
it's given. A prior revision hardcoded `len=1024` as "the common AvPlayer
chunk size"; if the decoder's actual chunk size differs — `details.audio.size`
is the real per-frame byte size `SceAvPlayer` reports — every single
`sceAudioOutOutput` call over- or under-reads `audio.pData` by the mismatch.
That is exactly the "choppy/cut" playback reported in `log_000039`, as
opposed to an occasional scheduling hiccup. The fix derives `audioFrameLen`
from the frame's reported size instead of assuming it.

---

## video_play – VOICE Port Type vs. MAIN

**Location:** Inside `video_play()`'s audio-frame branch, above the
`sceAudioOutOpenPort` call (original lines 816–825).

The cutscene audio port is opened as type `SCE_AUDIO_OUT_PORT_TYPE_VOICE`,
not `MAIN`. The vitasdk header spells out that `MAIN` "must be set to
48000Hz" — every 44100Hz cutscene (most of them; only `PoP_V1_1` is 48000Hz)
was hitting exactly that restriction with `MAIN`, confirmed by
`log_000032`/`log_000038`'s error code `0x80260008` =
`SCE_AUDIO_OUT_ERROR_INVALID_SAMPLE_FREQ` — not a port-full/contention error,
as first assumed. `VOICE` has no such restriction, and is a different port
type than the game's own single BGM mixer port (`audio.cpp`), so it can't
collide with it either.

---

## video_play – Avoiding the sceAudioOutOpenPort Retry Storm

**Location:** Inside `video_play()`, in the `if (audioPort < 0)` branch after
a failed `sceAudioOutOpenPort` call (original lines 829–834).

Retrying `sceAudioOutOpenPort` on every audio frame — as a prior revision
did — hammers a real, fairly expensive driver call. `log_000032` showed it
firing more than 400 times in a single cutscene, stalling the entire frame
pump; that retry storm was the actual root cause of the original "video is
slow" symptom. The current code attempts the call only once per cutscene and
fails silently (video keeps playing, just muted) if it doesn't succeed.

---

## video_play – Cutscene Audio Hand-off Timing Note

**Location:** Inside `video_play()`'s audio-frame branch, above the
`cutscene_audio_submit()` call (original lines 857–861).

This code path is now just a hand-off to the dedicated cutscene-audio output
thread (a `memcpy` into a free double-buffer slot) instead of calling the
blocking `sceAudioOutOutput()` directly. The `t_audioout_total` timing stage
should drop to near-zero once this is measured on hardware; if it doesn't,
the two threads are contending for CPU and the fix didn't help as intended.
