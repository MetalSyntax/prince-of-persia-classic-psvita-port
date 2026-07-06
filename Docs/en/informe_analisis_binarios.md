# Static analysis report — Phase 1 of the portability plan

**Phase 1** deliverable of `plan_portibilidad.md` ("Findings from the static inspection of the `.so`").
Everything that follows was generated with commands executable from macOS without cross-ARM toolchain
(`objdump`, `unzip -l`, `strings`, `file`), over the three actual binaries in `bin/`. The exact commands
are documented in each section so that it is reproducible if the `.so` are replaced with another version
of the APK.

## 0. Binary identification

```bash
file bin/*.so
```

| Archive | Size | Build date | Format |
|---|---:|---|---|
| `libcocosdenshion.so` | 74,532B | 2012-10-23 | ELF32 LSB, ARM EABI5, stripped, dynamically linked |
| `libcocos2d.so` | 1,437,604 B | 2012-10-23 | ELF32 LSB, ARM EABI5, stripped, dynamically linked |
| `libgame_logic.so` | 1,345,616B | 2012-10-23 | ELF32 LSB, ARM EABI5, stripped, dynamically linked |

All three have dynamic symbol tables intact (`stripped` only removed local/debug symbols, not the
dynamic table), so `objdump -T`/`-p` works the same as with a non-stripped binary for everything
import into a `so_util` loader.

## 1. Dynamic section: `NEEDED` / `SONAME`

```bash
objdump -p bin/<lib>.so | sed -n '/Dynamic Section/,/^$/p'
```

```
libcocosdenshion.so SONAME=libcocosdenshion.so
                      NEEDED: liblog libstdc++ libm libc libdl

libcocos2d.so SONAME=libcocos2d.so
                      NEEDED: libGLESv1_CM liblog libz libstdc++ libm libc libdl

libgame_logic.so SONAME=libgame_logic.so
                      NEEDED: libGLESv1_CM libcocos2d.so libcocosdenshion.so liblog libstdc++ libm libc libdl
```

**Conclusion (used in Phase 4 of the plan):** `libgame_logic.so` is the only one that declares explicit dependency
about the other two `.so`s in the game. Required loading order: `libcocosdenshion.so` →
`libcocos2d.so` → `libgame_logic.so`. `lib/so_util/so_util.c` already resolves cross symbols between modules
automatically loaded via `so_resolve_link()` by comparing these same `NEEDED`/`SONAME`, so it doesn't do
no additional mechanism is missing.

## 2. Inventory of exported symbols vs. unresolved (`UND`)

```bash
objdump -T bin/<lib>.so > <lib>_dynsym.txt
grep -c '\*UND\*' <lib>_dynsym.txt # unresolved
grep -vc '\*UND\*' <lib>_dynsym.txt # exported
grep -c 'Java_' <lib>_dynsym.txt # exported JNI methods
```

| Bookstore | Total symbols | Exported | `UND` (to be resolved) | Exported `Java_` methods | `JNI_OnLoad` |
|---|---:|---:|---:|---:|:---:|
| `libcocosdenshion.so` | 431 | 400 | 27 | 0 | yes |
| `libcocos2d.so` | 5,513 | 5,344 | 165 | 25 | yes |
| `libgame_logic.so` | 5,444 | 5,051 | 389 | 1 | not |

## 3. Where each Cocos2dx-Android JNI method lives (fixes a mistaken assumption in the original plan)

`libcocos2d.so` is the one that exposes **the entire** lifecycle of `Cocos2dxRenderer`/`Cocos2dxActivity`:

```
Cocos2dxRenderer_nativeRender
Cocos2dxRenderer_nativeTouchesBegin / _Move / _End / _Cancel
Cocos2dxRenderer_nativeKeyDown / nativeKeyUp
Cocos2dxRenderer_nativeOnPause / nativeOnResume
Cocos2dxRenderer_nativeInsertText / nativeDeleteBackward / nativeGetContentText
Cocos2dxActivity_nativeSetPaths/nativeSetPackageName/nativeSetNumOfCPUCores
Cocos2dxActivity_nativeSetDensityScaleValue / nativeSetDevicePixelsPerInch
Cocos2dxActivity_nativeSetIsGoogleLauncherBuild
Cocos2dxActivity_GetConfig / SetControlVisible / SetControlInVisible
Cocos2dxBitmap_nativeInitBitmapDC
Cocos2dxAccelerometer_onSensorChanged
Cocos2dxVideo_onVideoCompleted
org_ubisoft_InApp_InAppHandler_purchaseSuccessful
```

