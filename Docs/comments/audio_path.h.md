# audio_path.h – Developer Comments

Extracted from `source/audio_path.h`. All meaningful block and inline
developer comments are preserved here verbatim, organized by section.

---

## Path Translation Design

> **Source lines 4–17** — file-level block comment before `#include <string>`.

This header contains pure path-translation helpers for the audio engine,
deliberately kept free of any Vita or SoLoud dependencies: the exact same
code is compiled and unit-tested on the host (see
`extras/tests/test_audio_path.cpp`) before every build.

The game requests Android asset paths such as:

- `Extra/Audio/Music/POP_BGM_Menu.mp3`
- `Extra/Audio/SFX/Enemies/Jaffar/94_jaffar_fight.mp4` (this is audio, despite
  the `.mp4` extension)
- `Extra/Audio/Music/95_boss_fight_2.m4a`

while the unpacked assets on the memory card are `.mp3` files under
`ux0:data/popclassic/Data/Audio/...` — the game's original mp3 files copied
as-is. The handful of non-mp3 sources (`.m4a` / `.mp4`-container audio) are
the only ones actually transcoded, and only once, offline, straight to
`.mp3`, so the engine only ever needs one native decoder (minimp3).
