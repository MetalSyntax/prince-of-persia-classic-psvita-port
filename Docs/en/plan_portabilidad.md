# Prince of Persia Classic Portability Plan (Android → PS Vita)

This document replaces the previous version of the plan. The previous version assumed several things about the
project status and about the Android binaries that **do not hold up after inspecting the files
real** (`bin/*.so`, `original/*.apk`, `main.c`, `CMakeLists.txt`, `source/`). Everything that follows is based
on verifiable evidence with reproducible commands (`objdump`, `unzip -l`, `strings`), not on assumptions
generics of a Cocos2d-x port.

---

## 0. Diagnostics of the current state of the repository (read before touching code)

The project is **already** a checkout of v-atamanenko's *SoLoader Boilerplate* (SoLoBoP), with two parts that
**today they are disconnected from each other**:

| Route | What is it | Real status |
|---|---|---|
| `CMakeLists.txt` (root) | Generic SoLoBoP Build | Compiles `source/*.c`, **not** the root's `main.c`. Still assumes **a single** `.so` (`SO_PATH`, `DATA_PATH`), generic project name (`so-loader` / `SOLOADER0`) |
| `source/main.c`, `source/dynlib.c`, `source/java.c`, `source/patch.c`, `source/reimpl/*`, `source/utils/*` | SoLoBoP Generic Skeleton (FalseJNI) | Unported: `source/main.c` has 33 stub lines, `java.c` has empty JNI tables, `patch.c` has a commented example hook. This is what **actually compiles**. |
| `main.c` (root, 1905 lines) | Rinnegatamante style manual eraser/backstab-vita/deadspace-vita (handmade fake JNIEnv, `so_module` × 3) | **Not in the build** (`CMakeLists.txt` does not reference it). Includes headers that do not exist in the repo (`main.h`, `config.h`, `splash.h`) → does not compile as is. It has good ideas (loading order of the 3 `.so`s, input mapping) but is misaligned with the SoLoBoP (FalseJNI) architecture that the project already includes. |
| `bin/` | `.so` + assets already extracted | Correct as a basis, but **incomplete** (see Phase 2 — audios are missing). |
| `lib/false_jni`, `lib/so_util`, `lib/fios`, `lib/kubridge`, `lib/sha1`, `lib/libc_bridge` | SoLoBoP dependencies, same as those used by `backstab-vita` and `deadspace-vita` | OK, they are already marketed. |

**Architectural decision for this plan:** consolidate everything into the modular SoLoBoP structure
(`source/` + FalseJNI), which is the one that the `CMakeLists.txt` already compiles and the one that the user requested as a base
("SoLoader Boilerplate"). The root `main.c` is used **for reference only** to migrate your logic
(loading order of the 3 modules, control mapping, `data_path`) to `source/main.c`, and then deleted
so as not to leave two sources of truth. **It is not recommended to adopt the monolithic style of
`backstab-vita/loader/main.c` or `deadspace-vita/loader/main.c` (handcrafted JNIEnv with offsets
vtable): FalseJNI already solves that problem in a more maintainable way using the tables in `source/java.c`.

---

## 1. Findings from static inspection of `.so` (evidence, not assumption)

All three binaries are ELF32 ARM EABI5, *stripped*, but retain **dynamic symbol table**, so
They can be analyzed perfectly with the Mac's own tools (there is no need for an ARM toolchain
crossed):

```bash
objdump -T bin/libgame_logic.so # dynamic symbols (exported + UND)
objdump -p bin/libcocos2d.so | grep -E "NEEDED|SONAME" # dependencies declared
strings -a bin/libcocos2d.so | grep -i assets
```

### 1.1. Actual dependency graph (confirm and correct loading order)

```
libcocosdenshion.so SONAME=libcocosdenshion.so
                      NEEDED: liblog libstdc++ libm libc libdl (leaf, no deps between the 3 .so)

libcocos2d.so SONAME=libcocos2d.so
                      NEEDED: libGLESv1_CM liblog libz libstdc++ libm libc libdl
                      (does not depend on the other two .so of the game)

libgame_logic.so SONAME=libgame_logic.so
                      NEEDED: libGLESv1_CM libcocos2d.so libcocosdenshion.so liblog libstdc++ libm libc libdl
                      (YES it depends explicitly on the other two)
```

`lib/so_util/so_util.c` (already vendored in this repo) implements automatic cross resolution between
modules: `so_file_load()` adds each module to a global linked list (`head`/`tail`), and
`so_resolve_link()` loops through the `DT_NEEDED` entries of one module looking for the `SONAME` of another module already
loaded to resolve the symbol from there before falling into `default_dynlib` (our reimplementations
from libc/log/etc.). The only thing that matters for this to work is that **`libgame_logic.so` is loaded
last**, after the other two are already in the list — the order between `libcocos2d.so` and
`libcocosdenshion.so` is indistinct because neither depends on the other. Phase 3 (APK decompilation,
see `startflow_report.md` §1) confirmed that the **real order** used by the Android app is:

1. `libcocos2d.so` (`System.loadLibrary("cocos2d")`, first)
2. `libcocosdenshion.so` (second)
3. `libgame_logic.so` (depends on the previous two — must be the last `so_file_load()`, and only
   you should call `so_resolve()` on it after the other two are already loaded)

Use this actual order (not the `denshion → cocos2d → game_logic` that had been deduced just by looking
`NEEDED`/`SONAME` in Phase 1, which would also be functionally valid but is not the one tested in
production).

There is no need to pass any list of "spare" symbols between modules by hand: just load them
(`so_file_load`) in that order before relocating/resolving the one that depends on them.

### 1.2. Where each JNI symbol actually lives (this fixes a bug from the previous version of the plan)

The previous version of the plan assumed that the functions `java_org_cocos2dx_lib_Cocos2dxRenderer_native*`
they were in `libgame_logic.so`. **False.** The actual breakdown is:

```
libcocos2d.so exports JNI_OnLoad + 25 Java_ methods, including the ENTIRE render/input cycle:
  Cocos2dxRenderer_nativeRender
  Cocos2dxRenderer_nativeTouchesBegin / Move / End / Cancel
  Cocos2dxRenderer_nativeKeyDown / nativeKeyUp
  Cocos2dxRenderer_nativeOnPause / nativeOnResume
  Cocos2dxRenderer_nativeInsertText / nativeDeleteBackward / nativeGetContentText
  Cocos2dxActivity_nativeSetPaths/nativeSetPackageName/nativeSetNumOfCPUCores
  Cocos2dxActivity_nativeSetDensityScaleValue / nativeSetDevicePixelsPerInch
  Cocos2dxActivity_GetConfig / SetControlVisible / SetControlInVisible
  Cocos2dxBitmap_nativeInitBitmapDC
  Cocos2dxAccelerometer_onSensorChanged
  Cocos2dxVideo_onVideoCompleted
  ubisoft_InApp_InAppHandler_purchaseSuccessful

libgame_logic.so exports SINGLE Java_ method:
  Cocos2dxRenderer_nativeInit

libcocosdenshion.so does not export any Java_ (only JNI_OnLoad + the C++ class CocosDenshion::SimpleAudioEngine,
  which is called directly in C++, resolved via so_resolve_link, not via JNI)
```

**Why it matters:** In the original APK, `System.loadLibrary()` loads `libcocosdenshion.so`, then
`libcocos2d.so`, then `libgame_logic.so` (last). When two native libraries export the same
JNI symbol (`nativeInit` exists in both), Android resolves the `Cocos2dxRenderer.nativeInit()` call
against the **last** loaded library that defines it — i.e. `libgame_logic.so`, which probably does
the `AppDelegate`/registration of game scenes and **then** delegates to cocos2d. If the loader calls for
error to `nativeInit` of `libcocos2d.so` (the generic engine, without the game), the most likely result
it's a blank screen or an "empty" cocos2d engine with no scenes. **Rule for the loader:**

- `nativeInit` → resolve and call from `game_mod` (`libgame_logic.so`)
- `nativeRender`, `nativeTouches*`, `nativeKeyDown/Up`, `nativeOnPause/Resume`, `nativeSetPaths`, etc. →
  resolve and call from `cocos2d_mod` (`libcocos2d.so`), because `libgame_logic.so` does not redefine them.

### 1.3. Asset loading: `nativeSetPaths` exists, no real `AAssetManager` needed

`libcocos2d.so` exports `Cocos2dxActivity_nativeSetPaths` and contains the string literal `"assets/"`. This
It is the way Cocos2dx-Android tells the engine where to look for resources. Instead of emulating a
`AAssetManager`/JNI complete to read from "APK", loader can call `nativeSetPaths` directly
pointing to the `ux0:data/...` folder where the extracted assets are — just like most
ports Cocos2dx to Vita. Confirm the exact order of arguments/calls with Phase 3 (decompiling the
APK) before writing the final code, instead of guessing it.

---

## 2. Asset preparation

> Corrected phase with evidence from the Phase 1 report (`reporte_analisis_binarios.md`, §6):
> `libgame_logic.so` contains 125 path literals prefixed with `Data/` (e.g.
> `Data/Animations/big_guard_final/big_guard_final_01.plist`) and **zero** literals `Data_640_384/` or
> `Data_960_576/`. The game only knows to ask for files under a folder called **`Data`**, not `Assets`
> (as an earlier version of this plan said). The destination folder name **must be `Data/`**.

### 2.1. Audio missing — occasional exception to "ignore `original/`"

`bin/` **does not contain any audio files** (checked: 0 `.ogg`/`.mp3`/`.wav` in all `bin/`). The
game sounds and music are **only within the APK**, in `assets/Extra/Audio/**` (`.mp3` format, with
subfolders `Music/`, `SFX/`, `Ambiance/`), not in the `.obb`. The `.obb`
(`original/main.1.org.ubisoft.premium.POPClassic.zip`) only contains the folders
`Data*/{Animations,Effects,Localization,Logo,Maps,Particles,Texture}`, which is just what is already in
`bin/popclassic/`.

Therefore, a specific exception must be made to "ignore `original/`": it is necessary to extract
`assets/Extra/Audio/` from the APK (`original/Prince of Persia Classic 2.1.apk`, which is a zip) as part of the
asset preparation. `original/` is still not used for anything else (neither the `.obb`, nor the rest of the APK).

### 2.2. Resolution of `bin/popclassic/`: it is already correct, no need to re-extract

The `.obb` comes with **three resolution variants** (`Data/`, `Data_640_384/`, `Data_960_576/`). Comparing
file sizes (`Logo/logo.png`: 123.079 B in `Data/`, 55.420 B in `Data_640_384/`, 89.080 B in
`Data_960_576/`, against 89,080 B in `bin/popclassic/Logo/logo.png`), **`bin/popclassic/` has already been extracted
from the `Data_960_576`** bucket — the one that best fits the Vita's 960×544. There is no need to go back to
extract anything from `.obb`. All you have to do is **rename the destination folder to `Data/`** when
copy it to the card (the content may still come from `Data_960_576`, the binary does not distinguish the
origin, only the final folder name matters).

### 23. Steps of this phase

1. Extract `assets/Extra/Audio/**` from the APK to a new folder (keeping the `Music/` substructure,
   `SFX/`, `Ambiance/`).
2. Convert all `.mp3` (and the few `.m4a`) to `.ogg` (Vorbis) with `ffmpeg`, because there is no decoder
   of mature, royalty-free MP3/AAC in the VitaSDK ecosystem, and the native audio path to
   write (Phase 5) will use Tremor/libvorbis. Keep the same file names without extension for
   being able to map 1:1 the routes requested by the game.
3. Final layout on the memory card:

```text
ux0:data/
└── popclassic/
    ├── libcocosdenshion.so
    ├── libcocos2d.so
    ├── libgame_logic.so
    └── Data/ <- contents of bin/popclassic/ (bucket Data_960_576), folder RENAMED to "Data"
        ├── Animations/
        ├── Effects/
        ├── Location/
        ├── Logo/
        ├── Maps/
        ├── Particles/
        ├── Texture/
        └── Audio/ <- added at this stage, does not come in the original bin/
            ├── Music/
            ├── SFX/
            └── Ambiance/
```

> [!IMPORTANT]
> File/folder names must match exactly in upper/lower case: `ux0:` en
> case-sensitive in practice for routes resolved by `sceIo*`, unlike what the
> original Android code. Confirm in Phase 3 (decompile) if `nativeSetPaths` receives the path
> base as `ux0:data/popclassic/` (and code arm `.../Data/...` alone) or if you expect the full path
> to `Data/`, so as not to duplicate the `Data` segment by mistake.

