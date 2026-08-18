# audio.cpp – Developer Comments

Extracted from `source/audio.cpp`. All meaningful block and inline developer
comments are preserved here verbatim, organized by section.

---

## Design Overview

> **Source lines 1–32** — File-level design rationale block.

```
Cocos2dxMusic / Cocos2dxSound JNI surface implemented on a small
self-contained mixer over sceAudioOut, following the reference ports:
  deadspace-vita drives sceAudioOut from its own output thread
  (loader/android/EAAudioCore.c) and pop2-vita keeps a single C runtime for
  everything.

SoLoud was dropped after three crash rounds: its vendored WavStream casts
SoLoud::File* to FILE* and expects an stb_vorbis compiled with its "file hack"
(soloud_file_hack_on.h) -- our tree compiled stb_vorbis raw, so every BGM load
ended in newlib fseek/ftell on a fake FILE and a branch into garbage
(the whole psp2core family of 2026-07-07, see Docs/Fixes_Log.md #10 and
plan §9.30).
```

### Hard Rules (from debugging)

1. **No stdio anywhere in this file.** Files are read with `sceIo`; decoding
   uses only minimp3's `*_buf` APIs (memory in, memory/callback out — no
   `FILE*` ever touches this file).
2. **No failure path ever executes a pointer.** Failed loads log and return a
   valid dummy handle (never 0 — Cocos2d-x loops on 0, the "infinite jump"
   bug).
3. **Sample data referenced by the mixer thread is only freed after the voices
   using it are silenced under the mixer lock** (no use-after-free).

### MP3 Assets Rationale

Assets are the game's original `.mp3` files (mono or stereo, 22050/32000/44100 Hz,
measured), decoded natively with minimp3 — no offline mp3->ogg transcode step.
That transcode used to run every asset through a second lossy encode on top of
the source mp3's own compression (already as low as 56 kbps/22050 Hz), which is
what made music/SFX sound noticeably worse than the original Android build.

The mixer output runs at 44100 stereo and every voice resamples linearly with
`step = pitch * (src_rate / 44100)`; mono sources are upmixed to stereo by
duplicating the sample to both channels (same treatment stb_vorbis's
forced-stereo mode used to give us for free — minimp3 decodes exactly the
source channel count, so it's done by hand here for BGM streaming and in
`mix_voice()` for one-shot SFX).

---

## MIX_GRAIN Buffer Size

> **Source lines 56–61** — `#define MIX_GRAIN 2048`

Frames per `sceAudioOutOutput` block (~46 ms). Matches the buffer size the
removed SoLoud `vita_homebrew` backend used (`Soloud::init`'s AUTO default is
2048) — doubled from an initial 1024 to give the mixer thread (which does
vorbis decode + resample + N-voice mixing, not just a memcpy) more slack before
the hardware needs the next block, reducing underrun-driven crackle.

See `Docs/Fixes_Log.md #11`.

---

## SOFT_CLIP_THRESHOLD Limiter

> **Source lines 66–73** — `#define SOFT_CLIP_THRESHOLD 0.92f`

Soft-knee limiter onset, as a fraction of full scale. Below this the output is
bit-identical to the mixed input (verified via a host-side bit-exact resampling
test — normal single/dual-voice playback never reaches this band).

Above it, peaks are compressed smoothly toward but never past full scale instead
of hard-clipping, so summing several simultaneously loud voices (BGM + footsteps
+ a sword hit, say) saturates gracefully instead of producing harsh digital
clipping — the reported "distorted" sound.

---

## Engine State Section

> **Source line 76** — `// --- engine state (gLock protects everything the mixer thread reads) ---`

