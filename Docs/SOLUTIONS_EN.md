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
