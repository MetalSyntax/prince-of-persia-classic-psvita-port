<h1 align="center">
Prince of Persia Classic · PSVita Port
</h1>
<p align="center">
  <a href="#installation-instructions">How to install</a> •
  <a href="#controls">Controls</a> •
  <a href="#build-instructions-for-developers">How to compile</a> •
  <a href="README.md">Versión en Español</a>
</p>

Prince of Persia Classic is a 2012 release for Android and iOS devices that recreates the levels of the legendary 1989 classic game with modernized 3D graphics.

This repository contains a **wrapper/loader** for the Android version of Prince of Persia Classic (based on Cocos2d-x), adapting the environment to run the native ARM dynamic libraries (`.so`) on the PS Vita using TheFloW's Android SO Loader base.

## ⚠️ Legal & DMCA Disclaimer
**Prince of Persia Classic** is the intellectual property of Ubisoft Entertainment.  
This repository does **NOT** contain any original code, executables, protected binaries, or game assets. It strictly provides the open-source "wrapper" code. The included LiveArea assets are AI-generated or open-source images, free from copyright restrictions.

To play the game, you MUST possess a legitimate, legally obtained copy of the Android game. Users must manually extract and provide their own files (`.apk`, `.obb`, and `.so` libraries).

---

## Installation Instructions

To install the port on a real PS Vita:

1. Ensure your console is running **Enso** (firmware 3.60 or 3.65).
2. Install the [kubridge] and [FdFix] plugins by adding them to your `config.txt` under `*KERNEL`:
   ```
   *KERNEL
   ur0:tai/kubridge.skprx
   ur0:tai/fd_fix.skprx
   ```
3. Install `libshacccg.suprx` (you can download it using the ShaRKBR33D homebrew).
4. Install the generated `popclassic.vpk` file.
5. Obtain your legal Android copy. You will need a **minimal modified APK** and a **modified OBB** (extracted/optimized) compatible with this port. Place the extracted data into the appropriate folder either at `ux0:app/POPCLASC1/` or `ux0:data/popclassic/`.
6. Extract the native libraries from the `lib/armeabi/` or `lib/armeabi-v7a/` folder of your `.apk` and place them alongside the game data:
   * `libcocos2d.so`
   * `libcocosdenshion.so`
   * `libgame_logic.so`

*(Note: Check community forums for patching scripts and tools to prepare your legal APK and OBB files).*

## Controls

| Button | Action |
|:---:|:---|
| Left Analog / D-Pad | Move Prince (Left/Right), Crouch (Down), Jump/Climb (Up) |
| Cross | Jump |
| Circle | Roll |
| Square | Action / Use Sword |
| Start | Pause Menu (Android KEYCODE_BACK) |
| Touchscreen | Touch Menu Navigation |

## Build Instructions (For Developers)

You need the PS Vita SDK compiled with `softfp` support:

```bash
git clone https://github.com/vitasdk-softfp/vdpm
```

To build the project using CMake:

```bash
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## License

This loader is distributed under the MIT License. See the `LICENSE` file for more details.