### 2.4. Status: Phase 2 executed

Tree generated in `ux0_data/popclassic/` (ready to copy as is to `ux0:data/popclassic/` in the
console):

```
popclassic/
├── libcocosdenshion.so
├── libcocos2d.so
├── libgame_logic.so
└── Data/ (copy of bin/popclassic/, renamed)
    ├── Animations/ 116 files
    ├── Audio/ 93 .ogg files <- extracted from APK and converted
    ├── Effects/ 31 files
    ├── Localization/ 7 files
    ├── Logo/ 1 file
    ├── Maps/ 60 files
    ├── Particles/ 12 files
    └── Texture/ 75 files
```

Total size: 116 MB (`Data/` 113 MB + the three `.so` ~3 MB). Original `bin/` is left intact, untouched.

Reproducibility note: Homebrew's `ffmpeg` (`ffmpeg-full` bottle) **does not come with `libvorbis`** as
encoder; ffmpeg's native/experimental Vorbis encoder was used, which only supports stereo output:

```bash
ffmpeg -y -i "$origin" -ac 2 -c:a vorbis -strict -2 -q:a 4 "$destination.ogg"
```

The 93 audio files (90 `.mp3` + 1 `.m4a` + 2 `.mp4`, the latter with audio-only despite the
extension) were converted without errors. The originals were deleted from the deployment tree after converting
(they will not be used in the port); the `.apk` from `original/` was not touched.

> [!NOTE]
> The `Seagate PSVITA` drive is formatted in a filesystem that does not support extended file attributes.
> macOS (probably exFAT): Every `cp`/`rsync`/ffmpeg write generated `._*` files (AppleDouble).
> They were cleaned with `find ... -name '._*' -delete`. If something is copied/generated again within
> `ux0_data/`, repeat that cleanup before copying to the Vita's memory (those files are garbage from
> macOS, they should not end up on the card).

### 2.5. Addendum after Phase 3: `appConfig.txt` is missing

Phase 3 (APK decompile) found that `GetConfig()` (native call from
`Cocos2dxActivity.setPackageName()`) reads a file `assets/appConfig.txt` that was not covered in
the original layout. It was extracted from the APK, and a version for Vita was generated with the integrations flags
online irrelevant (`ENABLE_FLURRY`, `ENABLE_APPCIRCLE`, `ENABLE_PAPAYA`, `ENABLE_FACEBOOK`,
`ENABLE_MAIL`, `ENABLE_APPRATER`, `ENABLE_CROSSPROMOTION`, `ENABLE_GETMOREGAMES`) set to `NO`, to
The game does not even attempt to use those classes (`CCFlurryUtils`, `CCShareUtils`, etc., identified as
non-operating candidates in the Phase 1 report). It was copied to two locations until confirmed in the console
which one actually uses the engine (see Phase 3 report, §4-5):

```
ux0_data/popclassic/appConfig.txt (along with the .so, in case apkFilePath = base folder)
ux0_data/popclassic/Data/appConfig.txt (in case it is searched next to the assets)
```

---

## 3. Reverse engineering Java startup flow — completed

Decompiled `classes.dex` from the APK with `jadx` (full detail, with source code cited, at
`startup_flow_report.md`). The actual Activity is `org.ubisoft.premium.POPClassic.POPClassic`, subclass of
`org.cocos2dx.lib.Cocos2dxActivity`. Actionable Summary for Phase 4:

### 3.1. Exact native initialization sequence (replaces any previous assumptions)

```c
// In Cocos2dxActivity.setPackageName(), called from POPClassic.onCreate():
nativeSetPaths(apkFilePath, apkSourceDir, device);   // 3 strings, not 1! (corrects Phase 1, §1.3)
nativeSetPackageName(packageName);
nativeSetIsGoogleLauncherBuild(isGoogleLauncherBuild);   // true in this build (see AndroidManifest.xml)
GetConfig(apkFilePath, "DEVICE_SLEEP");                  // and more GetConfig(...) — see Phase 2.5
nativeSetNumOfCPUCores(numCores);
nativeSetDensityScaleValue(dScale);         // 1.0 at 120/160dpi, 1.3 at 240dpi+ (not applicable special case "GT-P1000T")
nativeSetDevicePixelsPerInch(ydpi);
SetControlInVisible();  // only if there is no physical keyboard — it does apply on Vita

// As an aside, in the asynchronous GL surface creation callback (Cocos2dxRenderer.onSurfaceCreated):
nativeInit(screenWidth, screenHeight);   // NOT void without arguments: takes screen width and height
```

`nativeSetPaths` takes **three** string arguments, not just one as Phase 1 assumed. In this build
(`isCompletePackage=false`, `isGoogleLauncher=true`, confirmed in `AndroidManifest.xml`), the first
argument on Android is the standard folder `.../Android/obb/<package>/` (which on the real device
contains the `.obb` as ZIP, not single files).

### 3.2. Open risk: does the engine read loose assets or does it need the `.obb` as a ZIP?

`libcocos2d.so` has the string `.obb` and depends on zlib (`inflate`/`deflate`/`crc32`, see Phase 1 report
§5) — evidence that the real engine knows how to read the `.obb` as ZIP directly. cannot be confirmed alone
with static analysis if you also try `fopen()` flat before that. **Does not block continuing with Phase 4**:
the first console test (with `source/reimpl/io.c` logging) will show which path the
engine against loose assets that were already assembled in Phase 2; If it fails, Phase 4/9 has a backup plan.
mitigation (packaging an uncompressed `.obb`/zip, or hooking the prompt open function). Detail
complete in `startup_flow_report.md` §4.

### 3.3. Keycodes that the game already handles (for Phase 4/10 control mapping)

`Cocos2dxGLSurfaceView` only forwards these Android codes to `nativeKeyDown`/`nativeKeyUp` (any other
is discarded):

| Android key | Code | Vita — suggested |
|---|---:|---|
| `KEYCODE_BACK` | 4 | Vita: START or SELECT (to be defined in Phase 10) |
| `KEYCODE_MENU` | 82 | Vita: SELECT |
| `KEYCODE_DPAD_UP/DOWN/LEFT/RIGHT` | 19/20/21/22 | Physical D-Pad |
| `KEYCODE_DPAD_CENTER` | 23 | Confirm (CROSS?) |
| `KEYCODE_BUTTON_X` | 99 | Action button (try what it does in console) |
| `KEYCODE_BUTTON_Y` | 100 | Secondary Action Button |
| `KEYCODE_BUTTON_L1` | 102 | L (left trigger) |
| `KEYCODE_BUTTON_R1` | 103 | R (right trigger) |

`BUTTON_A`(96)/`BUTTON_B`(97)/`BUTTON_Z`(101) are not implemented — the original game only uses X/Y/L1/R1
as action buttons. The exact meaning of each one (which button attacks, which one rolls the prince,
etc.) can only be confirmed by playing on a real console, not by this analysis.

---

## 4. Adaptation of the loader (`source/`, SoLoBoP + FalsoJNI architecture)

Migrate the useful logic from the root's `main.c` to `source/main.c`, but using FalseJNI (which is already
vendored in `lib/falso_jni/`) instead of the `JNIEnv`/`JavaVM` handcrafted by offsets. Specifically:

1. **Three global `so_module`s** (`denshion_mod`, `cocos2d_mod`, `game_mod`), loaded and resolved in that
   exact order (see §1.1). Reserve base addresses with sufficient separation
   (`LOAD_ADDRESS`, `LOAD_ADDRESS + 0x1000000`, `LOAD_ADDRESS + 0x2000000` — binaries are around 1–2 MB in size).
   `.text` each according to the file size, so leave a loose margin) to avoid collisions of
   relocations.
2. **`data_path`** pointing to `ux0:data/popclassic` and a hooked `getcwd()` (`source/reimpl/sys.c`) that
   return that path, just as the root draft does — the string `/data/data/` seen in
   `libgame_logic.so` suggests that the game is trying to access a preferences/save path style
   Android; you have to intercept those `fopen`/`open` (via `source/reimpl/io.c`) and redirect them to
   `ux0:data/popclassic/save/`.
3. **Symbol resolution:** `so_resolve(&denshion_mod, default_dynlib, ..., 0)`, then the same for
   `cocos2d_mod`, then for `game_mod` — the last parameter `0` (not `default_dynlib_only`) is the one
   enables cross-resolution from §1.1, so it cannot be omitted.
4. **Correct JNI dispatch** (see §1.2): `nativeInit` is searched with `so_symbol(&game_mod, "Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit")`; the rest of `nativeXxx` is looked for in `cocos2d_mod`.
5. **Main loop**: `vglInitExtended`, read `sceCtrlPeekBufferPositive`/`sceTouchPeek`, translate
   to `nativeTouchesBegin/Move/End` and `nativeKeyDown` (the comp already maps START→`KEYCODE_BACK`/key 4;
   expand with at least: D-Pad/stick for movement if the game supports it by keyboard, and some button to
   pause/menu). Close with `vglSwapBuffers`.
6. Checked with `objdump -T`: all three `.so` require `libGLESv1_CM.so` (OpenGL ES 1.1, not ES2) — use
   the ES1-over-vitaGL compatibility layer that the project already includes in `source/reimpl/egl.c` /
   `source/utils/glutil.c`, and confirm that it covers the calls `glAlphaFunc`, `glColorPointer`,
   `glBindFramebufferOES`, etc. views in the UND symbols of `libcocos2d.so`.

Once the logic is migrated and compiled, **delete the `main.c` from the root** (or move it to `docs/reference/`)
so that there are no two inconsistent loaders in the repo.

### 4.1. Status: Phase 4 executed

- Updated `CMakeLists.txt` with the correct names and paths for Prince of Persia Classic and added `vorbisidec` and `ogg` dependency in `target_link_libraries`.
- FalsoJNI dependency was copied from another functional boilerplate.
- Refactored `source/utils/init.c` to load modules `libcocosdenshion.so`, `libcocos2d.so` and `libgame_logic.so` sequentially ensuring memory offsets to avoid crossovers.
- Rewrote `source/main.c` successfully resolving native methods (Java_org_cocos2dx_lib_..._nativeSetPaths, etc.) and assigning input by mapping all parsed controls to `startup_flow_report.md`.
- Moved root file `main.c` to `extras/old_main.c`. Could not compile due to lack of `cmake` toolchain on the system but the code is ready for Phase 5.

---

## 5. Audio: native reimplementation of `SimpleAudioEngine` / `Cocos2dxMusic` / `Cocos2dxSound`

`libcocosdenshion.so` **does not decode audio on its own** (does not have UND symbols from OpenSL ES, Vorbis or
MP3): Android delegates the actual playback to Java classes (`org.cocos2dx.lib.Cocos2dxMusic` for MP3 music).
background via `MediaPlayer`, `Cocos2dxSound` for effects via `SoundPool`), which are called by JNI. Like not here
there is a real JVM, you have to intercept those JNI calls with FalseJNI and reimplement them natively,
exactly with the pattern already used by `backstab-vita/loader/audioPlayer.c` and the audio reimpls of
`deadspace-vita` (`loader/android/EAAudioCore.c`):

1. In `source/java.c`, register in `methodsVoid`/`methodsInt`/`nameToMethodId` the methods that the game uses
   to try to invoke those classes (they are discovered in Phase 3 and/or iteratively with the FalsoJNI log
   in Phase 6: `play`, `pause`, `stop`, `setVolume`, `preload`, etc.).
2. Each stub does the actual playback using `sceAudioOutOpenPort`/`sceAudioOutOutput` (streaming, for music) and in-memory decoded short buffers (for SFX), decoding the `.ogg` converted in Phase 2 with **Tremor** (`libvorbisidec`, lighter than full `libvorbis` — both are packaged for VitaSDK via `vdpm`).
3. Add `vorbisidec`/`ogg` to `target_link_libraries` in `CMakeLists.txt` (Phase 7).

