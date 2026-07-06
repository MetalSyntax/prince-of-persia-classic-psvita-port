# Bootflow Reverse Engineering Report — Phase 3 of the Portability Plan

Deliverable of **Phase 3** of `plan_portibilidad.md`. Decompiled `classes.dex` from the APK
(`original/Prince of Persia Classic 2.1.apk`) with `jadx` (installed via Homebrew) to read the sequence
Exact number of native calls that the real app makes before rendering the first frame. This replaces the
Phase 1 "reasoned from the symbols" assumption/order by the actual sequence of the Java code.

```bash
jadx -d /tmp/pop_decomp "original/Prince of Persia Classic 2.1.apk"
```

## 1. Actual command of `System.loadLibrary()` — fixes Phase 1

`org/ubisoft/premium/POPClassic/POPClassic.java` (the main Activity, subclass of
`Cocos2dxActivity`):

```java
static {
    System.loadLibrary("cocos2d");
    System.loadLibrary("cocosdenshion");
    System.loadLibrary("game_logic");
}
```

**The actual order is `cocos2d` → `cocosdenshion` → `game_logic`**, not `cocosdenshion` → `cocos2d` →
`game_logic` as deduced in Phase 1 just by looking at the dependency graph (`NEEDED`/`SONAME`). This
doesn't break anything because neither `libcocos2d.so` nor `libcocosdenshion.so` need each other (confirmed in the
Phase 1: neither of them has the other in their `NEEDED`), so any order between those two works
for `so_util` symbol resolution. But if at some point logic is added that depends on what
module is "registered first" (e.g. if you further investigate whether `JNI_OnLoad` from `libcocos2d.so`
makes an explicit `RegisterNatives` — see §4), **use this actual order**, not the inferred one:
`libcocos2d.so` → `libcocosdenshion.so` → `libgame_logic.so`.

## 2. Complete native initialization sequence

`POPClassic.onCreate()` → `super.onCreate()` (create accelerometer, `Cocos2dxMusic`, `Cocos2dxSound`,
`Cocos2dxBitmap`, dialog handler, ads views) → `setPackageName(getApplication().getPackageName())`
(defined in `Cocos2dxActivity.java`, called explicitly by the subclass). inside
`setPackageName()`, in this exact order:

```java
nativeSetPaths(apkFilePath, appInfo.sourceDir, device);   // 1
nativeSetPackageName(packageName2);                        // 2
nativeSetIsGoogleLauncherBuild(Config.isGoogleLauncherBuild());  // 3
GetConfig(apkFilePath, "DEVICE_SLEEP") // 4 (and more GetConfig, see §3)
...
nativeSetNumOfCPUCores(getNumCores());                      // 5
nativeSetDensityScaleValue(dScale);                         // 6
nativeSetDevicePixelsPerInch(metrics.ydpi);                 // 7
SetControlInVisible();  // only if config.hardKeyboardHidden == 1 // 8 (conditional)
```

And **separately, later**, when `GLSurfaceView` creates the GL surface (asynchronous life cycle, not
part of `onCreate`):

```java
// Cocos2dxRenderer.onSurfaceCreated():
nativeInit(this.screenWidth, this.screenHeight);
```

### Exact signatures (from `native` declarations in Java, confirm what was inferred in Phase 1)

```c
void nativeSetPaths(char* apkFilePath, char* apkSourceDir, char* device);  // 3 strings, not 1
void nativeSetPackageName(char* packageName);
void nativeSetIsGoogleLauncherBuild(bool isGoogleLauncherBuild);
bool GetConfig(char* apkFilePath, char* key);
void nativeSetNumOfCPUCores(int cores);
void nativeSetDensityScaleValue(float scale);
void nativeSetDevicePixelsPerInch(float ydpi);
void SetControlVisible(void);
void SetControlInVisible(void);
void nativeInit(int screenWidth, int screenHeight);   // is NOT (void), takes width/height
```