`libgame_logic.so` exports **a single** `Java_` method: `Cocos2dxRenderer_nativeInit`. How Android loads
`libgame_logic.so` last (`System.loadLibrary` order: denshion → cocos2d → game_logic), su
`nativeInit` is the one that is actually executed when calling `Cocos2dxRenderer.nativeInit()` from Java (shadowing
by loading order). **Rule for loader (Phase 4):** resolve `nativeInit` from `game_mod`; the rest
(`nativeRender`, `nativeTouches*`, `nativeKeyDown/Up`, `nativeSetPaths`, etc.) from `cocos2d_mod`, because
`game_mod` does not redefine them.

`libcocosdenshion.so` does not expose any `Java_`: only the C++ class `CocosDenshion::SimpleAudioEngine`,
direct call in C++ from the other two modules (see §4).

## 4. Test cross-resolution between modules

```bash
comm -12 <(sort libgame_logic_UND.txt) <(sort libcocos2d_EXPORTS.txt)
comm -12 <(sort libgame_logic_UND.txt) <(sort libcocosdenshion_EXPORTS.txt)
```

- **284** `UND` symbols from `libgame_logic.so` are exported by `libcocos2d.so` (resolve themselves if
  the loading order of §1 is respected).
  - 235 are from the `cocos2d::` namespace (engine: `CCSprite`, `CCNode`, `CCArray`, `CCMenuItem`, etc.)
  - 21 are from third-party utilities packaged within `libcocos2d.so`, not the engine itself:
    `CCFlurryUtils` (analytics), `CCShareUtils` (sharing/Facebook), `CCVideoUtils` (video ads),
    `CCInAppUtils` (in-app purchases), `CCCrossPromoUtils`, `CCPapayaUtils` (Papaya social network, see
    `original/.../com.papaya.socialsdk...` in the APK), `CCGeneralUtils`, `CCFileUtils`.
    **Action for later phases:** these 8 classes are candidates for **no-op** (return
    `false`/empty/no effect) instead of actually redeploying — there is no point in porting telemetry from
    Flurry or in-app purchases from Google Play to Vita. Prioritize only if the game hangs when not receiving a
    valid answer.
  - 28 remaining: mix of C++ standard library symbols (`std::`, RTTI) that count as
    "exported" in `libcocos2d.so` by chance of layout, not relevant to list here.
- **15** `UND` symbols from `libgame_logic.so` are in `libcocosdenshion.so`: exactly the 15 methods
  `CocosDenshion::SimpleAudioEngine` (`playEffect`, `playBackgroundMusic`, `setEffectsVolume`, etc.) — the
  game calls audio in direct C++, not through JNI. This is the basis of Phase 5 of the plan (reimplementation
  native audio).

## 5. What remains to be resolved via `default_dynlib` (reimpl of libc/GLES/etc.)

Discounting the cross resolution of §4, there are:

- `libcocosdenshion.so`: 27 symbols — pure libc/pthread (`malloc`, `free`, `sprintf`, `strcmp`,
  `pthread_mutex_lock`, `__android_log_print`, etc.). Already covered by `source/reimpl/*` +
  `lib/libc_bridge`.