Marks the block of global state (voice table, SFX cache, volume, next-handle
counter) that the mixer thread reads on every mix pass. Any game-side thread
touching this state must acquire `gLock` first — the same lock the mixer
thread holds each time it mixes a block (see
[Mixing — Mixer Thread and gLock](#mixing--mixer-thread-and-glock)).

---

## BGM Streaming State Section

> **Source line 106** — `// --- BGM: streamed decode from the compressed mp3 kept in RAM ---`

Marks the block of global state backing background-music playback: the
compressed mp3 buffer and its `mp3dec_ex_t` decoder (kept open for the
duration of playback and decoded incrementally, rather than decoded to a full
PCM buffer up front), the decoded ring window (`gBgmWin`), and the playback
flags (`gBgmPlaying` / `gBgmPaused` / `gBgmLoop` / `gBgmEnded`). See
[BGM State — gBgmMp3Buf Lifetime](#bgm-state--gbgmmp3buf-lifetime) for the
lifetime rule on the compressed buffer itself.

---

## BGM State — gBgmMp3Buf Lifetime

> **Source line 107** — `static unsigned char *gBgmMp3Buf`

`gBgmMp3Buf` is `malloc`'d and **must outlive `gBgmMp3`** (the
`mp3dec_ex_t` decoder). `mp3dec_ex_open_buf` and `mp3dec_load_buf` require
the raw compressed buffer to remain valid for the lifetime of the decoder
object.

---

## File Loading — sceIo Only

> **Source line 118** — `// --- file loading (sceIo only) ---`

All file I/O in this module uses PSVita-native `sceIo*` calls exclusively.
`stdio` (`fopen`, `fread`, etc.) is strictly forbidden here to avoid the
newlib `FILE*` / SoLoud fake-FILE crash class described in the Design Overview.

The `read_entire_file()` helper additionally guards against unreasonable file
sizes (capped at 32 MB; the largest game `.mp3` is ~1 MB) and returns a
`malloc`'d buffer whose ownership transfers to the decoder.

---

## Mixing — Mixer Thread and gLock

> **Source line 174** — `// --- mixing (mixer thread only, gLock held) ---`

All functions in this section are called exclusively from `mixer_thread` while
`gLock` is held. Game-side threads (JNI surface) must acquire `gLock` before
modifying any state read here.

---

## soft_clip16 — Identity Below Threshold

> **Source lines 176–178** — inline comment above `soft_clip16()`

Identity below `SOFT_CLIP_THRESHOLD` (verified bit-exact against ground truth
for normal single/dual-voice playback); above it, compresses smoothly toward
but never past full scale instead of hard-clipping.

See also: [SOFT_CLIP_THRESHOLD Limiter](#soft_clip_threshold-limiter).

---

## bgm_refill_from_decoder — Mono Upmix

> **Source lines 197–200** — comment above `bgm_refill_from_decoder()`

Reads up to `frames_wanted` frames (bounded by remaining space in `gBgmWin`)
from `gBgmMp3` into `gBgmWin` at offset `gBgmAvail`, upmixing mono source to
stereo by duplicating each sample to both channels.

Returns frames actually appended (0 = decoder has no more samples right now).

---

## bgm_ensure_window — Stream Refill

> **Source lines 223–224** — comment above `bgm_ensure_window()`

Refills `gBgmWin` so at least 2 frames are readable from `gBgmReadPos`.
Returns `false` when the stream is over and not looping.

---

## Init / Shutdown Section

> **Source line 329** — `// --- init / shutdown ---`

Covers `audio_init()` and `audio_shutdown()`. Init opens the `sceAudioOut`
port and spawns the mixer thread. Shutdown signals `gQuit`, waits for thread
exit, releases hardware port, silences all voices, and frees the SFX cache and
BGM decoder.

---

## audio_init — sceAudioOutOpenPort Volume Note

> **Source lines 338–344** — inline comment inside `audio_init()`

`sceAudioOutOpenPort`'s own docs guarantee the port starts at
`SCE_AUDIO_VOLUME_0DB` (max) already — no explicit `sceAudioOutSetVolume` call
needed.

A previous version of this file called it anyway with an untested
channel-flag/array pairing; removed rather than risk it being the reason output
ended up quieter than the source material (verified bit-exact/0 dBFS-peaking
via a host-side resampling test — see `Docs/Fixes_Log.md #11`).

---

## audio_init — Mixer Thread Stack Size

> **Source lines 346–349** — inline comment before `sceKernelCreateThread`

128 KB stack: the mp3 decoder works on this thread's stack and 64 KB was proven
too small (core dump) during the SoLoud bring-up (with stb_vorbis; kept the
same margin switching decoders since minimp3's own frame buffers are a
comparable size).

---

## Background Music Section

> **Source line 389** — `// --- Background Music ---`

Covers all `Cocos2dxMusic_*` JNI surface functions and the internal
`bgm_prepare()` helper.

---

## bgm_prepare — Load / Reuse BGM Decoder

> **Source lines 391–392** — comment above `bgm_prepare()`

Loads (or reuses) the BGM decoder for the given raw path. Returns `false` on
any failure, leaving the previous BGM fully stopped and freed. Never touches
stdio.

---

## Sound Effects Section

> **Source line 534** — `// --- Sound Effects ---`

Covers all `Cocos2dxSound_*` JNI surface functions and the internal `sfx_get()`
cache helper.

---

## Cocos2dxSound_playEffect — JNI Signature Warning

> **Source lines 581–587** — inline comment inside `Cocos2dxSound_playEffect()`

In older Cocos2d-x versions (like the one used in PoP Classic), the
`playEffect` JNI signature is `(Ljava/lang/String;Z)I`, meaning it **only**
passes `path` and `loop`.

If `pitch`, `pan`, and `gain` are read using `va_arg`, it reads uninitialized
stack memory, which results in `gain = 0.0f` and completely mutes all sound
effects. These three parameters are therefore hardcoded to safe defaults
(`pitch=1.0`, `pan=0.0`, `gain=1.0`) instead of being read from `va_list`.

---

## Cocos2dxSound_unloadEffect — Silence Before Free

> **Source line 718** — `gVoices[i].smp = NULL; // silence before free`

Before freeing an `SfxSample`'s PCM buffer, all voices using it are set to
`smp = NULL` under `gLock`. This ensures the mixer thread cannot access freed
memory between the lock release and the `free()` call — the critical
use-after-free guard described in the [Hard Rules](#hard-rules-from-debugging).

---

## Cocos2dxSound_setEffectsVolume — gSfxVolume Scope

> **Source line 736** — `gSfxVolume = volume; // applies to effects started from now on`

`gSfxVolume` is applied at voice-launch time (multiplied into `gl`/`gr` in
`playEffect`). Changing volume does **not** retroactively adjust voices already
playing — it only affects effects started from the next `playEffect` call
onward.