**Correction to Phase 1:** `nativeSetPaths` takes **three** strings, not just the base path as assumed.
This must be replicated in `source/main.c` (Phase 4): call with `(base_path, base_path, "PSVita")` or
reasonable equivalent values, since `appInfo.sourceDir`/`device` are probably only used for
logging or to decide special device cases (see special case `"GT-P1000T"` in the calculation
of `dScale`, irrelevant on Vita).

## 3. Actual configuration values of this build (resolved from `AndroidManifest.xml`)

```xml
<meta-data android:name="isCompletePackage" android:value="false"/>
<meta-data android:name="isGoogleLauncher" android:value="true"/>
```

With `isCompletePackage=false` and `isGoogleLauncher=true`, the branch that runs in the actual APK is:

```java
apkFilePath = Environment.getExternalStorageDirectory() + "/Android/obb/" + packageName2;
// = /storage/emulated/0/Android/obb/org.ubisoft.premium.POPClassic (a FOLDER, not the .obb itself)
```

That is, the engine receives the standard Android OBB **folder**, not the `.obb` file directly or
a folder of already extracted assets.

## 4. Open and important risk: the engine probably reads the `.obb` as a ZIP, not as a flat folder

Evidence:

- `nativeSetPaths` receives a folder that on Android **contains the `.obb` as a ZIP file**
  (`main.1.org.ubisoft.premium.POPClassic.obb`), not single files.
- `libcocos2d.so` has the string literal `.obb` and zlib `UND` symbols (`inflate*`, `deflate*`,
  `crc32`, `gzopen`/`gzread` — see Phase 1 report, §5), consistent with parsing a ZIP by hand
  (common pattern in 2012 to read assets from the expansion without extracting them, saving space on the
  device).
- But there are also (Phase 1, §6) 125 flat path literals prefixed with `Data/` — which is
  compatible with both "path inside ZIP" (ZIPs have identical internal paths:
  `Data/Animations/...`) as with "flat file path if someone pre-extracted".

**Cannot be solved with certainty with static analysis alone without disassembling the logic of
`CCFileUtils`/reading files on ARM.** It is a common pattern in many engines of that era to try
first `fopen()` directly on the compound path and, if it fails, drop to read inside the ZIP — if so,
extracting the assets to `Data/` as loose files (which is what was already done in Phase 2) would work without
more changes. If instead the engine **always** looks for and opens the `.obb` as ZIP without trying `fopen()`
flat first, it will fail against `ux0:data/popclassic/Data/...` as a loose files folder.

**Mitigation plan for Phase 4/9 (does not block continuing with Phase 4):**

1. First test (the simplest, we already have the assets like this): pass as the first argument of
   `nativeSetPaths` the path `ux0:data/popclassic/` (which contains `Data/` with loose files) and, with
   `source/reimpl/io.c` logging every `fopen()`/`open()` that the game tries, see in console if:
   - try `ux0:data/popclassic/Data/Animations/...` directly → **single assets work, no need
     do nothing else**.
   - try to open a `*.obb` file within that path → you have to decide between (a) generating a `.obb`/zip
     valid with the assets already extracted (with `zip -0` or similar, uncompressed for simplicity), and leave
     have the engine open it with its own zlib parser, or (b) intercept/hook in `source/patch.c` the
     specific function of opening the `.obb` (once identified by address with `so_symbol`/log of
     crash) to redirect it to flat file reading.
2. If a need for (b) appears, it is a punctual hook, not a rewrite: same pattern as
   `backstab-vita/loader/patch.c` (`hook_addr` on an internal `.so` function).

## 5. One more file is missing from the assets layout: `appConfig.txt`