- `libcocos2d.so`: 165 symbols — libc/pthread + **zlib** (`crc32`, `deflate*`, `inflate*`, `gzopen`,
  `gzread`, `uncompress` — already covered by the `z` in `target_link_libraries`) + **OpenGL ES 1.1 function
  fixed complete**:
  ```
  glAlphaFunc glBindBuffer glBindFramebufferOES glBindTexture glBlendFunc glBufferData glBufferSubData
  glCheckFramebufferStatusOES glClear glClearColor glClearDepthf glColor4f glColorMask glColorPointer
  glCompressedTexImage2D glDeleteBuffers glDeleteFramebuffersOES glDeleteTextures glDepthFunc glDepthMask
  glDisable glDisableClientState glDrawArrays glDrawElements glEnable glEnableClientState
  glFramebufferTexture2DOES glFrustumf glGenBuffers glGenFramebuffersOES glGenTextures
  glGenerateMipmapOES glGetError glGetFloatv glGetIntegerv glGetString glLoadIdentity glMatrixMode
  glMultMatrixf glOrthof glPixelStorei glPointSizePointerOES glPopMatrix glPushMatrix glReadPixels
  glRotatef glScissor glTexCoordPointer glTexEnvi glTexImage2D glTexParameteri glTranslatef
  glVertexPointer glViewport
  ```
  **Concrete risk for Phase 4, point 6:** this is the array stack/fixed-function of ES1
  (`glLoadIdentity`, `glMultMatrixf`, `glPushMatrix`/`glPopMatrix`, `glRotatef`/`glTranslatef`,
  `glTexEnvi`, `glAlphaFunc`) — vitaGL primarily targets a modern ES2-like pipeline; you have to confirm
  before writing code that `source/reimpl/egl.c`/`source/utils/glutil.c` (or vitaGL directly) cover
  this ES1 compatibility layer, or if it is necessary to add a matrix-stack emulation (usual pattern
  in other ES1 engine ports to vitaGL).
- `libgame_logic.so`: 90 symbols — same libc/pthread profile as above, with nothing new outside of
  what is already covered by the boilerplate.

## 6. File path hints (directly affects Phase 2 of the plan — **major fix**)

```bash
strings -a bin/libgame_logic.so | grep -E "^Data/|^Data_640_384/|^Data_960_576/"
```

- `libgame_logic.so` contains **125 full literals** prefixed with `Data/`
  (e.g. `Data/Animations/big_guard_final/big_guard_final_01.plist`).
- **Zero** occurrences of `Data_640_384/` or `Data_960_576/` as a full literal in the binary.

This **fixes the assets section of the main plan**: the compiled code only asks for routes under a
directory literally named `Data`, not `Assets` (as v1 of the plan said) and not `Data_960_576`
(as v2 suggested, reasoning only for screen resolution fit). **The folder on the card must
be called `Data/`, not `Assets/`.**

As an aside, comparing file sizes:

```bash
unzip -l original/main.1.org.ubisoft.premium.POPClassic.zip | grep "Logo/logo.png$"
# Data/Logo/logo.png 123079 B
# Data_640_384/Logo/logo.png 55420 B
# Data_960_576/Logo/logo.png 89080 B
ls -la bin/popclassic/Logo/logo.png
#89080B
```

`bin/popclassic/` has already been extracted from the bucket **`Data_960_576`** (good fit with the 960×544 of the Vita in
regarding content/texture quality), but it should be **renamed/moved to a folder called `Data/`** in
the final destination, because the binary does not know how to search in `Data_960_576/`. That is: content of
`Data_960_576`, folder name `Data`.

Save file names (`/pop_save_normal`, `/pop_save_profile`,
`/pop_save_survivor`, `/pop_save_time_trial`, `/pop_save_level_specific`, `/pop_save_final`,
`/pop_save_updateV1[_fixed]`), with leading `/` — consistent with a route type
`getFilesDir() + "/pop_save_normal"` on Android (`/data/data/<pkg>/files/...`). Confirm that you have to
intercept those routes (Phase 4, point 2) and redirect them to `ux0:data/popclassic/save/`.

## 7. Immediate action on `plan_portibilidad.md`

The findings of §6 contradict Phase 2 as written today (use `Assets/` as asset name).
folder and suggests evaluating using the full `Data_960_576` bucket). It is necessary to update that phase to
reflect: destination folder `Data/`, content taken from `Data_960_576` of the `.obb` (which is what is already in
`bin/popclassic/`), without needing to extract any resolution again.