# glutil.c — Developer Comment Documentation

Migrated from inline comments in [`source/utils/glutil.c`](../../source/utils/glutil.c).

---

## Table of Contents

1. [CDRAM Threshold for SceAvPlayer Video Decoding](#cdram-threshold-for-sceavplayer-video-decoding)
2. [Vita3K MSAA Workaround](#vita3k-msaa-workaround)

---

## CDRAM Threshold for SceAvPlayer Video Decoding

**Location:** inside `gl_init()`, immediately before the `vglInitWithCustomThreshold(...)` calls.

The CDRAM threshold argument is set to 32MB, which is left unclaimed by vitaGL's own memory pool so that `SceAvPlayer`'s video decoder frame memory can be allocated as dedicated CDRAM kernel memblocks (`source/video.cpp`'s `av_alloc_texture`, which replicates OpenFMV's proven allocator). With plain `vglInitExtended`, vitaGL claims the whole CDRAM budget and those memblock allocations would have nothing left to come from — this is the same reason OpenFMV's own init passes a 32MB CDRAM threshold.

Game impact: the texture pool shrinks by 32MB, which still leaves ample headroom (roughly 70MB of the pool was free at video-playback time in `log_000030`, menus included).

---

## Vita3K MSAA Workaround

**Location:** inside `gl_init()`, in the `#ifdef EMULATOR_BUILD` branch, immediately before `vglInitWithCustomThreshold(...)`.

Under Vita3K, requesting 4x MSAA here has been observed to make vitaGL's internal init retry `sceGxmCreateContext` a second time, which then fails with `SCE_GXM_ERROR_ALREADY_INITIALIZED` and crashes, since vitaGL doesn't check that return value. Disabling MSAA (`SCE_GXM_MULTISAMPLE_NONE` instead of `SCE_GXM_MULTISAMPLE_4X`) avoids the retry. Real hardware does not hit this and keeps 4x MSAA.