`GetConfig(apkFilePath, key)` is native; both `libcocos2d.so` and `libgame_logic.so` contain the string
literal `"appConfig.txt"`. In the actual APK it is in `assets/appConfig.txt` (outside the `Data`/OBB folder,
directly at the root of `assets/`). The keys that the Java code queries against this file:
`DEVICE_SLEEP`, `ENABLE_FLURRY`, `ENABLE_APPCIRCLE`, `BUILD_FREEMIUM`, `ENABLE_CROSSPROMOTION`,
`ENABLE_APPRATER`, `SFR_OPERATOR`, `NOOK_BUILD`.

**Action for Phase 2 (already executed, needs to be tweaked):** copy `assets/appConfig.txt` from the APK to
`ux0_data/popclassic/appConfig.txt` (same level as `.so`, not within `Data/`, since
`apkFilePath` is the base folder, not the graphics data folder). In doubt about the risk of §4,
also copy it into `Data/appConfig.txt` — it's a text file of a few bytes, it doesn't cost
nothing to have it in both places until confirming in the console which route is used.

All the previous flags are from services that do not apply to Vita (Flurry analytics, AppCircle ads,
cross-promotion, app rater, SFR operator, Nook build). **Recommendation:** instead of replicating the content
real from the `appConfig.txt` of the APK, generate your own with all those flags in `false`/`0` so that the
corresponding code is not executed (avoids having to stube the classes `CCFlurryUtils`, `CCShareUtils`,
etc. identified in Phase 1 — if `GetConfig` returns `false` for all of them, the game never calls them).

## 6. Key codes that the game already knows how to handle (affects Phase 4/10 — control mapping)

`Cocos2dxGLSurfaceView.onKeyDown/onKeyUp` **only** forwards to the renderer (`handleKeyDown`/`handleKeyUp`,
which call `nativeKeyDown`/`nativeKeyUp`) these Android key codes; any other code
discard:

| KeyEvent | Code | Probable use |
|---|---:|---|
| `KEYCODE_BACK` | 4 | Return/exit |
| `KEYCODE_MENU` | 82 | Menu |
| `KEYCODE_DPAD_UP/DOWN/LEFT/RIGHT` | 19/20/21/22 | Menu movement/navigation |
| `KEYCODE_DPAD_CENTER` | 23 | Confirm |
| `KEYCODE_BUTTON_X` | 99 | Action (probable attack or jump) |
| `KEYCODE_BUTTON_Y` | 100 | Secondary action |
| `KEYCODE_BUTTON_L1` | 102 | Action/Left Trigger |
| `KEYCODE_BUTTON_R1` | 103 | Action/right trigger |

Note that there are **no** `BUTTON_A`(96), `BUTTON_B`(97) or `BUTTON_Z`(101) — the game was tested with a
gamepad that only exposed X/Y/L1/R1 (or the developer only mapped those). For Phase 4/10, the loader must
translate the Vita's D-Pad and physical buttons to exactly these codes via `nativeKeyDown`/
`nativeKeyUp`; The actual gameplay meaning of each button (what X vs Y does) can only be confirmed
testing in console, not by this static analysis.

## 7. Specific actions that this adds/corrects in `plan_portibilidad.md`

1. **Phase 2:** add `appConfig.txt` (your own, with all flags set to `false`) to the layout, in the root of
   `popclassic/` and, just in case, also inside `Data/`.
2. **Phase 4:** use the actual signatures from §2 for `nativeSetPaths` (3 strings) and `nativeInit` (receives
   `screenWidth`/`screenHeight`, is called in the "surface created", not in the general startup) and respect
   the full calling order of §2, not just "load the 3 `.so`s and be done".
3. **Phase 4/9:** empirically validate the risk of §4 (loose assets vs. `.obb` as ZIP) as soon as
   have a first build running with `io.c` logging enabled, before spending time on more
   manual reversing of `CCFileUtils` logic.
4. **Phase 4/10:** use the key code table in §6 for mapping the Vita's physical controls, instead
   of just mapping START→`KEYCODE_BACK` as the original draft did.