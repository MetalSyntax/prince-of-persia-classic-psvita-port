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
9. [ControlsLayer Member Layout – Sprites vs. Menu Items](#controlslayer-member-layout--sprites-vs-menu-items)
10. [Jump-Plus-Walk Diagnostic Log](#jump-plus-walk-diagnostic-log)
11. [Cross – Keycode 23 Already Is the Jump Event](#cross--keycode-23-already-is-the-jump-event)
12. [Native Xperia PLAY Control Path](#native-xperia-play-control-path)

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

**Location:** Comment above the combo-detection block right after the R1 keycode dispatch, and above the `ControlsLayer_sharedControlsLayer`/`CCMenuItemSprite_setOpacity` symbol resolution near the top of `main()` (superseded in v01.28 — see [Native Xperia PLAY Control Path](#native-xperia-play-control-path)).

Holding **L1+R1** together toggles the on-screen virtual control buttons on/off, for players using the physical controller who don't want the touch HUD cluttering the screen.

> **Correction (v01.27): the claim that `SetControlVisible`/`SetControlInVisible` are dead code was wrong, and it is why this feature ended up being built the hard way.** Those two exported JNI stubs (`libcocos2d.so` @ `0xa331c`/`0xa3338`) do write a single byte — `CCDirector::sharedDirector()[0xac] = 1/0` — but `libgame_logic.so` **does** read it back, in two places: `ControlsLayer::tick(float)` @ `0x79280` compares it against its own cached copy at `ControlsLayer+0x1cb` (`m_ControlsXVisible` — the game logs it under that name via `CCLog`) and, on any change, hides all twelve control nodes and then re-runs `ControlsLayer::setControlsVisible(m_ControlsXVisible)`; and `SingleClickMenu::initWithItems` @ `0xbe588` reads it to decide whether to place an initial keyboard cursor via `moveItemSelection(1)`. What gates that path is **not** the byte being unread — it is the byte one address higher, `0xad`, which the next paragraph also describes incorrectly. See [Native Xperia PLAY Control Path](#native-xperia-play-control-path).

**v01.21 originally used Select+Start for this combo — replaced in v01.22** after real hardware testing surfaced two problems traced to the same root cause. Select and Start each *individually* keep their existing bindings (keycode 82 `MENU` / keycode 4 `BACK`, further up in this file), and the game's own pause-menu flow opens off either one alone. Pressing the combo even slightly out of sync let one half register first and open the pause menu — exactly as reported ("si se hace a destiempo te lleva al menú de pausa"). Worse, once the pause menu had been touched this way, physical movement stopped responding even after hiding/showing again: confirmed via `arm-vita-eabi-objdump` that `ControlsLayer::tick(float)` (the function that drives all joystick/D-Pad direction handling every frame) opens with `if (CCDirector::sharedDirector()[0xad] != 0) return-early` — almost the entire function is skipped whenever that byte is set. That offset is one byte away from (and unrelated to) the dead `0xac` byte above; all evidence points to it being cocos2d-x's own `CCDirector::m_bPaused`, set by the game's real pause flow, not by anything this port's code writes. Switching the toggle to **L1+R1** (keycodes 102/103, with no pause association at all) removed that specific collision.

> **Correction (v01.27): `CCDirector+0xad` is not `m_bPaused`, and `tick()` does not return early on it.** The byte is written by exactly one place in either library — `Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetPaths`, which does `strcmp(device, "R800i")` and sets it to 1 on match, 0 otherwise. It is the game's **IsXperia** flag (the game logs it under that name in `ControlsLayer::setControlsVisible`), i.e. "am I running on a Sony Ericsson Xperia PLAY, which has physical game buttons". `ControlsLayer::tick()` @ `0x7909c` branches on it to `0x7927a`, which is the physical-gamepad control-visibility path, and that path **falls back through to the normal per-frame handler** at `0x790a6` once it has applied any visibility change — it never returns early. This port passed `"PSVita"` as the device string from its first commit, so `IsXperia` was always 0 and none of the game's own physical-button support was ever reachable. The Select+Start collision this paragraph describes was real, but the movement-freeze diagnosis attached to it was not — L1+R1 remains the combo regardless, since it is the better choice on its own merits.
>
> Two further notes on this paragraph's premise: on a handheld Vita the physical shoulder buttons report as `SCE_CTRL_LTRIGGER`/`SCE_CTRL_RTRIGGER` (`0x100`/`0x200`), **not** `SCE_CTRL_L1`/`SCE_CTRL_R1` (`0x400`/`0x800`, PSTV/DualShock only — see `psp2common/ctrl.h`), so the combo code and the keycode-102/103 dispatch were never reading the same buttons. And keycode 102 resolves to `ControlsLayer::keyLeftKeyClicked`, whose entire body is `return` — it has never done anything — while keycode 103 (`keyRightKeyClicked`) just sets the interact flag at `+0x1cc`, duplicating Triangle.

**v01.22–24 called `ControlsLayer::setControlsVisible(bool)` (mangled `_ZN13ControlsLayer18setControlsVisibleEb`) directly — replaced in v01.25 after a real console log exposed a second, unrelated problem: hiding the buttons this way also silently disabled walking.** `setControlsVisible` is the same function the original game calls on pause/resume, and it really does affect the real button sprites, not a vestigial flag — but a live `psvita-toolkit logs-live` capture showed footstep SFX stopping entirely for the whole time the controls stayed hidden (13 real seconds with `SCE_CTRL_RIGHT` confirmed held via `pad.buttons`) and resuming the instant they were shown again, while jump (`jump.mp3`) kept firing throughout. Root cause, confirmed by reading `ControlsLayer::init()`'s disassembly (not guessed): `setControlsVisible()` calls `CCNode::setIsVisible(bool)` (vtable slot 29, confirmed by hand-decoding `_ZTVN7cocos2d6CCNodeE`'s raw bytes) on the six `CCMenuItemImage` pointers at `this+0x174..0x188` (all six confirmed built via `CCMenuItemImage::itemFromFramesImage(...)` in `init()`), and `ControlsLayer::tick()` polls those exact same six pointers' click/visible state every frame to drive `control1Clicked()`/`control2Clicked()` (the D-Pad handlers) — jump never touches this array at all, since it's dispatched purely through the keycode → `AddEvent(JUMP)` path covered above, which is why it kept working. Setting a button invisible is, by the original engine's own design, indistinguishable from it not being pressable.

**Fix:** stop calling `setControlsVisible()` for this feature entirely. Instead, read those same six `this+0x174..0x188` pointers directly off the `ControlsLayer` singleton and call `CCMenuItemSprite::setOpacity(item, 0 or 255)` (mangled `_ZN7cocos2d16CCMenuItemSprite10setOpacityEh`, resolved from `libcocos2d.so`; confirmed exported and safe to call directly — its own disassembly shows it just recursively fades the button's normal/selected child images via `CCRGBAProtocol`, nothing else) on each. Opacity 0 looks identical to hidden on screen without ever touching `isVisible`, so `tick()`'s click/visible poll keeps seeing these buttons as available and D-Pad input keeps working while "hidden". This also means `setControlsVisible()`'s other side effects (its own unconditional hide of a *different*, apparently-unused set of six `CCSprite` pointers at `this+0x158..0x16c`, and its call into `HudLayer::setItemsVisible()`) no longer happen as part of this toggle — intentional, since the ask was specifically to hide the touch buttons, not other HUD chrome.

`comboMask`/edge-detection follows the same held-then-released pattern used elsewhere in this file (e.g. the combo only fires once per press, not every frame while held).

---

## ControlsLayer Member Layout – Sprites vs. Menu Items

**Location:** Comments above `kSpriteOffsets`/`kButtonOffsets` inside the L1+R1 combo block.

The twelve control nodes `setControlsVisible` touches are **not** all the same type, and the set at `this+0x158..0x16c` is not "apparently unused" as the section above claimed. Resolved from `ControlsLayer::init()`'s disassembly (`0x78618`), reading each creation call's pc-relative string literal (`target = word_at(literal_slot) + address_of("add rN,pc") + 4`):

| Offset | Created by | Real type | Sprite frame |
|---|---|---|---|
| `0x158` | `CCSprite::spriteWithSpriteFrameName` | `cocos2d::CCSprite` | `move_slider_base` |
| `0x15c` | `CCSprite::spriteWithSpriteFrameName` | `cocos2d::CCSprite` | `move_slider` |
| `0x160` | `CCSprite::spriteWithSpriteFrameName` | `cocos2d::CCSprite` | `joystick_base` |
| `0x164` | `CCSprite::spriteWithSpriteFrameName` | `cocos2d::CCSprite` | `joystick` |
| `0x168` | `CCMenuItemImage::itemFromFramesImage` | `cocos2d::CCMenuItemImage` | `control_arrow_left` |
| `0x16c` | `CCMenuItemImage::itemFromFramesImage` | `cocos2d::CCMenuItemImage` | `control_arrow_right` |
| `0x174` | `CCMenuItemImage::itemFromFramesImage` | `cocos2d::CCMenuItemImage` | `control_platform_crouch` |
| `0x178` | `CCMenuItemImage::itemFromFramesImage` | `cocos2d::CCMenuItemImage` | `control_platform_jump` |
| `0x17c` | `CCMenuItemImage::itemFromFramesImage` | `cocos2d::CCMenuItemImage` | `control_platform_interact` |
| `0x180` | `CCMenuItemImage::itemFromFramesImage` | `cocos2d::CCMenuItemImage` | `control_combat_attack` |
| `0x184` | `CCMenuItemImage::itemFromFramesImage` | `cocos2d::CCMenuItemImage` | `control_combat_defend` |
| `0x188` | `CCMenuItemImage::itemFromFramesImage` | `cocos2d::CCMenuItemImage` | `control_combat_seath` |

The first four are the *slider* and *joystick* control schemes (`SaveGame::GetSelectedControls()` picks which scheme is live — `CONTROLS_SLIDER` / `CONTROLS_BUTTONS` / `CONTROLS_JOYSTICK`); the arrows at `0x168`/`0x16c` are the left/right movement buttons the synthetic-touch D-Pad emulation was aiming at. `setControlsVisible` reaches all twelve through the **virtual** `CCNode::setIsVisible` (vtable `+0x74`), so the type difference never mattered to it.

**This matters because `setOpacity` is not virtual and must be called on the right class.** `sizeof(CCMenuItemImage)` is `0x11c` (284 bytes — `operator new(142 << 1)` in `itemFromFramesImage`), while `CCSprite::setOpacity` (`libcocos2d.so` @ `0xa5cfc`) writes `this+0x100`, reads `this+0x1bf`, may make a virtual call through `vtable[0xfc]` with `this+0x1bc`, and then calls `CCSprite::updateColor()`, which writes the quad vertex colours at `this+0x168`, `+0x16a`, `+0x1b0`, `+0x1b2` and beyond. Pointing that at a 284-byte `CCMenuItemImage` overruns the allocation by up to ~0x9a bytes — silent heap corruption on every toggle. `CCMenuItemSprite::setOpacity` (`0x93c10`) only touches `+0x110`/`+0x114`/`+0x118` (the normal/selected/disabled child images), all inside those 284 bytes, and `CCMenuItemImage` derives from `CCMenuItemSprite` and adds no members — so it is the correct entry point for all eight menu items, arrows included.

---

## Jump-Plus-Walk Diagnostic Log

**Location:** Comment above the `cross_combo` block right after `oldpad = current_pad;` in the main loop.

Added while investigating a report that the character can't jump (Cross) and walk (D-Pad Left/Right) at the same time using the physical controller, even though the equivalent two-finger touch gesture works on the touchscreen. A deep pass through the decompiled `libgame_logic.so`/`libcocos2d.so` (Prince's per-frame state dispatcher, `ControlsLayer::AddEvent`/`GetEvent`/`GetDirection`/`SetDirection`, the jump-trigger function that reads `GetDirection()` to produce a directional/running jump, `keyXClicked`/`keyRemoveX`) found **no code-level gate** preventing this combination — jump is dispatched via keycode 23 (`DPAD_CENTER` → `ControlsLayer::keyXClicked` → `AddEvent(JUMP)`), movement via synthetic touch into the same `ControlsLayer` singleton (→ `SetDirection`), and the jump-trigger explicitly reads the *current* movement direction to allow a running jump. Nothing found clears movement state on Cross press/release or vice versa.

Since this couldn't be confirmed or refuted by static analysis alone, this log is edge-triggered (fires once per state change, not every frame) and prints the exact Cross/Left/Right bitmask whenever Cross is held, so the next real hardware log capture of the failing combo can pinpoint whether the physical/touch dispatch itself sees the expected combined state — rather than guessing another blind fix. Follows this project's established methodology: one hypothesis at a time, confirmed by a real log, never guess-fix blindly.

**Correction (v01.26): the "jump-trigger function" and "state dispatcher" addresses this paragraph names were wrong, and the conclusion built on them doesn't hold.** A live-log capture (`live_session_20260830_183102.log`) confirmed Cross+Right really do arrive combined (`pad.buttons=0x00004020` held over a second) and that `jump.mp3` still plays — yet the character never actually left the ground while moving, only when standing still. Re-deriving the addresses independently (`arm-vita-eabi-objdump` + manual jump-table resolution, not trusted from the earlier unverified pass) found that `FUN_000c6aa2`/`FUN_000c78f0` are actually inside `SpecialItemsManager::PlaceHealthPotionAt`/`PickSpecialItemsNearBy` — potion/item pickup code with nothing to do with Prince's movement or jump state. See [Cross Real Jump via control1Clicked](#cross-real-jump-via-control1clicked) below for what's actually confirmed and the real fix.

---

## Cross – Keycode 23 Already Is the Jump Event

**Location:** Comment above the Cross rising/falling-edge block in the face-buttons section of the main loop.

> **This section replaces "Cross Real Jump via control1Clicked" (v01.26), whose central conclusion was exactly inverted.** That section stated that `control1Clicked` sends `AddEvent(8)` = the real jump event matching `control_platform_jump` at `0x178`, that `control2Clicked` is the crouch button, and that `keyXClicked`'s `AddEvent(4)` is therefore crouch. Every one of those three mappings is backwards, and v01.26 shipped Cross wired to the **crouch** button as a result.

**What the binary actually says.** `ControlsLayer::tick(float)` polls the six platform/combat buttons and dispatches each one, so the button ↔ selector pairing can be read straight off the poll order (`0x790ba` onward, `vtable+0xf0` = `CCMenuItem::getIsSelected`, which reads `this+0xf7`):

| Member | Sprite frame | Selector | Event sent |
|---|---|---|---|
| `0x174` | `control_platform_crouch` | `control1Clicked` @ `0x77164` | `AddEvent(8)` |
| `0x178` | `control_platform_jump` | `control2Clicked` @ `0x77170` | `AddEvent(4)` |
| `0x17c` | `control_platform_interact` | `control3Clicked` @ `0x7717c` | `RemoveEvent(2)` + `AddEvent(1)` |
| `0x180` | `control_combat_attack` | `control4Clicked` @ `0x7719c` | `AddEvent(0x10)` |
| `0x184` | `control_combat_defend` | `control5Clicked` @ `0x771a8` | `AddEvent(0x20)` |
| `0x188` | `control_combat_seath` | `control6Clicked` @ `0x771b4` | `AddEvent(0x80)` |

So **`AddEvent(4)` is jump and `AddEvent(8)` is crouch**, and the `CONTROL_EVENT` bitmask (`ControlsLayer+0x1d0`, OR-ed by `AddEvent` @ `0x770d8`) is: `1` = walk, `2` = run, `4` = jump/up, `8` = crouch/down, `0x10` = attack, `0x20` = defend, `0x40` = (space, see below), `0x80` = sheath.

**Keycode 23 therefore already was the jump event.** The `nativeKeyDown` keycode table and the `CCKeypadDispatcher::dispatchKeypadMSG` message→vtable-slot switch were both re-derived, and `ControlsLayer`'s keypad-delegate vtable was dumped directly out of `.data.rel.ro` (found by scanning for `_ZThn260_N13ControlsLayer14keyBackClickedEv`'s address at file offset `0x13ce34`, vaddr `0x144e34`) rather than inferred:

| Keycode | MSG | Vtable slot | Handler | Platform mode (`+0x1c9` = 0) | Combat mode (= 1) | Release handler |
|---|---|---|---|---|---|---|
| 4 `BACK` | 1 | `+0x00` | `keyBackClicked` | pause / back | — | `keyRemoveBack` |
| 82 `MENU` | 2 | `+0x04` | `keyMenuClicked` | — | — | *(none)* |
| 19 `DPAD_UP` | 3 | `+0x08` | `keyUpClicked` | `AddEvent(4)` jump | *(nothing)* | `keyRemoveUp` |
| 20 `DPAD_DOWN` | 4 | `+0x0c` | `keyDownClicked` | `AddEvent(8)` crouch | *(nothing)* | `keyRemoveDown` |
| 21 `DPAD_LEFT` | 5 | `+0x10` | `keyLeftClicked` | walk→run, dir = left | same | `keyRemoveLeft` |
| 22 `DPAD_RIGHT` | 6 | `+0x14` | `keyRightClicked` | walk→run, dir = right | same | `keyRemoveRight` |
| *(unreachable)* | 7 | `+0x18` | `keySpaceClicked` | `AddEvent(0x40)` | `AddEvent(0x10)` | `keyRemoveSpace` |
| **23 `DPAD_CENTER`** | 8 | `+0x1c` | **`keyXClicked`** | **`AddEvent(4)` jump** | `AddEvent(0x10)` attack | `keyRemoveX` |
| 99 `BUTTON_C` | 9 | `+0x20` | `keySqrClicked` | `AddEvent(8)` crouch | `AddEvent(0x20)` defend | `keyRemoveSqr` |
| 100 `BUTTON_Z` | 10 | `+0x24` | `keyTriClicked` | sets interact flag `+0x1cc` | `AddEvent(0x80)` sheath | `keyRemoveTri` |
| 102 `BUTTON_L1` | 0xb | `+0x28` | `keyLeftKeyClicked` | **empty body** | — | `keyRemoveLeftKey` |
| 103 `BUTTON_R1` | 0xc | `+0x2c` | `keyRightKeyClicked` | sets interact flag `+0x1cc` | — | `keyRemoveRightKey` |

Two things fall out of this table. `keySpaceClicked` — the only handler that sends `AddEvent(0x40)` — has **no keycode at all**: `nativeKeyDown` can emit MSG 1–6 and 8–0xc but never MSG 7, so that event is reachable only by calling the vtable slot directly. And keycode 96 (`BUTTON_A`) really is absent from the table, exactly as v01.26 found; that part was right, and it is now dropped.

**Why the events survive the frame.** Every `key*Clicked` handler also sets the byte at `ControlsLayer+0x1e8` ("a physical key is driving me"), and `tick()`'s no-button-selected path checks it at `0x795f6` and returns without clearing the event mask; the `keyRemove*` handlers clear it again once `GetEvent()` drops to 0. So keyboard-injected events are explicitly protected from being wiped by the on-screen-button poll — the port never needed to bypass the keycode path.

**Fix:** Cross sends keycode 23 down on its rising edge and up on its falling edge, and nothing else. No `control1Clicked` call (that is crouch, and Circle/Down/Square already cover crouch), no keycode 96 (no-op), no `crossSentKey` bookkeeping. Because keycode 23 is a per-scene `keyXClicked` callback and `SingleClickMenu`, `LevelSelectLayer`, `CutSceneSelectLayer`, `LevelBuyScreen`, `LevelCompleteStats`, `ModesUnlock`, `IntroTextLayer`, `GameInfoText` and `Offers` all override it (`nm -D`), the same single keycode is jump in platform mode, attack in combat, and confirm in every menu.

A note on the v01.26 diagnosis that jump "works when standing still but not while moving": with Cross bound to crouch, that is expected — and the reason the *original* keycode-23 binding may still have looked broken on hardware is combat mode, where `keyXClicked` sends `AddEvent(0x10)` (attack) instead of `AddEvent(4)`. Worth re-checking on real hardware in platform mode specifically before assuming anything else is wrong.

---

## Native Xperia PLAY Control Path

**Location:** The `"R800i"` device string passed to `nativeSetPaths`, and the `SetControlVisible`/`SetControlInVisible` symbol resolution near the top of `main()`.

This is the Xperia PLAY build of the game, and it ships a complete physical-gamepad code path that this port never switched on. `Java_org_cocos2dx_lib_Cocos2dxActivity_nativeSetPaths`'s third argument is a device name, and it is used for exactly one thing:

```c
if (strcmp(device, "R800i") == 0) CCDirector::sharedDirector()[0xad] = 1;  // IsXperia
else                              CCDirector::sharedDirector()[0xad] = 0;
```

`R800i` is the Sony Ericsson Xperia PLAY's model number. Nothing else in either library reads that string — the resource path and the `original.apk` source dir are the other two arguments — so changing it from `"PSVita"` to `"R800i"` has no asset-loading side effects.

With `IsXperia` set, three things change, all of them wanted here:

1. **`ControlsLayer::tick(float)`** branches at its first instruction (`0x7909c`) into the gamepad path at `0x7927a`. That path compares `CCDirector+0xac` against its cached copy `m_ControlsXVisible` (`ControlsLayer+0x1cb`, initialised to 1 in `init()`), and on a change hides all twelve control nodes via the virtual `setIsVisible(false)` and then calls `setControlsVisible(m_ControlsXVisible)` to bring back just the six action buttons if the flag says visible. Then it falls through to the normal handler. Since `main()` already calls `SetControlInVisible()` at boot (`CCDirector+0xac = 0`), the on-screen controls hide themselves natively on the first tick of the first level — no member-offset pokes, no opacity trick, and none of the "hidden controls disable walking" problem that motivated the opacity approach, because the game is hiding controls it is no longer polling.
2. **`SingleClickMenu::initWithItems`** (`0xbe588`) places an initial keyboard cursor via `moveItemSelection(1)` when `IsXperia` is set and controls are invisible, so menus get a highlight to move with the D-Pad. `SingleClickMenu::onExit` still removes both its keypad *and* its touch delegate, so touchscreen menu navigation keeps working alongside it.
3. **`Tutorials::ShowPopUp(int)`** (`0xcbd00`) takes its gamepad branch, so tutorial popups describe physical buttons instead of on-screen ones.

**Show/hide toggle.** L1+R1 (physically `SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER`) now just calls the two exported JNI stubs `Cocos2dxActivity_SetControlVisible` / `SetControlInVisible`, which flip `CCDirector+0xac`; `tick()` picks the change up on the next frame and applies it through the game's own code. This replaces reading raw member offsets off the `ControlsLayer` singleton and calling `setOpacity` on them — see [ControlsLayer Member Layout](#controlslayer-member-layout--sprites-vs-menu-items) for why that approach was hazardous.

**Walk vs. run, and jumping while running.** `keyLeftClicked`/`keyRightClicked` implement a two-stage promotion:

```c
if (!(GetEvent() & 1) && !(GetEvent() & 2) && this[0x1f8] == 0) {
    AddEvent(1); SetDirection(dir);                       // first press: walk
} else {
    AddEvent(2); SetDirection(dir); this[0x1f8] = 1;      // again while walking: run
}
```

and `keyRemoveLeft`/`keyRemoveRight` clear events 1 and 2 plus the run flag at `+0x1f8`. The on-screen arrows drive the same promotion on a timer: `tick()` @ `0x7951e` runs `CCSequence(CCDelayTime(0.25f), CCCallFunc(this, &ControlsLayer::setPrinceRun))` on first touch, and `setPrinceRun` (`0x770cc`) sets `+0x1f8 = 1`. The port replicates that rule for physical buttons — keycode 21/22 down on the press edge, then **once more** after the direction has been held for 250 ms, which promotes walk to run; keycode up on release. Tap to take a single careful step, hold to run, exactly as the touch controls behave.

Jumping while running then needs nothing special: the event mask is a bitfield, so holding a direction (event `2`) and pressing Cross (event `4`) leaves both bits set simultaneously — which the synthetic-touch D-Pad could never reliably express, since it had to fit walk, run and jump into finite touch slots at guessed screen coordinates.
