# CMakeLists.txt — Developer Comment Reference

Extracted developer/technical comments from
[`CMakeLists.txt`](../../CMakeLists.txt).

---

## Table of Contents

1. [Version History (v01.11–v01.20)](#version-history-v0111v0120)
2. [`ENABLE_VERBOSE_LOG` Rationale](#enable_verbose_log-rationale)
3. [`CMAKE_C_FLAGS_DEBUG` / `CMAKE_CXX_FLAGS_DEBUG` — the Silent `-O0` Bug](#cmake_c_flags_debug--cmake_cxx_flags_debug--the-silent--o0-bug)

---

## Version History (v01.11–v01.20)

**Location:** block comment above `set(VITA_APP_NAME ...)`.

This block duplicated, version by version, the same history already tracked in
[`Docs/CHANGELOG.md`](../CHANGELOG.md) (search there by version number, e.g. "v01.19", for the
full write-up). Rather than maintain two copies of the same history, the in-code comment now just
points here. Summary of what each version bump meant for the *build config specifically*:

| Version | Build-relevant change |
|---|---|
| v01.11 | Custom `sceAudioOut` mixer; SoLoud removed (Fixes_Log #10). |
| v01.12 | Verbose sequential logging always on, soft-limited audio output, native `SceAvPlayer` cutscene playback (Fixes_Log #11). |
| v01.13 | Fixed a v01.12 regression: `FALSOJNI_DEBUGLEVEL=0` logged every primitive JNI call (100s/sec), each paying a full file open+write+close, tanking the framerate and starving the audio thread of CPU (Fixes_Log #12). |
| v01.14 | Fixed a pre-existing, always-on FalsoJNI error path (`GetFloatArrayRegion`/`GetArrayLength` on an untracked array, once per touch-active frame) that fired hundreds of times/session once it started reaching the log file in v01.12. Fixed at the source (`length==0`/`NULL`-array probes are legitimate JNI no-ops, not errors), plus consecutive-duplicate suppression in the logger as defense in depth, plus restored a real missing method registration (`preloadEffect` via `CallStaticVoidMethod`) found along the way (Fixes_Log #13). |
| v01.15 | Made `source/main.c`'s "input tick" debug log edge-triggered (only on a touch/pad state *change*, not once per frame with any active touch) — the logger's duplicate suppression from v01.14 only collapsed exact repeats, so jitter in `touch.reportNum` between frames dodged it (Fixes_Log #14). |
| v01.16 | Fixed a zero-volume bug on all SFX (footsteps, hits, environment). The Android Cocos2dx 2.0 JNI signature for `playEffect` is `(Ljava/lang/String;Z)I`, meaning it does NOT pass pitch, pan, or gain. The variadic `va_arg` was reading garbage uninitialized stack memory for these missing parameters, resulting in a gain of `0.0f`, which silenced all sound effects. They are now hardcoded to `1.0f`/`0.0f`/`1.0f` (Fixes_Log #15). |
| v01.19 | Cutscene video performance + audio, and a build-system fix that affects everything else too: (a) hand-vectorized the YUV(NV12)→RGB565 conversion with NEON; (b) moved cutscene audio output to its own thread instead of blocking the render loop (a first attempt with `pthread_cond_t` crashed real hardware — this pthread port doesn't lazily initialize a statically-initialized cond var the way it does a mutex, fixed by using mutex+poll instead); (c) discovered every local dev build was silently compiling at `-O0` instead of `-O3` — see [§3](#cmake_c_flags_debug--cmake_cxx_flags_debug--the-silent--o0-bug) below. Confirmed on real hardware (Fixes_Log #18). |
| v01.20 | `original.apk` and the `.obb` are no longer required. `cocos2d::CCFileUtils::getFileData()` hardcoded EVERY relative file request straight into a ZIP read (`.apk` for `appConfig.txt`, `.obb` for everything else) with no loose-file check ever attempted at that level — unlike textures/maps/animations, which already went through this project's own `fopen_soloader()` and worked loose. `source/patch.c` now hooks `getFileData()` itself (via the previously-unused `hook_addr()` mechanism) to try a loose file for any relative path first, falling back to the real apk/obb-zip logic only if nothing loose exists. Confirmed on real hardware across three rounds of testing (Fixes_Log #19) — the `.apk`/`.obb` are still supported as a fallback, just no longer mandatory when the full `Data/` tree is present loose. |

---

## `ENABLE_VERBOSE_LOG` Rationale

**Location:** block comment above `option(ENABLE_VERBOSE_LOG ...)`.

Verbose sequential logging (module load, our own audio/video/scene milestones — see
[`Docs/Fixes_Log.md`](../Fixes_Log.md) #11) used to be tied to `CMAKE_BUILD_TYPE STREQUAL
"Debug"`, but `build_and_install.sh` always builds `Release`, so in practice `l_debug`/`l_info`/
`l_success` were **never** compiled in and only `l_error`/`l_fatal` ever reached the log file —
exactly the "only shows crashes" complaint. It's now decoupled from build type: `-O3` is hardcoded
in `CMAKE_C_FLAGS` regardless, so there's no performance reason to tie this to `Debug`. Turn it OFF
only if the log file is getting too large to page through.

It deliberately does **not** touch `FALSOJNI_DEBUGLEVEL` (left at its own default,
`FALSOJNI_DEBUG_INFO`): that `"ALL"` tier logs literally every primitive JNI call (`NewStringUTF`,
`FindClass`, `GetEnv`, `DeleteLocalRef`, ...), which fires constantly during normal engine
operation. A first attempt at enabling it produced 1600+ log lines in the first few seconds alone,
each paying a full `sceIoOpen`+`Write`+`Close` — this made the game unplayably slow and, by
stalling the main thread under the shared log mutex, starved the real-time audio mixer thread of
CPU right when it needed its ~46ms window, which is what made the audio sound harder/more
distorted too (see `Fixes_Log.md` #12). None of that per-primitive tracing is needed: every JNI
entry point *into* this project's own code (audio, video, dialogs, bitmaps, etc.) already logs
itself directly via `l_debug` in `java.c`/`audio.cpp`/`video.cpp`, independent of FalsoJNI's
internal dispatcher.

---

## `CMAKE_C_FLAGS_DEBUG` / `CMAKE_CXX_FLAGS_DEBUG` — the Silent `-O0` Bug

**Location:** block comment above `set(CMAKE_C_FLAGS_DEBUG ...)`.

CMake appends per-build-type flags (`CMAKE_C_FLAGS_DEBUG` etc.) **after** `CMAKE_C_FLAGS` on the
actual compile command, and the compiler honors the *last* `-O` flag it sees. Building with
`-DCMAKE_BUILD_TYPE=Debug` (needed for `-g` and to carry debug info in `ENABLE_VERBOSE_LOG` builds)
was silently re-adding `"-O0 -g -DDEBUG -D_DEBUG"` after this file's own `-O3`, and `-O0` won.

Every local Debug-type build was therefore compiling fully unoptimized while looking like a clean
build (no warnings, no errors) — confirmed 2026-07-28 by disassembling a hand-vectorized NEON
function: a 1852-byte stack frame with every single intermediate value, including NEON vector
registers, spilled to the stack and reloaded around each statement, textbook `-O0` codegen.

This project always wants `-O3` regardless of build type (`"Debug"` here is only ever used for
`-g`/`-DDEBUG`, never actual `-O0` debugging), so the Debug config's flags are overridden to drop
the competing `-O` level instead of relying on flag order.
