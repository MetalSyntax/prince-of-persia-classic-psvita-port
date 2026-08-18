# java.c — Developer Comment Documentation

Migrated from inline comments in [`source/java.c`](../../source/java.c).  
All Spanish comments have been translated to English.

---

## Table of Contents

1. [Cocos2dxActivity – Animation Interval and Online Integrations](#cocos2dxactivity--animation-interval-and-online-integrations)
2. [Cocos2dxHelper – Rewards Coins](#cocos2dxhelper--rewards-coins)
3. [IntroTextLayer – playVideo No-Op](#introtextlayer--playvideo-no-op)
4. [createTextBitmap – stb_truetype Rationale](#createtextbitmap--stb_truetype-rationale)
5. [Font Data Lifetime](#font-data-lifetime)
6. [startFlurry – No-Op](#startflurry--no-op)
7. [initializePapayaFramework – No-Op](#initializepapayaframework--no-op)
8. [CCVideoUtils/playVideo – Argument Handling](#ccvideoutilsplayvideo--argument-handling)
9. [Video Completion Callback](#video-completion-callback)
10. [preloadEffect Double-Registration](#preloadeffect-double-registration)
11. [WINDOW_SERVICE and SDK_INT Fields](#window_service-and-sdk_int-fields)

---

## Cocos2dxActivity – Animation Interval and Online Integrations

**Location:** `nameToMethodId[]` table, entries 60–64 (method IDs `setAnimationInterval`, `startFlurry`, `initializePapayaFramework`).

`appConfig.txt` already requests that Flurry analytics and the Papaya social/ad framework be disabled (`ENABLE_FLURRY=NO` / `ENABLE_PAPAYA=NO`). However, nothing in this loader actually invokes the native `GetConfig()` function that would make the engine honour those flags — on real Android, `GetConfig()` is called from Java's `Activity.onCreate()`, which this port does not have. As a result, the engine still attempts to reach both services regardless of the config file.

No-op stubs are the correct behaviour either way: none of these services should do anything on a Vita port.

---

## Cocos2dxHelper – Rewards Coins

**Location:** `nameToMethodId[]` table, entry 63 (`getRewardsCoins`).

`getRewardsCoins` is part of the same disabled online/cross-promotion integration as Flurry and Papaya above. There is no rewards or currency system on the Vita port, so the stub always returns `0`.

---

## IntroTextLayer – playVideo No-Op

**Location:** `nameToMethodId[]` table, entry 64 (`playVideo`).

`IntroTextLayer` plays an FMV cutscene via this static native call. Because there is no video codec or player active on this port (see also [CCVideoUtils/playVideo – Argument Handling](#ccvideoutilsplayvideo--argument-handling)), the method is a no-op and the game continues straight past it. This matches the `void` return type the caller expects.

---

## createTextBitmap – stb_truetype Rationale

**Location:** Block comment immediately before `#define MAX_FONTS 4` in the source.

### How Android normally works

On real Android, `createTextBitmap` calls back into native (`Cocos2dxBitmap_nativeInitBitmapDC`, exported by `libcocos2d.so`) with pixel data for the requested text, rasterized via `android.graphics.Canvas` / `Paint`.

### Why Sony font APIs were not used

`ScePvf` and `ScePgf` (the Vita's two built-in system font APIs) were investigated first. Both are **entirely unimplemented in Vita3K**: reading the Vita3K source (`vita3k/modules/ScePvf` / `ScePgf`) confirms that every exported function is a stub. No combination of arguments can produce real glyph data under the emulator. The same code would very likely work on real hardware, where the firmware actually implements them.

### Why stb_truetype was chosen

`stb_truetype` (public domain, vendored at `lib/stb/stb_truetype.h`) does not depend on any Sony system library and therefore works identically under Vita3K and on real hardware.

### Font selection logic

The game's `createTextBitmap` call always names a specific font asset. Real strings seen in the decompiled APK / `.so`:

| Font argument | Purpose |
|---|---|
| `Extra/font/UbiGameTextLReg.ttf` | Main game UI text |
| `Extra/font/UbisoftText.ttf` | Ubisoft branding text |
| `Extra/font/msmincho.ttf` | Japanese localisation |

An earlier version of this stub ignored the `fontName` argument entirely and always rendered with the bundled `DejaVuSans.ttf`, which is why in-game text never matched the original Ubisoft look.

The actual font files already live on the memory card under `DATA_PATH "Data/font/"` (the same location the engine's own asset loader uses), so `createTextBitmap` now reads `fontName` and loads the matching file from there.

`DejaVuSans.ttf` (bundled into the `.vpk` as `app0:/DejaVuSans.ttf` via `CMakeLists.txt`'s `vita_create_vpk FILE` list; see `extras/fonts/`) is kept only as a last-resort fallback when `fontName` is empty or names a file not present on the card, ensuring that text never silently disappears due to a font-name mismatch.

---

## Font Data Lifetime

**Location:** Inline comment after `stbtt_InitFont(...)` call inside `get_font()`.

The raw font file buffer (`data`) is **intentionally never freed**: `stbtt_fontinfo` keeps internal pointers directly into the buffer for the lifetime of the process. Freeing it would cause undefined behaviour on any subsequent glyph query.

---

## startFlurry – No-Op

**Location:** Body of `Cocos2dxActivity_startFlurry()`.

`ENABLE_FLURRY=NO` is set in `appConfig.txt`. Analytics have no place in a Vita port. The stub does nothing beyond emitting a debug log line.

---

## initializePapayaFramework – No-Op

**Location:** Body of `Cocos2dxActivity_initializePapayaFramework()`.

`ENABLE_PAPAYA=NO` is set in `appConfig.txt`. There is no ad or social framework on the Vita. The stub does nothing beyond emitting a debug log line.

---

## CCVideoUtils/playVideo – Argument Handling

**Location:** Body of `Cocos2dxActivity_playVideo()`, before the `video_play()` call.

`CCVideoUtils::playVideo(const char *path, bool, bool, ...)` forwards `path` as this call's first argument (confirmed from `libgame_logic.so`'s mangled symbol `_ZN12CCVideoUtils9playVideoEPKcbbP...`).

In FalsoJNI, `jstring` is a raw `char *` at runtime (see `GetStringUTFChars`'s `strdup(string)` in `FalsoJNI.c`), matching how `audio.cpp` already reads path arguments directly.

However, because we do not fully control what the compiled game pushes for the other (rarely-exercised) native call sites that share this same method slot, the implementation rejects anything that is not a plausible pointer (guard against misreading e.g. a stray `jboolean` as though it were the path) rather than dereferencing blindly. The threshold used is `(uintptr_t) j_path > 0x1000`.

---

## Video Completion Callback

**Location:** Body of `Cocos2dxActivity_playVideo()`, after the `video_play()` call.

Video playback — or the decision to skip it — **must always be followed by firing the completion callback** that Android's Java side would normally invoke once the video finishes. `VideoLayer` (in `libgame_logic.so`) blocks waiting for this callback; without it the engine would hang forever instead of continuing past the cutscene.

The callback resolved is `Java_org_cocos2dx_lib_Cocos2dxVideo_onVideoCompleted` (exported by `libcocos2d.so`).

---

## preloadEffect Double-Registration

**Location:** Comment before entry `{ 27, (void (*)(jmethodID, va_list)) Cocos2dxSound_preloadEffect }` in `methodsVoid[]`.

> **Translation of original Spanish comment (lines 490–501):**

`preloadEffect` (method ID 27) is registered here **in addition to** its entry in `methodsInt[]`. The game invokes it via the `CallStaticVoidMethod` path (confirmed by real logs — `"method ID 27 not found!"` on every level load). `methodVoidCall` could not find it because it had previously been removed from this table on suspicion that the double-registration was causing the `PC=0x20` SoLoud crash.

That hypothesis was ruled out — the real cause was the SoLoud file-hack (see `Fixes_Log.md #10`) — and removing it from here only broke the real preload without fixing anything.

The cast is necessary because `Cocos2dxSound_preloadEffect` returns `jint` while this table expects `void (*)(...)`. The return value is simply discarded under the ARM calling convention, with no unwanted side-effects. The same pattern is already used in `dynlib.c` for several symbols.

---

## WINDOW_SERVICE and SDK_INT Fields

**Location:** JNI Fields section, before `nameToFieldId[]`.

### `WINDOW_SERVICE`

System-wide string constant that applications sometimes request from `Context`.  
Reference: https://developer.android.com/reference/android/content/Context.html#WINDOW_SERVICE

Value: `"window"`

### `SDK_INT`

System-wide integer constant used to determine the Android API level at runtime.  
Reference: https://developer.android.com/reference/android/os/Build.VERSION.html#SDK_INT  
Possible values: https://developer.android.com/reference/android/os/Build.VERSION_CODES

Set to **19** (Android 4.4 / KitKat).
