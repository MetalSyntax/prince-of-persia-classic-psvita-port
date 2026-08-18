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
