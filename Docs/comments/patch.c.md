# patch.c – Developer Comments

> Extracted from `source/patch.c`.  
> Patching `.so` internal functions or bridging them to native for better compatibility.

---

## Loose-File Override for `CCFileUtils::getFileData`

**Location:** file-level block comment before `#define GETFILEDATA_SYM`

### Background – Why the original APK/OBB are no longer required

The original `original.apk` and `.obb` files are no longer required (see *Fixes_Log #19*). Every relative file request is served loose from the memory card when present, falling back to the APK/OBB ZIP only for whatever isn't.

### Root cause in `cocos2d::CCFileUtils::getFileData`

`cocos2d::CCFileUtils::getFileData()` (reverse-engineered in `so_decompiled/libcocos2d/out_ghidra.c`, confirmed against the real `bin/libcocos2d.so` symbol table) sends every relative (non-`/`-prefixed) path **straight into a ZIP read with no loose-file check ever attempted** at this level:

- A name equal to `"appConfig.txt"` always reads from `original.apk`.
- Anything else always reads from the `.obb`.

Textures, maps, and animations reach the memory card through a **different** path that already goes through this project's own `fopen_soloader()` / `resolve_data_path()` (`source/reimpl/io.c`), which is why those already work loose. However, `getFileData()` itself is a second, narrower choke point that other assets go through too — discovered one at a time on real hardware:

| Crash ID | Asset | Notes |
|---|---|---|
| `psp2core-1785297093` | `"Data_960_576/Localization/Spanish/Localizable.loc"` | First attempt only matched a `"Localization/"` prefix; the engine already bakes the resolution folder into the string for this request, so it never matched and fell through to the missing `.obb`. |
| `psp2core-1785297502` | `"Data_960_576/Logo/logo.png"` | `LogoScene::init()` — a totally different asset. Same crash: engine has no graceful handling for `getFileData()` returning `NULL`; it always assumes the read succeeded and crashes downstream (`R0=0xFFFFFFF8`). |

### Solution – Universal loose-file fallback

Rather than keep special-casing one newly-discovered filename per hardware round-trip, the hook now tries a loose file for **any** relative path, falling back to the real APK/OBB-zip logic only when no loose copy exists.

### Hooking strategy

The hook targets `getFileData()` itself via the existing (previously unused) `hook_addr()` mechanism rather than trying to patch every call site. It patches the function's own entry once, so it doesn't matter how many places inside `libcocos2d.so` / `libgame_logic.so` call it.

**Unhook/call/rehook pattern:** `hook_addr()` overwrites the target's own instructions, so calling "through" the hooked address would re-enter this hook. `so_unhook()` / `hook_addr()` restore and reapply around the real call instead of reimplementing the ZIP path by hand.

**Thread safety note:** Not thread-safe against a concurrent call to the same function on another thread. Asset loading has been sequential in every log captured so far, but a real `hook_addr()` return-value swap (atomic pointer, not unhook/call/rehook) would be worth doing if that ever changes.

---

## `hook_getFileData` – Loose-First Path Resolution Logic

**Location:** inline comment inside `hook_getFileData()`

The requested filename sometimes already includes its resolution folder (e.g. `"Data_960_576/Logo/logo.png"`) and sometimes doesn't (e.g. the bare `"appConfig.txt"`). The hook tries two paths before falling through to the real engine function:

1. `DATA_PATH + filename` — the name as given.
2. `DATA_PATH + "Data_960_576/" + filename` — with the resolution folder prepended as a second guess.

Only absolute paths (those starting with `/`) bypass the loose-file attempt entirely.

---

## Fixed 1% Opacity and `hook_setControlsPos` for Hidden Virtual Controls

**Location:** `source/patch.c` (`controls_update_opacity`, `hook_setControlsPos`, `so_patch`)

### Issue & Root Cause

When on-screen touch controls are disabled by the user (`controlsVisible == 0` via `ACT_TOGGLE_HUD` combo / default), dying and reloading from a checkpoint via `GameOverLayer` caused the blue directional/movement buttons on the left of the screen to reappear, while the action buttons on the right remained hidden. The user had to press the HUD toggle twice as a workaround.

**Engine Behavior:**
1. Upon checkpoint restart (`GameOverLayer::buttonActivated` case 0), `GameScene::ReloadFromCheckPoint()` calls `ControlsLayer::reset(ControlsLayer::sharedControlsLayer())`.
2. `ControlsLayer::reset()` calls `ControlsLayer::setControlsPos(this, 0)`.
3. Inside `ControlsLayer::setControlsPos(this, 0)`, the engine repositions all controls and unconditionally invokes `CCNode::setIsVisible(true)` on the movement control pointers (`0x158..0x16c`: arrows, joystick, or slider).
4. `ControlsLayer::tick()` only refreshes visibility when `CCDirector+0xac != ControlsLayer+0x1cb`. Since `CCDirector+0xac` was already 0 and `field_1cb` remained 0, `tick()` never re-hid them.

### Solution

1. When controls are hidden, instead of relying solely on `CCNode::setIsVisible(false)` which gets clobbered on checkpoint reset, a fixed 1% opacity (`CONTROLS_OPACITY_HIDDEN`, `(unsigned char) 2`, ~0.78% alpha) is applied to all 12 virtual controls:
   - Sprites at `0x158..0x164` (slider base/knob, joystick base/stick) via `cocos2d::CCSprite::setOpacity`.
   - Menu items at `0x168..0x188` (left/right arrows, crouch, jump, interact, attack, defend, sheath) via `cocos2d::CCMenuItemSprite::setOpacity`.
2. `ControlsLayer::setControlsPos` (`_ZN13ControlsLayer14setControlsPosEb`) is hooked via `hook_addr()`. Whenever `setControlsPos` executes (e.g. during `ControlsLayer::reset()` after player death), the hook intercepts the return and immediately re-applies `controls_update_opacity(this, g_controlsVisible)`.
3. Because opacity is 1%, even though the engine sets `setIsVisible(true)` on checkpoint reload, the controls render at 1% opacity—making them completely invisible on screen while preserving touch polling and internal engine state.
4. When the user explicitly toggles controls back on via the controller shortcut (`ACT_TOGGLE_HUD`), opacity is set to 255 (100% visible).

