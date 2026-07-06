<h1 align="center">
Prince of Persia Classic · PSVita Port
</h1>
<p align="center">
  <a href="#setup-instructions-for-players">How to install</a> •
  <a href="#controls">Controls</a> •
  <a href="#build-instructions-for-developers">How to compile</a> •
  <a href="#license">License</a>
</p>

Prince of Persia Classic is a 2012 version for Android and iOS devices that recreates the levels of the legendary 1989 classic game with modernized three-dimensional graphics.

This repository contains a loader for **the Android version of Prince of Persia Classic (based on Cocos2d-x)**, adapting the environment so that the native ARM dynamic libraries run on the PS Vita using [Android SO Loader by TheFloW] as a base.

Disclaimer
----------------

**Prince of Persia Classic** is the intellectual property of Ubisoft Entertainment. This software does not contain original code, executables, or game assets. To play, you must have a legitimate copy in `.apk` format and its corresponding `.obb` data file.

Setup Instructions (For Players)
----------------

To install on a real PS Vita:

- Make sure you are on an Enso firmware version (3.60 or 3.65).
- Install or update the [kubridge] and [FdFix] kernel plugins by adding them to your `config.txt` under the `*KERNEL` section:
  ```
  *KERNEL
  ur0:tai/kubridge.skprx
  ur0:tai/fd_fix.skprx
  ```
- Copy the `libshacccg.suprx` file to `ur0:/data/` (can be downloaded using the ShaRKBR33D tool).
- Extract the following libraries from the `lib/armeabi/` or `lib/armeabi-v7a/` folder of your `.apk` file and place them in `ux0:data/popclassic/`:
  * `libcocos2d.so`
  * `libcocosdenshion.so`
  * `libgame_logic.so`
- Extract the resources folder from your `.obb` data file (usually contains folders like `Animations`, `Texture`, etc.) and place it in `ux0:data/popclassic/Assets/`.
- Install the generated `popclassic.vpk` file.

Controls
-----------------

| Button | Action |
|:---:|:---|
| Left Analog / D-Pad | Move the Prince (Left/Right), Crouch (Down), Jump/Climb (Up) |
| Cross | Jump |
| Circle | Roll |
| Square | Action / Use sword |
| Start | Pause Menu (Android KEYCODE_BACK / Back button) |
| Touchscreen | Touch menu navigation |

Build Instructions (For Developers)
----------------

To compile the project, the PS Vita SDK compiled with `softfp` support is required:

```bash
git clone https://github.com/vitasdk-softfp/vdpm
```

To generate the build using CMake:

```bash
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

License
----------------

This loader is distributed under the terms of the MIT license. See the [LICENSE](LICENSE) file for more details.
