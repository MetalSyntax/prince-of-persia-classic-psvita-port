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
8. [Controls Visibility Toggle – L1+R1 Combo](#controls-visibility-toggle--l1r1-combo)
9. [Jump-Plus-Walk Diagnostic Log](#jump-plus-walk-diagnostic-log)
10. [Cross Real Jump via control1Clicked](#cross-real-jump-via-control1clicked)

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

---

## Controls Visibility Toggle – L1+R1 Combo

**Location:** Comment above the combo-detection block right after the R1 keycode dispatch, and above the `ControlsLayer_sharedControlsLayer`/`CCMenuItemSprite_setOpacity` symbol resolution near the top of `main()`.

Holding **L1+R1** together toggles the on-screen virtual control buttons on/off, for players using the physical controller who don't want the touch HUD cluttering the screen. This does **not** reuse `Cocos2dxActivity_SetControlInVisible`/`SetControlVisible` (already called once at boot, further up in `main()`) — those write a single byte on `CCDirector`'s singleton (confirmed via `nm`/Ghidra: `*(iVar1 + 0xac) = 1/0`) that nothing in either `libcocos2d.so` or `libgame_logic.so` ever reads back; it's dead code left over from an Android-TV/hardware-keyboard code path that never shipped, and calling it has no visible effect.

**v01.21 originally used Select+Start for this combo — replaced in v01.22** after real hardware testing surfaced two problems traced to the same root cause. Select and Start each *individually* keep their existing bindings (keycode 82 `MENU` / keycode 4 `BACK`, further up in this file), and the game's own pause-menu flow opens off either one alone. Pressing the combo even slightly out of sync let one half register first and open the pause menu — exactly as reported ("si se hace a destiempo te lleva al menú de pausa"). Worse, once the pause menu had been touched this way, physical movement stopped responding even after hiding/showing again: confirmed via `arm-vita-eabi-objdump` that `ControlsLayer::tick(float)` (the function that drives all joystick/D-Pad direction handling every frame) opens with `if (CCDirector::sharedDirector()[0xad] != 0) return-early` — almost the entire function is skipped whenever that byte is set. That offset is one byte away from (and unrelated to) the dead `0xac` byte above; all evidence points to it being cocos2d-x's own `CCDirector::m_bPaused`, set by the game's real pause flow, not by anything this port's code writes. Switching the toggle to **L1+R1** (keycodes 102/103, with no pause association at all) removed that specific collision.

**v01.22–24 called `ControlsLayer::setControlsVisible(bool)` (mangled `_ZN13ControlsLayer18setControlsVisibleEb`) directly — replaced in v01.25 after a real console log exposed a second, unrelated problem: hiding the buttons this way also silently disabled walking.** `setControlsVisible` is the same function the original game calls on pause/resume, and it really does affect the real button sprites, not a vestigial flag — but a live `psvita-toolkit logs-live` capture showed footstep SFX stopping entirely for the whole time the controls stayed hidden (13 real seconds with `SCE_CTRL_RIGHT` confirmed held via `pad.buttons`) and resuming the instant they were shown again, while jump (`jump.mp3`) kept firing throughout. Root cause, confirmed by reading `ControlsLayer::init()`'s disassembly (not guessed): `setControlsVisible()` calls `CCNode::setIsVisible(bool)` (vtable slot 29, confirmed by hand-decoding `_ZTVN7cocos2d6CCNodeE`'s raw bytes) on the six `CCMenuItemImage` pointers at `this+0x174..0x188` (all six confirmed built via `CCMenuItemImage::itemFromFramesImage(...)` in `init()`), and `ControlsLayer::tick()` polls those exact same six pointers' click/visible state every frame to drive `control1Clicked()`/`control2Clicked()` (the D-Pad handlers) — jump never touches this array at all, since it's dispatched purely through the keycode → `AddEvent(JUMP)` path covered above, which is why it kept working. Setting a button invisible is, by the original engine's own design, indistinguishable from it not being pressable.

**Fix:** stop calling `setControlsVisible()` for this feature entirely. Instead, read those same six `this+0x174..0x188` pointers directly off the `ControlsLayer` singleton and call `CCMenuItemSprite::setOpacity(item, 0 or 255)` (mangled `_ZN7cocos2d16CCMenuItemSprite10setOpacityEh`, resolved from `libcocos2d.so`; confirmed exported and safe to call directly — its own disassembly shows it just recursively fades the button's normal/selected child images via `CCRGBAProtocol`, nothing else) on each. Opacity 0 looks identical to hidden on screen without ever touching `isVisible`, so `tick()`'s click/visible poll keeps seeing these buttons as available and D-Pad input keeps working while "hidden". This also means `setControlsVisible()`'s other side effects (its own unconditional hide of a *different*, apparently-unused set of six `CCSprite` pointers at `this+0x158..0x16c`, and its call into `HudLayer::setItemsVisible()`) no longer happen as part of this toggle — intentional, since the ask was specifically to hide the touch buttons, not other HUD chrome.

`comboMask`/edge-detection follows the same held-then-released pattern used elsewhere in this file (e.g. the combo only fires once per press, not every frame while held).

---

## Jump-Plus-Walk Diagnostic Log

**Location:** Comment above the `cross_combo` block right after `oldpad = current_pad;` in the main loop.

Added while investigating a report that the character can't jump (Cross) and walk (D-Pad Left/Right) at the same time using the physical controller, even though the equivalent two-finger touch gesture works on the touchscreen. A deep pass through the decompiled `libgame_logic.so`/`libcocos2d.so` (Prince's per-frame state dispatcher, `ControlsLayer::AddEvent`/`GetEvent`/`GetDirection`/`SetDirection`, the jump-trigger function that reads `GetDirection()` to produce a directional/running jump, `keyXClicked`/`keyRemoveX`) found **no code-level gate** preventing this combination — jump is dispatched via keycode 23 (`DPAD_CENTER` → `ControlsLayer::keyXClicked` → `AddEvent(JUMP)`), movement via synthetic touch into the same `ControlsLayer` singleton (→ `SetDirection`), and the jump-trigger explicitly reads the *current* movement direction to allow a running jump. Nothing found clears movement state on Cross press/release or vice versa.

Since this couldn't be confirmed or refuted by static analysis alone, this log is edge-triggered (fires once per state change, not every frame) and prints the exact Cross/Left/Right bitmask whenever Cross is held, so the next real hardware log capture of the failing combo can pinpoint whether the physical/touch dispatch itself sees the expected combined state — rather than guessing another blind fix. Follows this project's established methodology: one hypothesis at a time, confirmed by a real log, never guess-fix blindly.

**Correction (v01.26): the "jump-trigger function" and "state dispatcher" addresses this paragraph names were wrong, and the conclusion built on them doesn't hold.** A live-log capture (`live_session_20260830_183102.log`) confirmed Cross+Right really do arrive combined (`pad.buttons=0x00004020` held over a second) and that `jump.mp3` still plays — yet the character never actually left the ground while moving, only when standing still. Re-deriving the addresses independently (`arm-vita-eabi-objdump` + manual jump-table resolution, not trusted from the earlier unverified pass) found that `FUN_000c6aa2`/`FUN_000c78f0` are actually inside `SpecialItemsManager::PlaceHealthPotionAt`/`PickSpecialItemsNearBy` — potion/item pickup code with nothing to do with Prince's movement or jump state. See [Cross Real Jump via control1Clicked](#cross-real-jump-via-control1clicked) below for what's actually confirmed and the real fix.

---

## Cross Real Jump via control1Clicked

**Location:** Comment above the `ControlsLayer_control1Clicked` symbol resolution near the top of `main()`, and above the call to it inside the Cross-pressed block (face buttons section).

Properly tracing `Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown`'s own keycode dispatch table (resolved by hand: `table_base = value_at(0xa3ef8) + address_of("add r3,pc")+4`, then `target = table_base + table[keyCode-4]`, cross-checked against the one entry already known to be correct — keycode 4/BACK → `kTypeBackClicked`) turned up two facts neither previous session had verified:

- **Keycode 96 (`BUTTON_A`, sent alongside keycode 23 since this port's very first commit) resolves to the table's default "do nothing" stub.** It has never done anything. Every valid entry in this table is a real Android keycode with a real meaning: 4 (`BACK`), 19–23 (`DPAD_UP/DOWN/LEFT/RIGHT/CENTER`), 82 (`MENU`), 99/100/102/103 (further gamepad buttons this game also supports). 96 isn't one of them.
- **Keycode 23 (`DPAD_CENTER`) dispatches to `CCKeypadDispatcher` msgType 8, which calls the currently-active scene/layer's own `keyXClicked()` override** — a virtual method declared on `cocos2d::CCKeypadDelegate` and overridden by a long list of classes (`ControlsLayer` in gameplay, but also `SingleClickMenu`, `LevelSelectLayer`, `CutSceneSelectLayer`, `LevelBuyScreen`, and others in menus — confirmed via `nm -D`). In other words, "X" is a generic per-scene "Cross was pressed" callback, not a jump-specific one; menus need it for confirm, so keycode 23's dispatch can't just be removed.

**`ControlsLayer::keyXClicked()` itself was fully disassembled and confirmed to send `AddEvent(4)` or `AddEvent(16)` depending on an internal mode flag — never `AddEvent(8)`.** `AddEvent(4)` is the exact same event `ControlsLayer::control2Clicked()` sends (the on-screen **crouch** button), and `AddEvent(16)` is the same event `control4Clicked()` sends (the on-screen **attack** button, `control_combat_attack`). `AddEvent(8)` — confirmed via `nm`/disassembly to be what `ControlsLayer::control1Clicked()` sends — is the real jump event, matching the on-screen `control_platform_jump` button (one of the six `CCMenuItemImage`s at `this+0x174..0x188`, see the controls-visibility section above; `0x178` specifically resolves to `"control_platform_jump"`/`"control_platform_jump_p"` via the same string-literal-resolution technique used there). **Cross, via this port's original keycode-23/96 mapping, has never actually sent the real jump event** — whatever the earlier "works when standing still" behavior was is crouch or attack's own idle-state animation, not a genuine jump; the real, correctly-behaving jump-while-moving the user already confirmed via a two-finger touch test goes through `control1Clicked()`, not through this keycode path at all.

**Fix:** keycode 23/96 dispatch is left completely alone (still needed for menu confirm and whatever crouch/attack behavior already existed), and a **direct call to `ControlsLayer::control1Clicked()`** (resolved via `so_symbol`, called with the `ControlsLayer::sharedControlsLayer()` singleton as `this`) is added alongside it on Cross's rising edge only. This is not a repeat of the reverted synthetic-touch jump-highlight attempts (`docs/comments/main.c.md#jump-touch-highlight--reverted-twice`) — it never injects a touch at a screen coordinate, so it can't land on a menu list item and hijack selection; it only flips `ControlsLayer`'s own internal event bitmask, which does nothing while a menu scene (not `ControlsLayer`) is what's currently ticking.
