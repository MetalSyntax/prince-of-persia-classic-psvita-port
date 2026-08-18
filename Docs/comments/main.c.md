# main.c — Developer Comment Documentation

Migrated from inline comments in [`source/main.c`](../../source/main.c).

---

## Table of Contents

1. [JNI_OnLoad – Calling Every Module That Exports It](#jni_onload--calling-every-module-that-exports-it)
2. [nativeSetPaths – Argument Semantics and original.apk Fallback](#nativesetpaths--argument-semantics-and-originalapk-fallback)
3. [Touch Slot Registry – Hardware ID vs. Engine Slot Index](#touch-slot-registry--hardware-id-vs-engine-slot-index)
4. [Edge-Triggered Input Logging](#edge-triggered-input-logging)
5. [Virtual Finger Slots and the CC_MAX_TOUCHES Limit](#virtual-finger-slots-and-the-cc_max_touches-limit)
6. [Jump Touch-Highlight – Reverted Twice](#jump-touch-highlight--reverted-twice)
7. [Crouch – Shared Keycode for Down and Circle](#crouch--shared-keycode-for-down-and-circle)

---

## JNI_OnLoad – Calling Every Module That Exports It

**Location:** Comment above the loop that calls `JNI_OnLoad` on every loaded module, in `main()`.

Each loaded `.so` that exports `JNI_OnLoad` caches the `JavaVM*` pointer it is given in its own internal global variable, for later use — e.g. `SimpleAudioEngine` (in `libcocosdenshion.so`) attaches a background thread and calls `(*jvm)->GetEnv(...)` on that cached pointer. Because each module keeps its own independent copy of the pointer, `JNI_OnLoad` must be called on **every** module that exports it, not just whichever module happens to export it first.

---

## nativeSetPaths – Argument Semantics and original.apk Fallback

**Location:** Inside the `if (nativeSetPaths)` block in `main()`, immediately before the call to `nativeSetPaths()`.

### Argument semantics (confirmed by testing)

Two different native code paths consume `nativeSetPaths`'s arguments, matching real Android semantics:

- `apkFilePath` (argument 1) is treated as the *folder* that would hold the real Android `/Android/obb/<package>/` directory: the engine appends the known `.obb` filename to it directly (e.g. it reads `<apkFilePath>/main.1.org.ubisoft.premium.POPClassic.obb` as a zip) to pull `Data*/` files such as `Localization/*.loc`.
- `apkSourceDir` (argument 3) is opened natively (via zlib) *directly* as a zip/apk file, to read `assets/appConfig.txt` — it must point straight at the `.apk`, not at a folder.

`Data/*` loose assets under `DATA_PATH` are unaffected either way, since those are read via plain `fopen()`, not through either of these paths.

### original.apk no longer required

See also `Fixes_Log.md` #19. Without `original.apk`, the engine's own `getFileData()` would try to read `assets/appConfig.txt` from a NULL zip handle and crash with a confusing Data abort instead of a clear error message. `source/patch.c`'s `hook_getFileData()` now serves `"appConfig.txt"` from a loose file (`Data_960_576/appConfig.txt` or bare `DATA_PATH/appConfig.txt`) *before* the engine's real zip-based `getFileData()` ever runs, so a missing `original.apk` is only a real problem if neither loose candidate exists either.

`main()` checks for both loose candidates the same way the hook does, and only calls `fatal_error()` if there is truly no way to serve `appConfig.txt` from anywhere. If `original.apk` is missing but a loose `appConfig.txt` was found, it logs an informational message and continues.

---

## Touch Slot Registry – Hardware ID vs. Engine Slot Index

**Location:** Comment above the `int slotHwId[5]` declaration in `main()`.

`slotHwId[]` tracks which Vita hardware touch id currently occupies each of the 5 engine touch slots (`-1` = free). This is **not** the id handed to the engine: `SceTouchReport::id` is an 8-bit counter that keeps growing for the whole session (not a small 0–4 range like Android's pointer ids), while `nativeTouchesBegin`/`Move`/`End` index a fixed-size array internally with whatever id they are given. Passing the raw hardware id therefore writes out of bounds and corrupts the heap — confirmed via `vita-parse-core` on two real crash dumps, both showing the crash inside or downstream of `nativeTouchesEnd`. The slot index (0–4) is what actually gets sent to the engine instead.

---

## Edge-Triggered Input Logging

**Location:** Comment above the `input tick` debug log block in the main loop.

The log line is edge-triggered: it only fires when the touch/pad state actually **changes** from the previous frame, plus a periodic heartbeat every 120 frames while idle. The previous, level-triggered condition fired on every single frame with any active touch, so a sustained drag alone logged 60 identical lines per second. The logger's consecutive-duplicate suppression (`Fixes_Log.md` #13) collapses exact repeats, but any jitter in `reportNum`/`buttons` between frames defeated that suppression, since jittering values count as distinct messages each time, not repeats.

---

## Virtual Finger Slots and the CC_MAX_TOUCHES Limit

**Location:** Comment above the loop that builds the combined `reportHwId`/`reportX`/`reportY` list in the main loop.

Each frame, one combined list of "virtual fingers" is built: the real touches plus (if held) synthetic ones for the D-Pad-driven joystick drag and the combat/action buttons below. All of them compete for the **same** 5 engine touch slots (0–4), never a 6th one: cocos2d-x's Android touch dispatch is sized for `CC_MAX_TOUCHES == 5`, so id 5 is already one past the end of its internal array. This was confirmed the hard way — that exact off-by-one corrupted the heap on real hardware.

---

## Jump Touch-Highlight – Reverted Twice

**Location:** Comment above the `DPAD DOWN is handled via nativeKeyDown` line, where a synthetic touch for the Jump virtual button would otherwise be added.

Jump's on-screen virtual button intentionally does **not** get a synthetic touch, and so never lights up as "pressed" the way Walk's virtual button does. Cross also means "confirm" in menus, and **any** synthetic touch tied to Cross — even at the real, screenshot-measured button position (904, 399), not a guess — reproduces the exact same regression as the first, guessed attempt at (815, 400): it hijacks list navigation. Confirmed twice, in `log_000011` and `log_000039`, both landing on the wrong list item on a Cross press.

Root cause: this file has no signal to tell "in a menu" apart from "in gameplay," so a synthetic touch fires in both, and menus interpret it as a real tap. This needs a real gameplay/menu state signal — not currently exposed anywhere in this codebase — before it can be attempted safely again; do not re-add without one. Jump itself still works correctly via the keycodes below; only the virtual pad's visual highlight is missing.

---

## Crouch – Shared Keycode for Down and Circle

**Location:** Comment above the combined Down/Circle crouch handling in the main loop.

Crouch (keycode 20, `DPAD_DOWN`) is shared by two physical inputs: Down/left-stick-down and Circle (keycode 97/`BUTTON_B`, tried for Circle first, did nothing in gameplay — confirmed on hardware). The press/release transition is computed on the **combined** (OR'd) state of both inputs, not on each button separately: sending `keyUp` on Circle's release alone would cancel crouch even while Down is still physically held.