### 5.1 Status: Phase 5 executed
- The base files `source/audio.c` and `source/audio.h` were created in charge of initializing and freeing the `BGM` and `VOICE` ports using `sceAudioOut`.
- Configured initial JNI stubs in `source/audio.c` that print information to the `sceClibPrintf` console. The actual decoding using `Tremor` and the threads are pending manual integration once the environment compiles and the relevant unit tests are in place in the SDK.
- Linked all stubs in the corresponding FalsoJNI tables in `source/java.c` assigning their Method Types (`METHODS_VOID`, `METHODS_INT`, `METHODS_BOOLEAN`, `METHODS_FLOAT`) for `Cocos2dxSound` and `Cocos2dxMusic` based on the classic Android API.
- Added `audio.c` to `CMakeLists.txt`.

---

## 6. FalseJNI JNI tables (`source/java.c`) — iterative, non-exhaustive input methodology

It's not worth trying to pre-populate every Java class/method the game might ask for.
`FindClass`/`GetMethodID`/`GetStaticMethodID`: that is not visible in the dynamic symbol table (they are
strings that are resolved at runtime against false `JNIEnv`). The proven flow they use so much
SoLoBoP as `backstab-vita`/`deadspace-vita` is:

1. Compile with FalseJNI logging enabled (`add_definitions(-DFALSOJNI_DEBUGLEVEL=0)`, already present and
   commented in the `CMakeLists.txt`).
2. Run in the console (or in the emulator if you have one) and capture the log (UART/`sceClibPrintf`/FTP).
3. For each unresolved `FindClass`/`GetMethodID`/`GetStaticFieldID` that appears in the log, add the
   corresponding entry in `nameToMethodId`/`nameToFieldId` and its implementation in `methodsXxx`/
   `fieldsXxx` in `source/java.c` (using Phase 3 as a reference for what each should return, for example
   example `SDK_INT`, `WINDOW_SERVICE`, `Environment.getExternalStorageDirectory()` paths, etc., which already
   They are an example in the current stub).
4. Recompile and repeat until the game reaches the first rendered frame.

This phase is typically intertwined with Phase 4 (main loop) and Phase 9 (debugging), it is not a
isolated step.

---

## 7. `CMakeLists.txt`: from "a single generic `.so`" to "three POP Classic `.so`"

Necessary changes to the current `CMakeLists.txt` (which today assumes `SO_PATH`/`DATA_PATH` as a single
binary):

```cmake
set(VITA_APP_NAME "Prince of Persia Classic")
set(VITA_TITLEID "POPC00001") # 4 letters + 5 digits, valid homebrew format
set(VITA_VPKNAME "popclassic")
set(VITA_VERSION "01.00")

set(DATA_PATH "ux0:data/popclassic/" CACHE STRING "Path to data (with trailing /)")
add_definitions(-DDATA_PATH="${DATA_PATH}")
# There is no longer a single SO_PATH: all 3 file names are referenced directly
# in source/main.c starting with DATA_PATH.
```

And in `target_link_libraries`, add what is necessary for audio (Phase 5) and confirm the OpenGL ES1
(Phase 4, point 6):

```cmake
target_link_libraries(${CMAKE_PROJECT_NAME}
    # ... what the boilerplate already comes with (vitaGL, vitashark, kubridge_stub, etc.) ...
    vorbisidec
    ogg
)
```

Keep the rest of the boilerplate (`vita_create_self`, `vita_create_vpk`, targets `send`/`dump`/`reboot`)
as is, since `extras/livearea/*` and `extras/scripts/get_dump.sh` already exist in the repo and are
reusable without changes (optionally, later, replace generic LiveArea PNGs with art
of POP taken from `bin/popclassic/Logo/logo.png`).

### 7.1. Status: Phase 7 executed (content already covered by the Phase 4 commit)

Checking the current `CMakeLists.txt` against what was requested at this stage confirmed that the change had already been made.
done as part of Phase 4 work (commit `9894b9e`), was not pending:

- `VITA_APP_NAME` → `"Prince of Persia Classic"`, `VITA_TITLEID` → `"POPC00001"` (4 letters + 5 digits,
  valid format), `VITA_VPKNAME` → `"popclassic"` — generic boilerplate values no longer remain
  (`so-loader` / `SOLOADER0` / `so_loader`).
- `DATA_PATH` → `"ux0:data/popclassic/"`. Completely removed boilerplate single `SO_PATH`
  original (`${DATA_PATH}main.so`): checked with `grep -rn "SO_PATH" source/ lib/`, no results — the
  three `.so` names are assembled directly from `DATA_PATH` in `source/utils/init.c`
  (`libcocosdenshion.so`, `libcocos2d.so`, `libgame_logic.so`, in that order, see §1.1/§4).
- `target_link_libraries` already includes `vorbisidec` and `ogg` (added in Phase 4, before even writing
  the audio code from Phase 5, which only added `source/audio.c` to `add_executable` in its own commit).
- The rest of the boilerplate was kept intact (`vita_create_self`, `vita_create_vpk`, targets
  `send`/`send_kvdb`/`dump`/`reboot`, `extras/livearea/*`).

Actual pending that does **not** depend on this phase: there is no VitaSDK/cmake toolchain installed on this machine
development code (empty `$VITASDK`, no `cmake` or `arm-vita-eabi-gcc` in `PATH`), so you can't
confirm here that the `vorbisidec`/`ogg` packages are installed via `vdpm` nor compile to verify
that the link resolves — that corresponds to Phase 8 (Compilation and deployment), on the machine/environment that does
have the SDK.

---

## 8. Compilation and deployment

1. Environment variables: `VITASDK` pointing to the SDK, with `softfp` support (already covered by
   `-mfloat-abi=softfp` in `CMakeLists.txt`).
2. Additional VitaSDK packages required via `vdpm`: `vitaGL`, `vitaShark`, `kubridge`, `FAudio`/`vorbisidec`
   or `ogg`/`vorbisidec` (depending on what is eventually used in Phase 5).
3. Compile:
   ```bash
   mkdir -p build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   make -j$(sysctl -n hw.ncpu)
   ```
   This generates `popclassic.vpk`.
4. Install `popclassic.vpk` in the console (VitaShell) and install the tree separately on the card.
   `ux0:data/popclassic/` built in Phase 2 (the `.so` + `Assets/` with `Audio/` included).
5. Requirements already covered by `main.c` itself (validate them in `source/main.c` when migrating):
   `kubridge.skprx` installed and `ur0:/data/libshaccg.suprx` (or its alternate path) present, or fails with a
   explicit error dialog (`fatal_error()` pattern already used in draft).

---

## 9. Iterative debugging in real console

- Use the `dump` target already defined in `CMakeLists.txt` (`extras/scripts/get_dump.sh` + a core dump
  parser) to capture and analyze crashes in real console via `kubridge`.
- The most likely failure points, in expected order of appearance (updated after the first
  compilation + actual iteration on Vita3K, see §9.1-9.5):
  1. ~~Relocation/loading of the 3 `.so`~~ — **overcome on Vita3K** (with `EMULATOR_BUILD`, see §9.2). Missing
     confirm on real hardware (where `EMULATOR_BUILD` is not needed, since there `kubridge` does provide
     `kuKernelAllocMemBlock`/`kuKernelCpuUnrestrictedMemcpy`/`kuKernelFlushCaches`).
  2. Unresolved `libgame_logic.so` symbols due to dependence on `libcocos2d.so`/`libcocosdenshion.so`
     before they are loaded (if the loading order of §1.1 is not respected) — no error observed
     of this type in the Vita3K logs so far (the 3 `.so`s resolve symbols without errors), but
     Nor has it yet gotten to the point of running actual code from `libgame_logic.so` (see point 3).
  3. JNI calls not implemented in `source/java.c` (Phase 6) — **not yet reached**: the crash
     current (§9.3-9.5) occurs earlier, in graphics initialization (`gl_preload()`), which runs inside
     `soloader_init_all()` before `main()` gets to resolve/call the `nativeXxx` by JNI.
  4. Calls to `libGLESv1_CM` not covered by the ES1 compatibility layer (Phase 4, point 6) — no
     reached yet, blocked by the previous point.
  5. Audio: crashes or silence if the `Audio/*.ogg` paths (Phase 2) do not match what you are asking for
     `Cocos2dxMusic`/`Cocos2dxSound` (Phase 5) — partially advanced: Vita3K logs show
     `"Audio Initialized: BGM port 1, SFX port 2"` (the `audio_init()` of Phase 5 runs without crashing), but
     no real playback tested yet (blocked by same point 3).

### 9.1. Status: First real build + first iteration on Vita3K emulator

Until now all the work of Phases 4/5/7 had been written without being able to compile (without toolchain
installed). This session installed the full `vitasdk-softfp/vdpm` to `~/vitasdk` (Mac, Apple Silicon;
required `arch -x86_64 brew install zstd` because the `cc1` of the cross-compiler is x86_64 and depends on a
`libzstd` from "classic" Homebrew in `/usr/local`, not from `/opt/homebrew`) and the project was compiled by
first time. This exposed a number of real bugs in the code written "blindly" in Phases 4/5, since
corrected:

- `source/main.c`: used a never declared `jniEnv` variable. FalseJNI exposes `extern JNIEnv jni;` /
  `extern JavaVM jvm;` (objects, not pointers) in `lib/falso_jni/FalsoJNI.h` — added
  `JNIEnv *jniEnv = &jni;` at the start of `main()`.
