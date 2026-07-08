# Patch and Solutions History (PS Vita Port)

This document details the critical problems encountered during the development of the *Prince of Persia Classic* PS Vita port and the technical solutions implemented in the *Wrapper*.

## 1. "Unknown Symbol" Crashes (e.g., 0x99165940)
**Problem:**
When attempting to load a new level, the game would crash abruptly, displaying an error message like `Unknown symbol "???" (0x99165940)`. The error failed to show the actual name of the missing function because the memory address requested was out of the bounds expected by the error catcher.

**Solution:**
* **Error Log Enhancement (`so_util.c`):** The `reloc_err` function was modified to search through *all* relocations across all modules (including `.text` and `.data` segments), removing the strict bounds limit. We also injected a patch to print *all* missing symbols during the initial load directly into the log file before the game collapsed.
* **Exported Symbols (`dynlib.c`):** Thanks to the improved log, we discovered the Android engine was trying to call standard `C` and compression functions that were not exposed to the `.so` library. The `bsearch`, `cosh`, and `gzread` functions were added to the `default_dynlib[]` array to link the game binary with the native `libc` and `libz` libraries of the PS Vita.

## 2. Invisible Texts, Broken Boxes, and Scrolling Flickers
**Problem:**
Accented letters (á, é) and the "ñ" character were rendered as white boxes. Certain dialogue boxes or menus ("Quick games") appeared completely empty. Additionally, at the end of a level, the text trembled or flickered violently when scrolling upward.

**Solution (`java.c` - `Cocos2dxBitmap_createTextBitmap`):**
* **UTF-8 Decoder:** A `utf8_decode` function was implemented to replace the strict byte-by-byte (ASCII) step when iterating the text string, allowing `stb_truetype` to correctly recognize and draw special characters for Spanish and other languages.
* **Auto-Sizing Bounding Boxes:** Logic was added so that when the engine requests `0x0` dimensions, the Wrapper automatically calculates the maximum width (`max_w`) and the number of lines (`num_lines`) required to create a perfectly sized box. This fixed the disappearing texts.
* **Pixel-Perfect Rendering:** A hack that artificially multiplied the font scale by `1.25x` and added a `+4` pixel padding was removed. This restored native 1:1 pixel rendering, preventing Cocos2d-x from creating visual artifacts when snapping to sub-pixels during a smooth scroll motion.

## 3. Log Unification and Persistence (Debug)
**Problem:**
Standard information and debug messages were printed using `sceClibPrintf` or `printf`, making them visible only when the console was connected to a live terminal. The user had no easy way to audit the loading process on a real PS Vita if the game didn't crash directly.

**Solution:**
* **Global Redirection (`logger.c`):** The source code was globally modified (`dynlib.c`, `java.c`, `so_util.c`, etc.) to replace traditional console outputs with internal library calls like `l_debug` and `l_info`.
* Now, absolutely all processes (JNI initialization, OpenGL patches, texture loading, and errors) are automatically and permanently saved in `ux0:data/popclassic/logs/log_TIMESTAMP_.txt`, allowing full execution auditing from VitaShell.

## 4. Full Audio with SoLoud (`PC=0x20` Crash on Launch)
**Problem:**
The first SoLoud integration crashed the port on launch, before reaching the menu (11 core dumps between 00:16 and 01:06 on 2026-07-07). As an emergency measure a stable build with audio disabled was shipped (`popclassic.vpk`). Forensic analysis of the dumps with `vita-parse-core` (`Prefetch abort`, constant `PC=0x20` on the main thread, stack chain `LogoScene → MenuScene → MainMenuLayer::init → playBackgroundMusic → JNI dispatcher`) proved the crash lived in the **load-failure path** of the menu BGM: the file wasn't present on the console (`error 2`) and the code carried on through an uninitialized pointer.

**Fix (`source/audio.cpp`, `source/audio_path.h`, `source/java.c`):**
* **Hardened reimplementation on SoLoud** (`vita_homebrew` backend): streamed BGM (`WavStream`), RAM-decoded cached SFX (`Wav`), and a voice group to pause/stop all effects without touching music. No error path ever executes a pointer: a failed load = log + silence + incremental dummy handle (never `0`); every audio object goes through `stopAudioSource()` before being freed.
* **Testable path sanitizer:** `sanitize_audio_path` lives in a pure header (`audio_path.h`) compiled identically on console and on the Mac. It translates `Extra/Audio/*.mp3|.m4a|.mp4` → `ux0:data/popclassic/Data/Audio/*.ogg` (audio `.mp4` requests such as `94_jaffar_fight.mp4` are sound, not video) with a `Data_960_576/` fallback.
* **Automated pre-build tests** (`extras/tests/run_tests.sh`): sanitizer against the real request shapes; verification that all 84 paths the game can request (extracted from the `.so`s with `strings`) resolve to an existing `.ogg` (84/84); and a full decode of all 93 `.ogg` files with the vendored `stb_vorbis` (93/93, 9.3M samples).
* **JNI table cleanup:** the duplicate `preloadEffect` registration in `methodsVoid[]` was removed (it also lived in `methodsInt[]` with an incompatible signature), and the immediate `onVideoCompleted` callback in the `playVideo` stub was restored (without it, "New Game" hangs waiting for a video that never plays — see plan §9.20).
* **Identifiable build:** `popclassic_soloud.vpk`, app "Prince of Persia Classic SND" v01.10, same `TITLEID` (keeps saves). The build script now archives the symbolized `build/so_loader.elf` so future `.psp2dmp` files can be symbolized.

> **Final update:** two later dumps proved the SoLoud reimplementation above still crashed due to a SoLoud-internal contract (stb_vorbis built without its "file hack") and a too-small `__sF_fake`. SoLoud was removed from the build: the definitive audio is a custom mixer over `sceAudioOut` (build `popclassic_audio.vpk` v01.11). Full detail in `Fixes_Log.md` #10 and `plan_portabilidad.md` §9.30.