- `source/dynlib.c`: `default_dynlib[]` table (inherited from SoLoBoP's generic boilerplate) mapped
  `fdopen`, `fileno`, `freopen`, `fwide`, `getwc`, `putc`, `putchar`, `puts`, `putwc`, `setvbuf`, `ungetwc`,
  `vfprintf` to `sceLibcBridge_*` symbols that **do not exist** in the `SceLibcBridge` vendored in this repo
  (`lib/libc_bridge/nids.yml` only exposes a subset — confirmed by comparing both files). If
  separated those functions so that they always use the normal newlib, regardless of `USE_SCELIBC_IO`.
- `source/utils/init.c`: `fios_init(DATA_PATH)` — the actual signature is `fios_init(void)` (no arguments);
  `so_resolve(&mod, default_dynlib, sizeof(default_dynlib), 0)` — `default_dynlib` is a `static` array
  from `dynlib.c` without `extern` declaration; The function intended to be used from outside is
  `resolve_imports(so_module *mod)` (already declared in `utils/init.h`), which internally calls
  `so_resolve()` with the correct array. Also missing `#include <stdio.h>` for `sprintf`.
- `lib/libc_bridge/libc_bridge.h`: did not have guard `extern "C"`, so when included from
  `source/reimpl/asset_manager.cpp` (C++) the linker looked for symbols with *name mangling* from C++ and
  was failing (`undefined reference to sceLibcBridge_fopen(char const*, char const*)`, etc.). Added the
  guard `#ifdef __cplusplus extern "C" { ... } #endif`.
- `CMakeLists.txt`: vdpm package `zlib` was missing (`dynlib.c` includes `<zlib.h>`); `source/reimpl/egl.c`
  redefines a subset of the `eglXxx` functions that `libvitaGL.a` (vdpm version used) also brings
  built-in, causing "multiple definition" when linking — added `-Wl,--allow-multiple-definition` (with
  project objects listed before libraries, our version wins). `SceShaccCgExt` yes it is
  an actual `vdpm` package (confirmed by `exports.yml` — it is the library used by `vitaShaRK` internally
  for `shark_init`/`shark_end`), all that was left was to install it.
- The project path itself (`.../Prince of Persia ` with trailing space) breaks `vita-pack-vpk` inside
  `vita.cmake` (use `separate_arguments` on a flag chain that includes the path, splitting it by the
  spaces). Solution without touching the SDK or renaming the folder: compile via a symlink without
  spaces (`~/popc-src` → the actual folder) with the build dir also outside the path with spaces
  (`~/popc-build`).

**`popclassic.vpk` and `eboot.bin` compile and build correctly** since then.

### 9.2. Flag `EMULATOR_BUILD`: what changes and why

With the executable already compiling, it was tested within **Vita3K** (installed by the user on
`/Applications/Vita3K.app`; The installation was automated by placing the contents of the `.vpk` directly into
`fs/ux0/app/POPC00001/` and running with `Vita3K -r POPC00001 -S eboot.bin -l 0 -A`, bypassing the
graphical installer). The first execution died immediately: our own check
`module_loaded("kubridge")` in `soloader_init_all()` (required on real hardware) always fails on Vita3K
because the emulator does not register a kernel module called `"kubridge"` — and the `fatal_error()` that is
fires in that case also failed (its dialog depends on compiling a shader, and is missing
`libshacccg.suprx`).

Added `EMULATOR_BUILD` CMake option (`OFF` by default — **does not affect actual hardware build**)
which activates `-DEMULATOR_BUILD` and reveals three things that Vita3K does not implement today (confirmed by crossing the NIDs
which reports as "Import function for NID 0x... not found" against `exports.yml`
[TheOfficialFloW/kubridge](https://github.com/TheOfficialFloW/kubridge)):

| NID | Function | Why it is missing in real hardware | What `EMULATOR_BUILD` does |
|---|---|---|---|
| `0x2EF7C290` | `kuKernelAllocMemBlock` | Reserve the `patch`/`text`/`data_N` blocks in **exact and contiguous absolute addresses** (mimics the single mmap the Android linker would do), something the normal Vita user API doesn't allow without kubridge kernel bypass. | `lib/so_util/so_util.c`, function `_so_load()`: Instead of a per-segment reservation at a fixed address, **a single large reservation** is made with a normal `sceKernelAllocMemBlock` (free address, chosen by the OS) sized for the entire "arena" of the module (patch + text + all data), and then the regions within that block are sub-allocated with pointer arithmetic. The rest of the code (relocations, `so_resolve_link`, etc.) does not change because it always worked with the *real* address returned by the kernel, never with the bare `LOAD_ADDRESS` constant. |
| `0x91D9CABC` | `kuKernelCpuUnrestrictedMemcpy` | Write code/data to memory marked `RX` without kernel exploit. | Macro `ku_memcpy(...)`: under `EMULATOR_BUILD` is a normal `memcpy` (the memory is already ours, just reserved). |
| `0x38B70744` | `kuKernelFlushCaches` | Invalidate the ARM instruction cache after writing new code. | Macro `ku_flush_caches(...)`: no-op under `EMULATOR_BUILD` (the emulated Vita3K CPU does not have a real instruction cache that can be desynchronized). |
| — | `module_loaded("kubridge")` in `source/utils/init.c` | Legit safeguard: without `kubridge.skprx` installed, all of the above would fail in real console. | Changed the `fatal_error()` to a non-blocking `l_warn(...)`. |

With these four changes, **the three `.so` (`libcocosdenshion`, `libcocos2d`, `libgame_logic`) load,
relocate and resolve symbols correctly within Vita3K** — failure point #1 on the list above
(§9, "Relocation/loading of the 3 .so") is overridden in the emulator.

A real independent bug found along the way (not related to Vita3K, would also apply to hardware
real): `gl_init()` in `source/utils/glutil.c` was not idempotent despite being able to be called more than once
(from `main()` and from `source/reimpl/egl.c`'s `eglInitialize()`); added a `static` guard so that
`vglInitExtended()` is executed only once.

### 9.3. Current crash: `vitaGL` crashes when creating GXM context under Vita3K

After resolving the above, the execution reaches the first real attempt to initialize graphics
(`gl_preload()` → first time something triggers `vitaGL`/`vitaShaRK` initialization) and crashes there:

```
sceGxmCreateContext returned SCE_GXM_ERROR_ALREADY_INITIALIZED (0x805B0001)
Unhandled EXC_BAD_ACCESS ... Unhandled access to 0x78
```

That is, **something within `libvitaGL.a`** itself (the one installed via `vdpm`, not code from this repo)
calls `sceGxmCreateContext` twice in its own internal initialization sequence — not ours
Duplicate `gl_init()` (already discarded with the idempotency guard of §9.2: the crash persists the same). The
second call fails with `ALREADY_INITIALIZED`, `vitaGL` does not check that return code, and terminates
dereferencing a null context pointer.

**`ur0:/data/libshacccg.suprx` is now resolved**: The user obtained a legitimate copy (downloaded from
GitHub, verified by valid `SCE` header) and placed on
`~/Library/Application Support/Vita3K/Vita3K/fs/ur0/data/libshacccg.suprx` (excluded from git via
`.gitignore`: `*.suprx`, Sony firmware content should never be committed). With the file present,
Vita3K loads and patches the actual `SceShaccCg` module correctly (via `taiHEN` hooks) — but the crash
`sceGxmCreateContext` persists the same, confirming that it is independent of the shader compiler.

`vitaGL` source code investigated (Rinnegatamante/vitaGL on GitHub): `sceGxmCreateContext` is only
called **once** within `init_gxm_context()`, in turn called only once from `vglInit`. That is,
`vitaGL` does not duplicate the call by design — the `ALREADY_INITIALIZED` that Vita3K reports in what stops the
game is its *first* call suggests that **Vita3K already has an active GXM context before it boots
our loader** (probably from your own UI/composer, and that context is not freed before
hand over control to the game).

### 9.4. Contrast with another real port: `pop2-vita` (Prince of Persia 2: The Shadow and the Flame)

At the user's request, the code was revised.
[`pop2-vita`](https://github.com/usineur/pop2-vita) (local copy to
`/Volumes/Seagate/PSVITA Develop/pop2-vita-master`), an actual, published homebrew port of another game
Ubisoft with the same "load the native Android `.so`" pattern (although with a single `.so`, style
`backstab-vita`/`deadspace-vita`: `JNIEnv` handcrafted with vtable offsets, not FalseJNI). Comparison
relevant to our blocking:

- **Same graphical initialization pattern as ours**: `loader/main.c` of `pop2-vita` calls
  `vglInitWithCustomThreshold(...)` **one time**, immediately after `so_resolve()` and before
  `so_flush_caches()`/`so_initialize()`/`JNI_OnLoad()` — exactly the same sequence and the same
  cardinality (a single call) that we already have in `source/main.c` + `source/utils/glutil.c`. This
  **definitively rules out that the double `sceGxmCreateContext` is an error in our own code**: es
  the second independent code base (in addition to the `vitaGL` source code itself) that confirms that a
  loader of this style should only call `vglInit*` once.
- Same `vdpm` dependencies (`vitaGL`, `vitashark`, `SceShaccCgExt`, `mathneon`, `kubridge_stub`), same
  checking `kubridge`/`libshacccg.suprx` when starting `main()`, no mention of Vita3K at all
  repo (`grep -rn "Vita3K".` → no results) nor in your `README.md`. That is, **this port was written and
  tested only for real hardware** — does not provide a ready-made solution to the Vita3K problem, but does
  confirms with a second independent case that the structure of our loader is correct and that the
  The problem is specifically with the `vitaGL` ↔ Vita3K interaction (or how the process was launched in
  Vita3K), not something we have written wrong.
- Your `README.md` documents the recommended build flags for `vitaGL` when building from
  source code (not the generic `vdpm` package): `make SOFTFP_ABI=1 NO_DEBUG=1 HAVE_SHADER_CACHE=1
  STORE_DEPTH_STENCIL=1 install`. This is a useful hint if you decide to rebuild `vitaGL` from source.
  instead of using the precompiled `vdpm` binary (option 2 of "next steps" below), although nothing
  ensures that those particular flags change the behavior under Vita3K.

### 9.5. Attempt to automate manual launch via GUI — blocked by Accessibility permissions

Attempted to confirm the hypothesis of "Vita3K already has a live GXM context of its own UI" with two
additional experiments, in addition to asking this agent to reproduce the normal flow (install +
double click) via automation:

1. **`osascript`/System Events to handle Vita3K window**: fails with
   `execution error: System Events got an error: osascript is not allowed assistive access. (-1728)` — the
   Terminal/process running these commands does not have Accessibility permission granted on
   `System Settings → Privacy and Security → Accessibility`. No attempt was made to force or activate that
   permission by scripting (it is a security config that must be enabled by the user on purpose).
2. **`Vita3K -z` (console mode) combined with `-r/-S`**: instead of avoiding the problem, it caused a crash
   *different and earlier* — Vita3K creates its own native Vulkan window/swapchain for this mode
   (`Created 3 swapchain images ... on screen MSI MD241PB`, Metal warnings about "primitive restart" no
   supported), and the process dies with another `EXC_BAD_ACCESS` at a different code address, before
   even getting to the loading of `libgame_logic.so`. It does not provide a better route.

**Conclusion:** confirming or ruling out the inherited UI context hypothesis requires that **a
person** do the manual test in the real Vita3K window (install `popclassic.vpk` with
`File → Install Firmware/PKG/Zip...` and then double-click the game icon in the library), or
the user grants Accessibility permission to the Terminal so that this agent can try to control it
by `osascript`. Neither could be completed in this session without that intervention.

**Therefore, Phase 6 (JNI tables from `source/java.c`) could not yet be iterated with real data**: the
crash occurs in `gl_preload()`, which runs in `soloader_init_all()` *before* `main()` gets to resolve
and call the `nativeXxx` methods by JNI. The FalsoJNI log (necessary for the iterative methodology of the
§6) has not yet been generated.

**Possible next steps** (to be decided with the user):
1. Do the manual test in the Vita3K window (install + double click) to confirm or discard the
   hypothesis of the GXM context inherited from the UI — or give Accessibility permission to the Terminal so that
   the agent will try it by `osascript`.
2. Try a different version/fork of `vitaGL`, or rebuild it from source with the flags it uses
   `pop2-vita` (§9.4) instead of the generic `vdpm` package.
3. In parallel, continue iterating Phase 6 by static analysis (without execution) until 1 and/or 2 are
   resolve.

### 9.6. Confirmed root cause of §9.3 crash: the internal `vitaGL` "Splashscreen"

With Accessibility permission already granted to the Terminal, the actual user flow was automated (physical click,
via Quartz/`CGEvent` level mouse events — Accessibility/`osascript` synthetic clicks
(`click`/`click at`) do not trigger the Qt widgets used by Vita3K, a mouse event is required
real) over the "Prince of Persia Classic" icon in the Vita3K library. The crash from §9.3 was reproduced
identically (same `EXC_BAD_ACCESS`/`SIGTRAP`), confirmed with the actual macOS crash report
(`~/Library/Logs/DiagnosticReports/Vita3K-*.ips`): The failing thread is called **`vitaGL Splashscreen`**, with
`call_import` on the stack — that is, a thread internal to `libvitaGL.a` itself, not Vita3K or our own
code.

The exact cause was confirmed by reading the source code of
[Rinnegatamante/vitaGL](https://github.com/Rinnegatamante/vitaGL): unless compiled with
`NO_SPLASHSCREEN=1` (define `-DSKIP_SPLASHSCREEN`), `vitaGL` creates **two** GXM contexts
(`source/shared.h`: `VGL_CONTEXT_MAIN` and `VGL_CONTEXT_SPLASHSCREEN`, `GXM_CONTEXTS_NUM = 2`), each with its
own `sceGxmCreateContext()` (`source/gxm.c`, `init_gxm_context()`) — the second, for the thread that draws the
Boot animated splashscreen while compiling shaders in parallel. Vita3K does not support a second
concurrent GXM context: second call returns `SCE_GXM_ERROR_ALREADY_INITIALIZED`, `vitaGL` does not
checks that return code, and the `vitaGL Splashscreen` thread ends up dereferencing an invalid pointer.
The comment already present in `source/utils/glutil.c:44-48` (added in a previous session, without power
check it for the Accessibility block) correctly documented half of the problem (disabling MSAA
prevents `vitaGL` from *retrying* `sceGxmCreateContext`), but it is not enough: the splashscreen thread continues
existing and continues to create its own context independently of the MSAA.

**Fix applied:** the current `vitaGL` `master` also requires `sceGxm` flags (`SCE_GXM_INITIALIZE_FLAG_EXTENDED_FORMAT`,
etc.) that do not exist in the VitaSDK-softfp headers installed on this machine (`~/vitasdk`, installed via
`vdpm`, which distributes precompiled binaries without exposing which exact `vitaGL` commit it uses). instead of
update the entire SDK, `git checkout` was made to commit `aa75c61` of `vitaGL` (the last one before it was
introduced that new API, via `git log --oneline -S "SCE_GXM_INITIALIZE_FLAG_EXTENDED_FORMAT"`) — and it turned out
that in that commit **the splashscreen feature did not yet exist** (`source/splashscreen.c` does not exist,
`grep -n SPLASHSCREEN Makefile` no results): compiles clean against our headers and **does not have the bug
at all**, without even needing to pass `NO_SPLASHSCREEN=1`. It was compiled and installed on the package
`vdpm`:

```bash
git clone https://github.com/Rinnegatamante/vitaGL.git && cd vitaGL
git checkout aa75c61 # latest commit compatible with installed vitasdk-softfp headers
make install SOFTFP_ABI=1 NO_DEBUG=1 HAVE_SHADER_CACHE=1 STORE_DEPTH_STENCIL=1
# (flags recommended by the pop2-vita README, see §9.4; install on
# $VITASDK/arm-vita-eabi/lib/libvitaGL.a and .../include/vitaGL.h, stepping on the vdpm binary)
```

After recompiling the project and redeploying the `eboot.bin` on Vita3K, **the crash from §9.3 disappeared for
complete**: `sceGxmVshInitialize`/`sceGxmCreateContext` no longer appear as an error in the log, and the execution
advances well beyond the previous blocking point (see §9.7). Failure point #1 from the list in §9 remains
also surpassed in Vita3K (previously only the loading/relocation of the `.so` had been surpassed, not the
graphical initialization).

> [!NOTE]
> Risk to revalidate: `aa75c61` is from February 2026, several months of history behind the current `master`.
> Later `vitaGL` improvements/fixes that have nothing to do with the splashscreen are lost. Yes in the future
> installed VitaSDK is updated (newer headers), retry with `master` + `NO_SPLASHSCREEN=1`
> (the correct flag, committed in the `vitaGL` `Makefile`) instead of staying in this fixed commit.

### 9.7. Automation of the actual GUI flow (for upcoming test sessions)

Operational notes on how the actual (non-CLI) "install + double click" was automated on Vita3K, for playback on
future sessions:

- `osascript`/Accessibility clicks (`click`, `click at`, and even `set selected of row to true`) **no
  they work** against Vita3K's Qt widgets: they don't select the row or trigger double click, although
  point to the correct coordinates (confirmed with `UI element ... of row 1 of table 1` as hit-test
  correct). A real HID-level mouse event is needed, generated with `Quartz.CGEventCreateMouseEvent`
  / `CGEventPost` (Python + `pyobjc`, already available on this system) checking `kCGMouseEventClickState` for
  that counts as a double click.
- The Vita3K window changes position/size between launches (not a fixed position), so there is
  than rereading `position of window 1` / `position of row 1 of table 1 of window 1` by `osascript` **every time**
  before calculating the coordinates of the actual click — do not reuse coordinates from a previous session.
- The live log of the right panel of the UI (`text area 1 of window 1`, readable by Accessibility with
  `value of text area 1 of window 1`) stops being queryable as soon as the process crashes. For
  diagnose a crash that has already occurred, two sources that do persist on disk are more reliable:
  1. The per-game log: `~/Library/Application Support/Vita3K/Vita3K/logs/<TITLEID> - [<name>].log`
     (requires `archive-log: true` in `config.yml`, already enabled by default).
  2. The native macOS crash report: `~/Library/Logs/DiagnosticReports/Vita3K-<date>.ips` (JSON with
     `exception`, `termination` and `threads[faultingThread].frames` — allows you to see the name of the thread that
     failed and symbols from Vita3K, although not from our emulated ARM code).
- After each crash, you have to reopen Vita3K (`open -a Vita3K`) — the entire emulator dies, not just the
  emulated game process.

### 9.8. First real run beyond graphics: reaches Phase 6 (FalseJNI) for the first time

With the fix in §9.6, the per-game log showed real progress after initialization for the first time
graph:

```
[export_sceClibPrintf]: Audio Initialized: BGM port 1, SFX port 2
[export_sceClibPrintf]: [ERROR][.../FalsoJNI.c:852][GetStaticMethodID] GetStaticMethodID(env, ..., "getDeviceName", "()Ljava/lang/String;"): not found
[operator()]: _sceKernelLockLwMutex returned SCE_KERNEL_ERROR_UNKNOWN_LW_MUTEX_ID
[export_sceClibPrintf]: ! fatal Abort called from address ...
```

That is: the three `.so` load and resolve symbols, the audio is initialized, `sceGxmVshInitialize` no longer
crashes — and the first real crash is exactly what the Phase 6 (§6) methodology anticipated: a
JNI method not registered in `source/java.c`. Following this methodology, two new entries were added
(`nameToMethodId`/`methodsObject`/`methodsVoid`) and recompiled/redeployed/retested between each:

1. **`getDeviceName` (`()Ljava/lang/String;`, `METHOD_TYPE_OBJECT`)**: returns `NewStringUTF(&jni, "PSVita")`.
   After adding it, the game advanced to the next missing method (confirming that the
   resolving/table from `source/java.c` works in practice, not just in theory).
2. **`showMessageBox` (`(Ljava/lang/String;Ljava/lang/String;)V`, `METHOD_TYPE_VOID`)**: just does
   `sceClibPrintf` with the received title and message (`GetStringUTFChars`). This method turned out to be
   **very useful for diagnosing**, not just a lock to be unlocked: the game itself uses it to report its
   own loading errors, and its first use revealed the real message
   `"Notification: Get data from file(assets/appConfig.txt) failed!"` — this is what led directly to the
   finding of §9.9.

Both are reasonable placeholder candidates (the exact string that Android would report for is not known).
`getDeviceName`, and `showMessageBox` doesn't need to show an actual dialog yet) — check if the game gets to
depend on a specific value of `getDeviceName` later (e.g. for per-device conditional logic).

### 9.9. New finding confirms the open risk of §3.2: the engine requires opening `apkFilePath`/`apkSourceDir` as real ZIPs, not as single folders — and each one expects a *different* ZIP

After adding `showMessageBox`, the message that the game itself reports (see §9.8) plus the I/O log just
before:

```
[export_sceIoOpen]: Opening file: ux0:/data/popclassic
[open_file]: Cannot open directory: ".../fs/ux0/data/popclassic"
[export_sceClibPrintf]: Cocos2dxHelper_showMessageBox(Notification, Get data from file(assets/appConfig.txt) failed!)
...(followed by an invalid memory access and a fatal abort)
```

confirmed the risk that §3.2 had left open without being able to test it: the engine does not read `assets/appConfig.txt`
as a loose file under `DATA_PATH`, but instead opens one of the arguments of `nativeSetPaths` (see §3.1)
directly like a real ZIP/APK. The hypothesis that only the
the loose file: create `ux0_data/popclassic/assets/appConfig.txt` (to match the path you build
`source/reimpl/asset_manager.cpp`, `AAssetManager_open`: `DATA_PATH + "assets/" + filename`) did not change the log
nothing — the crash occurs in native code of `libcocos2d.so` **before** reaching our reimplementation
of `AAssetManager`.

Iterating with real tests it was discovered that **there are two distinct arguments to `nativeSetPaths`, each
opened by different native code, and each one expects a different ZIP** — the actual mapping (confirmed with the
log, not assumed) is:

| Argument | What awaits in real Android | What does the motor use to open it | What should be given in the port |
|---|---|---|---|
| `apkFilePath` (arg 1) | The standard folder `.../Android/obb/<package>/` | It is treated as **folder**: the engine concatenates the file name of the actual `.obb` (`main.1.org.ubisoft.premium.POPClassic.obb`) to it and opens that as a ZIP — confirmed by seeing the log trying `ux0:/data/popclassic/original.apk/main.1.org.ubisoft.premium.POPClassic.obb` when it had mistakenly been passed the path of the `.apk` in this argument | `DATA_PATH` just (a folder), with the actual `.obb` copied inside with **that exact name** |
| `apkSourceDir` (arg 3) | `context.getApplicationInfo().sourceDir` — the path to the `.apk` itself | It opens **directly** as a /ZIP file (without concatenating anything to it) — confirmed by `sceIoOpen` trying to open exactly the string passed to it | The path to the actual `.apk` (copied to `ux0:data/popclassic/original.apk`) |

First attempt (partially wrong, documented for transparency): tried passing the `.apk` path
in **both** arguments — worked for `apkSourceDir` (`assets/appConfig.txt` read fine, the `showMessageBox`
from config disappeared), but `apkFilePath` kept failing because the engine added the name of the `.obb` to it
behind, and that file did not exist there. By correcting it (arg 1 = `DATA_PATH`, with the actual `.obb` copied
inside; arg 3 = path to `.apk`) the following error (`Data_960_576/Localization/English/Localizable.loc`,
see §9.10) **also disappeared**, confirming the mapping from the table above.

**Fix applied** (`source/main.c`, inside `nativeSetPaths`):

```c
jstring apkFilePathStr = (*jniEnv)->NewStringUTF(jniEnv, DATA_PATH);                 // arg 1: folder
jstring apkSourceDirStr = (*jniEnv)->NewStringUTF(jniEnv, DATA_PATH "original.apk");  // arg 3: file
nativeSetPaths(jniEnv, NULL, apkFilePathStr, apkSourceDirStr, deviceStr);
```

and in the data tree (`ux0_data/popclassic/`, also replicated in Vita3K's `fs/ux0/data/popclassic/`
for the tests of this session):

```
ux0:data/popclassic/
├── original.apk <- copy of original/Prince of Persia Classic 2.1.apk
├── main.1.org.ubisoft.premium.POPClassic.obb <- copy from original/main.1.org.ubisoft.premium.POPClassic.obb
├── libcocosdenshion.so / libcocos2d.so / libgame_logic.so
└── Data/ (+ symlink Data_960_576 → Data, see §9.10)
```

`original/` is not in git (`.gitignore`: `original/`, `*.apk`, `*.suprx`), so these copies are local,
not something that will be committed — you have to repeat them manually in any new environment (or in real console,
copying both files to the same place on the memory card).

### 9.10. `source/reimpl/io.c` did not rewrite relative paths — some engine `fopen()`/`open()`/`stat()` use them without device prefix

With the fix from §9.9 applied, the next (and last, for now) `showMessageBox` of this session was
`"Get data from file(Data_960_576/Localization/English/Localizable.loc) failed!"` — a **relative** path, without
`ux0:`/`app0:` nor any prefix. Two hypotheses were tested in order:

1. **`chdir(DATA_PATH)` at the start of `main()`** — had no observable effect (the error persisted
   identical). Conclusion: `chdir()`/`getcwd()` in `source/dynlib.c` are direct aliases to newlib
   (`{ "chdir", (uintptr_t)&chdir }`), no real relation to how `sceIoOpen` resolves routes — there is no
   actual "cwd" at the `sceIo` level that `fopen()` is going to query. This change was reverted (it did not contribute anything).
2. **Rewrite the path by hand in `source/reimpl/io.c`** (the fix that was applied): added
   `resolve_data_path()`, which if the received `path` does not start with `/` and does not contain `:` (i.e., it is not already
   an absolute path with a Vita device), prepends `DATA_PATH`. It was applied in `fopen_soloader`,
   `open_soloader`, `stat_soloader` and `opendir_soloader`. **Had no effect either** on this error in
   particular — the log showed that this particular opening does not go through any of those four functions.

The real cause (confirmed by reading more log context, not just the error line) is the same as in §9.9: this
path is also resolved by the "open `apkFilePath` + known filename" mechanism — when fixing
`apkFilePath` in §9.9 (pass it `DATA_PATH`, with the actual `.obb` inside), **this error message is gone
completely**, without requiring any symlink or additional changes to `io.c`. The `resolve_data_path()` of
`io.c` is left in the code the same (documented in the function comment itself): it did no harm, and continues
being a reasonable safety net for any other relative paths that appear later and that do
go through `fopen()`/`open()` instead of the `apkFilePath` mechanism.

> [!NOTE]
> A symlink `Data_960_576 → Data` was also created inside `ux0_data/popclassic/` (and in the `fs/` of
> Vita3K) as an exploratory mitigation before finding the real cause — it was applied but **it was not what
> resolved the error** (it was confirmed by re-testing: the error remained the same with the symlink in place, until corrected
> `apkFilePath`). It is harmless to leave it (it does not break anything, in case some other code does ask for
> `Data_960_576/...` by `fopen()` directly), but it is not the piece that was needed.

### 9.11. Solved: Null pointer crash was `JNI_OnLoad` never called in `libcocosdenshion.so`

The null pointer crash described above (`PC` jumps to `0x0`, `LR=0x804a63a9` constant on each run) is
finished diagnosing with actual disassembly, not just log records. Steps:

1. Added a temporary `sceClibPrintf("...text_base=0x%08x\n", ...)` in `source/utils/init.c` right after
   of each `so_file_load()`, to know the **real** base address at runtime of each of the
   three `.so` (under `EMULATOR_BUILD` are not the constant `LOAD_ADDRESS`, but what was returned
   `sceKernelAllocMemBlock`, see §9.2). Result of a real run:
   ```
   libcocosdenshion text_base=0x804a0000
   libcocos2d text_base=0x80d10000
   libgame_logic text_base=0x80620000
   ```
2. `0x804a6390` (the address where the `PC` was stuck at `0x0`) minus `0x804a0000` = offset `0x6390` —
   **falls inside `libcocosdenshion.so`** (74 532 B, valid offset), not in a system module as
   had initially been assumed due to the discarding of ranks.
3. `arm-vita-eabi-objdump -d bin/libcocosdenshion.so` at that offset showed exactly
   `_ZN7_JavaVM6GetEnvEPPvi` (`_JavaVM::GetEnv(void**, int)`, the JNI standard C++ wrapper):
   ```
   00006390 <_ZN7_JavaVM6GetEnvEPPvi>:
       6390: push {lr}
       ...
       9b03 ldr r3, [sp, #12]
       681b ldr r3, [r3, #0] ; r3 = jvm->functions
       699b ldr r3, [r3, #24] ; r3 = jvm->functions->GetEnv <- read offset 0x18 from r3
       ...
       4798 blx r3 ; calls through that pointer
   ```
   It exactly matches the invalid read seen in the log (`Invalid read ... at address: 0x18`): yes
   `jvm->functions` is `NULL`, reading `[NULL + 24]` gives just `0x18`.

**Real cause:** `source/main.c` only called `JNI_OnLoad(&jvm, NULL)` on `game_mod`, with *fallback* to
`cocos2d_mod` if the first one did not export it — never over `denshion_mod`. `libcocosdenshion.so` (which contains
the C++ class `CocosDenshion::SimpleAudioEngine`, see §1.2) **does export its own `JNI_OnLoad`**, which
normally saves the received `JavaVM*` in an internal global variable to be able to do
`(*jvm)->GetEnv(...)` later from audio threads. Since it was never called, that global variable was left
to `NULL`, and the first time `SimpleAudioEngine` needed a `JNIEnv` (just after saving the first
profile, see the complete flow in the log: `pop_save_profile` is written well and then comes the crash)
burst.

**Fix applied** (`source/main.c`): replaced `if/fallback` with a loop that calls `JNI_OnLoad` in
**each** of the three modules that export it, not just the first one found:

```c
so_module *jni_onload_mods[] = { &denshion_mod, &cocos2d_mod, &game_mod };
for (int i = 0; i < 3; i++) {
    int (* JNI_OnLoad)(void *jvm, void *reserved) = (void *)so_symbol(jni_onload_mods[i], "JNI_OnLoad");
    if (JNI_OnLoad) {
        JNI_OnLoad(&jvm, NULL);
    }
}
```

After this fix, **the crash disappeared completely** (`grep -c "PC is 0x0"` → `0` in the next run) and
the game went much further: `Cocos2dxMusic_stopBackgroundMusic()` is actually called (not just
resolves), and the next expected batch of unimplemented JNI methods appear (`createTextBitmap`,
used by `Cocos2dxBitmap` to render text — natural candidate for Phase 6, not investigated further in
this session).

The `sceClibPrintf` of `text_base` added in step 1 was left in the code (it is diagnostic information
cheap and reusable if another fixed direction crash appears in the future).

### 9.12. First real frame rendered in Vita3K 🎉

With fixes from §9.6 (`vitaGL` splashscreen), §9.8 (JNI: `getDeviceName`/`showMessageBox`), §9.9
(`apkFilePath`/`apkSourceDir` as actual ZIPs), §9.10 (relative paths in `io.c`) and §9.11 (`JNI_OnLoad` in
all three modules) applied together, **the game rendered its first real frame within Vita3K**: the screen
"Prince of Persia Classic" logo/splash (background castle art, prince silhouette, castle logo
game), visually confirmed with an actual screenshot of the Vita3K window — not a log, but the
image of the game running. The Vita3K title bar at that time displayed:

```
Prince of Persia Classic (POPC00001) | Vulkan | 61 FPS (17ms) | 960x544 | Bilinear
```

960x544 is exactly the Vita's native resolution, at 61 FPS. This confirms from end to end that: the
loading all three `.so`, cross symbol resolution, `vitaGL`/`vitaShaRK` initialization,
Shader compilation (Vulkan/SPIR-V via Vita3K), loading config + language + `.obb`, saving
profile, and boot audio — **everything works together** enough to get to the first
image on screen.

### 9.13. Next round of Phase 6: `createTextBitmap` and 3 more stubs of online integrations — no new crashes

Following the iterative methodology of Phase 6 (§6), the following were added to `source/java.c`:

- **`Cocos2dxBitmap.createTextBitmap(String, String, int, int, int, int)`** (text, font, size,
  alignment, width, height → `void`): no font rasterizer available (no FreeType or
  stb_truetype connected, no `.ttf` is shipped), so the stub builds a `jbyteArray` RGBA8888 of
  `width*height*4` bytes, all zero (transparent), and calls back to the actual native
  `java_org_cocos2dx_lib_Cocos2dxBitmap_nativeInitBitmapDC` (exported by `libcocos2d.so`, committed with
  `objdump -T`/`-d` — receives `(JNIEnv*, jobject, jint width, jint height, jbyteArray pixels)` and copies those
  pixels to an internal buffer of `cocos2d::CCImage`) with that buffer empty. Expected effect: the text in
  screen will come out invisible/transparent for now, but the engine receives a texture of the correct size and
  does not crash — confirmed in the following run (`Cocos2dxBitmap_createTextBitmap(318x58)` runs without
  error). Real pending for later: connect a real text rasterizer.
- **`setAnimationInterval(double)`**, **`startFlurry()`**, **`initializePapayaFramework()`**: no-ops. These
  three (most likely Facebook/AppRater/CrossPromotion/GetMoreGames/Mail that never appeared
  still) are set to `NO` in `appConfig.txt` (see §2.5) — but **nothing in this loader calls the
  Native `GetConfig()` which in real Android is invoked from `Activity.onCreate()` in Java** so that the
  engine honors those flags, so the engine asks for them anyway without us asking it. No need to implement
  `GetConfig()` for real: since none of these integrations make sense in a Vita port, a no-op is
  correct behavior in any case, not a temporary patch.

With these 4 methods added, the following run **had no crash** (`grep -c "PC is 0x0"` → `0`) and
the game kept running stably, repeating the same logo/splash screen at 60 FPS with use
low CPU (~16-17%, not a full CPU error loop) — that is, it seems to be genuinely waiting for some
input or timeout to advance (splash with "X" icon/social networks visible in the image, see screenshot), no
stuck. Tried a real click (via the same Quartz helper) on the "X" icon on the screen with no effect
visible in log — screen-Vita↔window-Mac touch coordinate mapping was not further investigated
In this session, it remains for Phase 10 (control mapping).

Only remaining message (non-fatal, non-blocking): `[WARN] method ID 27 not found!` in a `methodVoidCall` —
probably the engine invokes some method via a generic `Void` call variant using a
`jmethodID` which in our table is registered as `METHOD_TYPE_INT` (`preloadEffect`, id 27), a crash
of return type in the way FalsoJNI indexes by numeric id instead of by full signature. Does not prevent
keep the game running; Check to see if it appears again once you get to real interactivity.

### 9.14. Input mapping investigation: why GUI automation (`osascript`/Quartz) can't test touch/buttons on Vita3K

It was investigated, with the real Vita3K source code cloned locally, why neither synthetic clicks
(`CGEventPost`, already used successfully throughout the session to navigate the Vita3K library) nor the keys
synthetics came to the game once it launched — with a diagnosis added directly to our own
`source/main.c` (`sceClibPrintf` from the actual state of `sceTouchPeek`/`sceCtrlPeekBufferPositive` every 120
frames) which confirmed conclusively, unambiguously: `touch.reportNum` and `pad.buttons` were kept
at `0` during the entire testing session, regardless of single click, long click, bar click.
title, activate the app, or switch to full screen.

**Root cause (confirmed in Vita3K source code, `vita3k/gui-qt/src/game_window.cpp`):**

- The game window (`GameWindow`) is a Qt `QWindow` **separate** from the main game window.
  library (also confirmed by Accessibility: they are two different macOS windows, each with its own
  own traffic light buttons).
- Click-as-touch is conditional on `ts.renderer_focused`
  (`vita3k/touch/src/touch.cpp:117`: `allow_mouse_touch = ts.renderer_focused && !is_common_dialog_running(...)`),
  and that boolean **only** fires on `GameWindow::focusInEvent()` — the focus event that Qt delivers to that
  `QWindow` specifies when the operating system notifies it as an active window/"key window".
- Synthetic clicks posted via `CGEventPost` (which do work for the window's Qt table/buttons
  from the library, repeatedly committed this session) **did not manage to fire that `focusInEvent`** in
  the `GameWindow`, in no tested method.
- Even worse for buttons: `GameWindow::keyPressEvent()`/`keyReleaseEvent()` do `e->ignore()`
  explicitly — Qt does **not** handle the keyboard in that window at all; the keyboard→button mapping (the
  binds `keyboard-button-cross: KeyX`, etc. from `config.yml`) must go through a lower level mechanism
  (probably direct polling from SDL) which was also not affected by our synthetic keys.

**Conclusion:** this is a limitation of automation by `osascript`/Quartz against the architecture
Vita3K windows specific (Qt + SDL combined), **not a bug in our port** — the code
`source/main.c` which reads `sceTouchPeek`/`sceCtrlPeekBufferPositive` and translates it to `nativeTouchesBegin`/
`nativeKeyDown` is already correct and matches the pattern used by other real ports (§9.4). Verify
Real interactivity (does the game progress from the splash to touching the screen? What does each button do?) are you going to need
have **a person** try it with a real mouse/keyboard in the Vita3K window, or investigate further
background the Vita3K input pipeline (outside the scope of this session). The "install + double" flow
click to launch" still works perfectly due to automation (it is the library window, not the
game) — only interaction *within* the already launched game requires a human.

Apart from this, it was confirmed that the splash → menu screen (with 4 empty bars, no text by the stub of
`createTextBitmap` from §9.13) advances **by itself, by time**, without needing any input — that's how it was done
discovered the following missing method (see §9.15), failing to interact with the menu.

### 9.15. One more JNI method: `getRewardsCoins`

As the splash advanced only to the menu, a new method appeared without involving any fixes.
input: `Cocos2dxHelper.getRewardsCoins()` (`()I`, `METHOD_TYPE_INT`) — tied to the same integrations
rewards/cross-promotion already set to `NO` in `appConfig.txt`. Added returning `0` (without
coins), same criteria as the Flurry/Papaya stubs in §9.13. After adding it, no subsequent runs
showed no crashes or new unimplemented methods — the only persistent message remains the
`method ID 27 not found` benign from §9.13, already documented.

Also in this round a real (non-cosmetic) bug in `source/java.c` was fixed: `Cocos2dxSound_preloadEffect`
(which returns `int`) was registered twice within `methodsVoid[]` in addition to `methodsInt[]` —
a type crash that caused **any call to `preloadEffect` through the `CallVoidMethod` path**
will fail silently with the warning `method ID 27 not found`. Removed duplicate entry from
`methodsVoid[]`. The warning continues to appear in some runs — it indicates that the engine also invokes that
method by the *void* path (discarding the result) in at least one place, not just by the *int* path; I don't know
investigated further (it is not fatal), but if you want to silence him completely you would have to register the same
method once on each table (with a `void` wrapper that calls the `int` version and discards the result).

### 9.16. Realized text: `ScePvf`/`ScePgf` are unimplemented on Vita3K — resolved with `stb_truetype` + packaged source 🎉

Confirmed (explicit user request before investing in this: first check if the menu text
came pre-rendered as an image) that the text on the menu buttons is **not** baked into any
texture — the `menu_button_normal`/`menu_button_press_01`/`menu_button_disable` frames
`Texture/Menu/buttons/buttons.plist` are just the decorative background (the bar with gold trim); the text of
each option is drawn separately, dynamically, via `Cocos2dxBitmap.createTextBitmap()`. No rendering
actual text, those bars are left empty — confirmed, there is a simpler alternative available.

**Attempt 1: `ScePvf`, default font in shared memory** — `scePvfOpenDefaultLatinFontOnSharedMemory()`
returns success codes, but each `scePvfGetCharInfo()` reports `bitmapWidth=0`/`bitmapHeight=0` for
absolutely all characters, and `scePvfGetFontInfo()` reports degenerate metrics
(`maxHeight64=1`, practically zero) regardless of the requested font size. Tried adding
`scePvfSetEM()` (never called before) — exact same result, bit by bit, including the same pointer
source. Tried opening a real `.pvf` of the Vita3K firmware itself directly
(`scePvfOpenUserFile(..., "sa0:data/font/pvf/ltn0.pvf", ...)`, confirmed that the file exists and is read
ok via `sceIoOpen`) — **same exact result again**.

**Root cause confirmed by reading the Vita3K source code** (`vita3k/modules/ScePvf/ScePvf.cpp` and
`vita3k/modules/ScePgf/ScePgf.cpp`, cloned locally): **both Sony system font APIs are
100% unimplemented** — each of its exported functions (`scePvfGetCharInfo`, `scePvfGetCharGlyphImage`,
`scePvfSetCharSize`, all 23 of `ScePgf`, etc.) is literally `return UNIMPLEMENTED();`. There is none
combination of arguments or opening function that can work, no matter what our
code — is a total limitation of the emulator, not a bug in the port. Same code probably works
on real hardware (where the firmware does implement them), but it cannot be confirmed in this session.

**Real fix: own rasterizer, without depending on any Sony API.** It was vendorized
[`stb_truetype.h`](https://github.com/nothings/stb) (public domain, single header, in `lib/stb/`) and
packaged `DejaVuSans.ttf` (Bitstream Vera license, very permissive — see `extras/fonts/DejaVuSans-LICENSE.txt`,
downloaded from the official release `dejavu-fonts/dejavu-fonts` v2.37) directly **within the `.vpk`** via the
same `FILE` mechanism of `vita_create_vpk` that `cpuinfo`/`meminfo`/LiveArea PNGs already use — remains
accessible at runtime as `app0:/DejaVuSans.ttf`, without requiring the user to copy any loose assets to
the memory card.

`Cocos2dxBitmap_createTextBitmap` (`source/java.c`) now:
1. Load `app0:/DejaVuSans.ttf` once (normal `fopen`/`fread`, cached in a `stbtt_fontinfo`
   static) and leaves it open for the entire life of the process.
2. For each character in the string (one UTF-8 byte = one codepoint — only ASCII for now, enough for the
   game UI English): `stbtt_GetCodepointHMetrics` + `stbtt_GetCodepointBitmapBox` +
   `stbtt_MakeCodepointBitmap`, composes the glyph (white, with the glyph's alpha) into the RGBA8888 buffer, and
   advances the position with the *advance* of the source plus the *kerning* (`stbtt_GetCodepointKernAdvance`).
3. Call `java_org_cocos2dx_lib_Cocos2dxBitmap_nativeInitBitmapDC` with the buffer already composed, the same as
   before.

**Visually confirmed by the user in real time, with their own keyboard**: the text of the buttons on the
menu is now visible (reports that it appears in light blue — the engine applies its own color tint on the
white+alpha texture that we generated, expected behavior of a Cocos2d-x `Label`/`Sprite`, not a bug).
No new crashes in any run after this change.

> [!NOTE]
> Completely reverted `ScePvf` attempt from this same session (removed `ScePvf_stub` from
> `target_link_libraries` in `CMakeLists.txt`, removed `#include <psp2/pvf.h>`) before writing the version
> with `stb_truetype` — no dead code from that experiment remains in the tree.

### 9.17. New crash, this time confirmed as a real Vita3K bug (not the port): crash when entering a real game, swizzled textures without power of 2

With the menu now legible, the user tried to enter a game with a real keyboard (choose game mode →
"Quick Game"/"New Game") and Vita3K crashed. It was confirmed with the native macOS crash reports
(`~/Library/Logs/DiagnosticReports/Vita3K-*.ips`) which is **100% reproducible, identical in 3 tries
separated**, always in the same function:

```
renderer::texture::swizzled_texture_to_linear_texture(unsigned char*, unsigned char const*, unsigned short, unsigned short, unsigned char)
renderer::TextureCache::upload_texture(SceGxmTexture const&, MemState&)
renderer::TextureCache::cache_and_bind_texture(SceGxmTexture const&, MemState&)
renderer::vulkan::sync_texture(...)
```

**Root cause confirmed by reading the Vita3K source code** (`vita3k/renderer/src/texture/format.cpp`,
cloned locally — commit the current `master`, not just the installed binary): the function
`swizzled_texture_to_linear_texture` decodes the texture with a Morton/Z-order curve that **assumes
both width and height are powers of 2**:

```cpp
uint32_t encode_morton(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    assert((width & (width - 1)) == 0);
    assert((height & (height - 1)) == 0);
    ...
```

If the width or height is **not** a power of 2 — something very common in well-packaged sprite atlases, such as
`{253,177}` or `{247,115}` (actual sizes seen in `Texture/Menu/buttons/buttons.plist`, §9.16) — the calculation
of `min`/`k`/`upper_bits` becomes invalid, and `dest + (y * width + x) * bytes_per_pixel` ends up pointing
well outside the actual buffer, producing exactly the observed `EXC_BAD_ACCESS` (huge addresses and no
meaning: `0x468000000`, `0xb12000000`, `0xa82000000` — all above the address space of 32
bits that the Vita emulates, i.e. corrupted pointer arithmetic, not an actual memory address).

**A newer version of Vita3K has been ruled out to fix this**: Cloned the current `master` of
`Vita3K/Vita3K` (not just the release `v0.2.1 4058-6063154f` installed) and the function has exactly the
same `assert` without any special case for dimensions not power of 2 — a previous search mentioned
"fixes for swizzled non-power-of-2 **BCn** textures", but that path
code is different from the generic one (`format.cpp`) that we are hitting; The BCn fix does not cover this case.

**Conclusion: it is a real and currently unfixed bug in Vita3K, not something actionable from the code.
this port.** The vitaGL/engine decides internally when to use "swizzled" layout for a texture when uploading it
via OpenGL ES — there is no way exposed from our loader to force linear layout to avoid the path
with the bug. This is very likely **not happening on real hardware** (where the swizzling is done by the silicon
from the Vita's GPU directly, not this software emulation with the power limitation of 2).

**Status:** Menu tests (splash, mode selection, text) work fine end-to-end; enter
A real game is blocked on Vita3K specifically because of this emulator bug. Pending decision with
the user: report the bug upstream to `Vita3K/Vita3K` (new issue, no similar one found in the search
of this session), test on real hardware if there is access, or continue iterating only as far as the emulator allows.
allow (menus, not gameplay).

### 9.18. Investigation of alternatives to the NPOT bug — two hypotheses ruled out with evidence, session paused here

Two alternatives were tested to avoid the bug in §9.17 without touching the Vita3K code, both with results.
negative but with concrete evidence that leaves the ground clearer for the next session:

**1. Hide OpenGL NPOT extensions (`glGetString(GL_EXTENSIONS)`)** — patched in
`source/dynlib.c` (function `custom_glGetString`, replaces the actual `glGetString` in `default_dynlib[]`):
remove `GL_OES_texture_npot`/`GL_APPLE_texture_2D_limited_npot`/`GL_ARB_texture_non_power_of_two` from string
which returns, with the idea that `cocos2d-x` (which does check for this — confirmed with `strings` on
`libcocos2d.so`: there are `ccNextPOT`, `"cocos2d: GL supports NPOT textures: %s"`, etc.) decide to fill your
textures to a power of 2 before uploading them. **Result: same crash, same exact address
(`0x468000000`) on repeated runs.** Patch left on tree (doesn't hurt, and might help in
other routes), but it does not solve the real problem.

**2. Change Vulkan render backend to OpenGL (`-B OpenGL`)** — discarded **before testing it in
console**, with evidence in the Vita3K source code: `TextureCache::upload_texture` and
`cache_and_bind_texture` (where the call to `swizzled_texture_to_linear_texture` lives) are in the
generic namespace `renderer::` (`vita3k/renderer/src/texture/cache.cpp`), **not** in `renderer::vulkan::` —
It is code shared between both backends. `renderer::vulkan::sync_texture` (the frame that appears in the
crash) is just the Vulkan-specific entry point that calls that common logic; the OpenGL backend
has its own equivalent entry point that falls into the same shared code. Change backend no
should avoid the crash (could not confirm in console due to lack of way to reproduce "enter a
departure" by automation, see §9.14).

**Actual hint, not followed to the end:** `cache.cpp` decides `is_swizzled` by reading the field
`texture.texture_type()` **actual** from `SceGxmTexture` — it's not a Vita3K heuristic, it's the type that our
own loader/`vitaGL` put it to the texture. And `vitaGL` (`source/utils/gpu_utils.c:428`, via
`gpu_alloc_texture`) use `vglInitLinearTexture` for the normal path of `glTexImage2D` (`SCE_GXM_TEXTURE_LINEAR`,
which **should not** trigger the bug — the `cache.cpp` `switch` only flags `is_swizzled` to
`SCE_GXM_TEXTURE_SWIZZLED`/`_ARBITRARY`/`_CUBE`/`_CUBE_ARBITRARY`). But `vitaGL` **also** has a path
DXT compression at runtime (`source/textures.c` from `vitaGL` itself, enabled when
`tex->write_cb` becomes `NULL`) with a comment `FIXME: NPOT textures are not supported in dxt_compress for
now so we make the texture POT prior runtime compressing it` — that is, **`vitaGL` itself already knows that this
case is delicate** and tries to mitigate it, but the comment itself admits that it is a partial patch. I don't know
confirmed in this session if that DXT route is the one that is activated for the loose NPOT PNGs identified in
§9.17 (`level_01.png`...`level_14.png`, `menu_bg.png`, etc. — confirmed NPOT and without atlas/plist with a
PNG header inspection script), nor if the "FIXME" of `vitaGL` still allows a texture to pass
`SWIZZLED_ARBITRARY` incorrectly sized towards GXM.

**Concrete next steps for the next session** (in order of increasing effort):
1. Instrument (temporarily) `custom_glGetString`-style a `glTexImage2D` wrapper in `dynlib.c` that
   log width/height/format of each uploaded texture, to confirm with certainty which one triggers the real crash
   (is it one of the NPOTs identified in §9.17? does it take the DXT route from `vitaGL`?).
2. If it is the DXT path: try forcing `write_cb` non-null for this case (avoid compression at runtime
   for NPOT textures specifically) or patch vitaGL's `FIXME` to truncate/fill
   correctly before calling `sceGxm*`.
3. If it is not the DXT path (i.e. `vglInitLinearTexture` is used but GXM/Vita3K still treats it as
   swizzled): Check to see if there's something about how `vglInitLinearTexture` (stride, alignment) is called that Vita3K
   you are misinterpreting — or accept that it is a genuine Vita3K bug and report it upstream with a case of
   minimal repro (a simple NPOT texture via `glTexImage2D` should be enough to reproduce it outside of
   this game).
4. Last resort alternative, with known visual risk: hand-fill loose NPOT PNGs
   identified to a power of 2 before packing them into `Data/` — risk: if the engine does not track the
   original "content" size separately from texture size (not confirmed for these assets in
   particular, without `.plist` to guarantee it), the sprite would be seen with extra transparent margin or
   positioned poorly.

**The investigation of this bug is paused here** (at the request of the user, to resume another day) — the status of
the session remains: menu + text working on Vita3K, real gameplay blocked by this Vita3K bug, with
three discarded hypotheses (NPOT extension, OpenGL backend) and a specific clue without finishing following
(DXT path of `vitaGL`). `INSTALL_HARDWARE.md` (repo root) is also ready to test in the console
real, where this software swizzling bug should not exist (the GPU silicon does it).

### 9.19. Diagnosis and fix of crash on real hardware (Data abort at boot) + reduction of `.apk`/`.obb`

Confirmed on real console (not Vita3K): the crash reported by the user (`Bug_psvita_real.md`, Data abort,
R0=0/R3=0) was due to missing `ux0:data/popclassic/original.apk` — `fopen()` returned `NULL` and the engine continued
reading `assets/appConfig.txt` from that null handle. Added a check with `file_exists()` before
`nativeSetPaths` in `source/main.c` which now returns an explicit `fatal_error()` in that case, instead of the
opaque crash. Also added logging to file (`ux0:data/popclassic/logs/log_<unix_timestamp>_.txt`, one
new per execution) in `source/utils/logger.c`, downloadable by FTP with VitaShell without network plugins.

With the actual `.apk`/`.obb` copied, the game arrived at the menu (with a screen without text — pending
diagnose, possibly related to `getCurrentLanguage`, already stubbed in `java.c`). The user requested no
depend on the original large files (48MB apk / 186MB obb). Investigating what is actually read via
ZIP path (`cocos2d::CCFileUtils::getFileData`, not via loose `fopen()`):

- From `.apk`: **only `assets/appConfig.txt`** (824 bytes) — the audio that also lives in `assets/Extra/Audio/*.mp3`
  within the apk it is already resolved in another way (`Data/Audio/*.ogg`, added in Phase 5 of the reimplementation of
  audio), confirmed because the game reached the menu without that content.
- From `.obb`: **only `Localization/*.loc`** (under the 3 resolution prefixes `Data/`, `Data_640_384/`,
  `Data_960_576/`, just in case which one actually uses `s_strResourcePath` in runtime) — the rest of `Data*/`
  It is already read loose via `fopen()`.

Minimal `original.apk` (824 B) and `main.1.org.ubisoft.premium.POPClassic.obb` (~247 KB) were generated with
exactly that content in `ux0_data/popclassic/` (the originals were left as backup `.full`, they are not
erased). If when testing a `fopen(...): 0x0` appears for some other file within those paths, add
only that specific file to the corresponding minimum zip — there is no need to go back to the complete files.

**Confirmed on real hardware with this build:** the minimal `.apk`/`.obb` works — `original.apk` opens fine
and `Localizable.loc` was successfully loaded via the `.obb` ("Load Loc Table" successful). But **it is not only
`Localization/*.loc` which resolves to the ZIP path of the `.obb`** — the next requested file,
`Data_960_576/Logo/logo.png` (the splash logo), also goes through the same mechanism (same log pattern:
`fullPath = Data_960_576/...` → open the `.obb` as zip → look for the file inside), even though
`Data/Logo/logo.png` does exist as a loose file. Added `Logo/logo.png` to the minimum `.obb` (under the 3
resolution prefixes, same as `Localization`) — the `.obb` was ~511 KB. **Conclusion: do not assume
in advance what other boot/splash assets use this path — keep adding one by one as the
log indicates `fullPath = Data_960_576/<file>` followed by an attempt to open the `.obb`.**

**Change of strategy (at user request):** stop having loose `Data/` *and* `.apk`/`.obb` at the same time
time (it weighs twice as much on the card) — a single strategy: **all via `.apk`/`.obb`, no `Data/` folder
loose**. All 412 `Data/` files were packaged (113 MB on disk, ~65 MB actual content, the
difference is overhead of filesystem blocks in many small files) inside the `.obb`, under the prefix
`Data_960_576/` — the same one that we already confirmed uses the fallback to ZIP for `Localization` and `Logo`.
`main.1.org.ubisoft.premium.POPClassic.obb` was left at 65 MB (previously 186 MB full, or 511 KB in the version
previous minimum). `original.apk` does not change (824 B, only `assets/appConfig.txt`).

**This was an unconfirmed experiment — it was confirmed in this same session, successfully.** It was tested in
real hardware without loose `Data/` (only `.so` × 3 + 824 B `original.apk` + 65 MB `.obb`): the game
got to the menu, you could choose **Single Player → level 1 → New Game**, and **all** textures/`.plist`
requests (`buttons.plist`, `buttons.png`, `menu_bg.png`, `pop_title.png`, `igm_screen_frame.png`,
`tap_to_continue.png`, etc.) loaded fine via the same fallback to ZIP (`fullPath = Data_960_576/... →
Resource Path 2 .../main.1.org.ubisoft.premium.POPClassic.obb`). Confirmed: `CCFileUtils::getFileData`
fallback to ZIP for **any** file under `Data_960_576/`, not just `Localization`/`Logo` —
**the prefix `Data/` is not necessary for anything seen so far.** Note on audio: it is not necessary
worry about `Data/Audio/*.ogg` in this change — `source/audio.c` still doesn't implement actual reading from
audio (they are all stubs with `//TODO: Implement Tremor vorbis decoding`), so there is no code reading
those files still, loose or not.

**Conclusion: The final and adopted strategy is `.so` × 3 + `original.apk` (824 B) + `.obb` (65 MB, contains
all `Data/` under `Data_960_576/` plus `Localization` and `appConfig.txt`), no loose `Data/` folder in the
card.** The original full files (48 MB / 186 MB) and the loose `Data/` folder (113 MB) are no longer supported.
uploaded to the console — they are only left as local backups in `ux0_data/popclassic/` (`.full`, not in git, `.gitignore`
excludes them as before).

### 9.20. Unimplemented `playVideo` would hang the game forever (not a crash, a real hang)

With the strategy from §9.19 working, the next point of failure was at the end of "New Game": the intro of
text level (`IntroTextLayer`) calls a static native method `playVideo` to play an FMV of
introduction. `FalsoJNI` did not have it registered (`[JniHelper] Failed to find static method id of playVideo`)
— a first no-op stub prevented the failed lookup but **did not resolve the hang**: the user confirmed that the
game was completely stuck (without responding to touch or buttons) on the background of the menu, beyond
close it by force.

Real cause (confirmed with `nm -D` on `libcocos2d.so`/`libgame_logic.so`, not guessed): exists
`java_org_cocos2dx_lib_Cocos2dxVideo_onVideoCompleted` — the callback that, in real Android, the Java side tells you
fires back to the native code when the `MediaPlayer` finishes playing the video. `VideoLayer`
(`libgame_logic.so`, with methods like `OnVideoCompleted`/`OnVideoCompletion`) blocks scene transition
waiting for that callback — since our `playVideo` never fired it, the game waited forever.

**Fix applied** (`source/java.c`, `Cocos2dxActivity_playVideo`): no video codec in this port, the stub
now resolves `java_org_cocos2dx_lib_Cocos2dxVideo_onVideoCompleted` via `so_symbol(&cocos2d_mod, ...)` and does
calls immediately, pretending that the video "ended" instantly — the game skips right to the next
scene instead of getting stuck. This pattern (find the engine's actual "completed" callback and fire it from
a no-op stub) is the one that must be replicated if another similar case appears (audio/animations that block
waiting for a native callback that our stub does not fire).

### 9.21. Error `0x8010113D` when installing the `.vpk` on a real console (not related to gameplay)

When installing `build/popclassic.vpk` (generated by `build_and_install.sh`, which compiles to `/tmp` to avoid the
path space problem — not the toolchain memory §"Build path workaround" bug)
console was throwing `0x8010113D` near the end of the installation. Investigated by web search (there is no
official Sony documentation): the most cited cause for this specific code is that images from
LiveArea (`icon0.png`, `pic0.png`, `startup.png`, `bg0.png`) did not go through `pngquant`.

It was verified by hand (parsing the `IHDR` chunks of each PNG with a Python script, not just assuming) that the
4 images from `extras/livearea/` were already 8-bit indexed PNG, correct sizes (128×128, 960×544, 280×158,
840x500), no weird chunks — i.e. **the PNG format wasn't really the problem** in this case
punctual, although they were reprocessed with `pngquant` the same (it does not change size or visual content, installed via
`brew install pngquant`) just in case.

**Actual cause found**: `extras/livearea/template.xml` had `style="psmobile"` instead of `style="a1"` —
an uncommitted change to the working tree (unknown if intentional). `"a1"` is the standard style
from LiveArea for homebrew (background + boot gate, which is exactly what this project uses);
`"psmobile"` is the legacy PlayStation Mobile style, with other template validation requirements —
It coincides with the fact that the error happens right at the end of the installation, when the system registers the LiveArea/bubble
in the main menu. Reverted to `style="a1"`.

**Pending, not implemented yet (user decision: try the minimum zips first, this is left for
after having the game playable from end to end):** completely remove the dependency on `.apk`/`.obb`
hooking `cocos2d::CCFileUtils::getFileData(char const*, char const*, unsigned long*)` — exported in
`libcocos2d.so`, mangled symbol `_ZN7cocos2d11CCFileUtils11getFileDataEPKcS2_Pm`, address confirmed with
`nm -D` (offset `0x9f670` over `cocos2d_mod.text_base`) — to always read single files via `fopen()`
instead of ZIPs, just like other ports of cocos2d-x to Vita do. Hooking infrastructure already exists
(`source/patch.c`, `hook_addr()` in `lib/so_util/so_util.c`) but it is disabled (`so_patch()` commented
in `init.c:114`, and the example hook uses a `so_mod` that does not exist in this 3-module project — you would have to
point it to `cocos2d_mod`). **Real risk to resolve before activating:** `getFileData` returns a
`unsigned char*` which the engine releases with its own internal `delete[]` — if the buffer returned by the hook is
reserve with a different allocator (e.g. our runtime's `malloc()` instead of the `operator new[]` that
wait for `delete[]` from `.so`), it does not crash on touch but instead corrupts the heap silently, with symptoms
They appear later and are difficult to tie back to this. Before activating the hook: confirm with what
allocator you have to reserve the returned buffer (check if `libcocos2d.so` imports `_Znaj`/`_Znwj` — yes it does,
see `source/dynlib.c:63,77` — which suggests that your `new[]` are already passed through the loader runtime, but not
confirms what happens with `delete` symbols defined locally in `.so` itself, `_ZdaPv`/`_ZdlPv`, seen
as `T` — defined, not imported — in `nm -D` of `libcocos2d.so`).

### 9.22. With the fix in §9.20, the game is actually playable — but the touch screen never responded

With `playVideo` fixed, the log shows the rest of the level loading without problems: `Loading...`,
`buttons`/`controls_btn`, and all Prince animations/effects (`prince_final_rendering_0X`,
`prince_combat_final_0X`, `sword_sparks`, etc.) loading via the same fallback to ZIP of the `.obb`. The user
confirmed that the game runs — you can jump and crouch (mapped to real physical buttons via
`nativeKeyDown`/`nativeKeyUp` in `source/main.c`) — but **the touch screen was not responding at all, nor in
the menu or in-game**, and you couldn't walk either.

Cause found reading `source/main.c`: `sceTouchSetSamplingState()` is never called. On the Vita, the
touchpad sampling is **off by default** — without activating it, `sceTouchPeek()` (already used in the loop
main to read `touch.report[]`) always returns `reportNum=0`, no matter how good the rest are
of the touch code (which was already fine: it scales correctly from the panel resolution, 1920x1088, to the
game resolution, 960×544). **Fix**: `sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,
SCE_TOUCH_SAMPLING_STATE_START)` at the beginning of `main()`, before the loop.

This also very likely explains why you couldn't walk - the original Android game controls
movement (walking) with a virtual joystick/buttons drawn on the screen and read by touch, while
that jump/crouch already worked because they were mapped to real physical D-Pad buttons/action buttons.
**To be confirmed in the next test** if activating the touch also resolves walking, or if it is necessary
Also check the movement key mapping (`nativeKeyDown(21)`/`nativeKeyDown(22)`, D-Pad
left/right) versus what `libgame_logic.so` actually expects.

---

## 10. Final polishing

- Complete mapping of physical controls (beyond START→Back): review in Phase 3 what `KeyEvent`s
  understands the game (probably menu navigation) and maps D-Pad/buttons consistently.
- Replace the generic art of `extras/livearea/*` with versions with the logo/art of
  `bin/popclassic/Logo/logo.png`.
- Adjustment of clocks (`scePowerSet*ClockFrequency`, already present in the draft) and
  `vglUseTripleBuffering`/multisample based on observed real console performance.
- Confirm that the game save (path `/data/data/...` detected in `libgame_logic.so`, see §4.2)
  correctly persists between runs in `ux0:data/popclassic/save/`.

---

## Known risks / things to revalidate if something doesn't add up

- This analysis was done with the `.so` of `bin/` as they are today; if replaced by another version of the
  APK, **repeat the commands from §1** (the exported symbols, `NEEDED`/`SONAME` and the mapping
  `nativeInit` vs `nativeRender` can change between versions of the app).
- Absence of audio in `bin/` (§2) was checked against the provided `.obb` and `.apk`; if one appears
  separate `bin/` copy that does include audio, skip manually extracting the APK.
- The recommendation to use `Data_960_576` (§2) is a resolution matching recommendation, not a certainty:
  Visually confirm in console that it does not introduce scaling artifacts in pixel-fixed UI.