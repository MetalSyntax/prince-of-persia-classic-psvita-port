# video.h – Developer Comment Reference

Extracted and translated developer/technical comments from
[`source/video.h`](../../source/video.h).

---

## video_play – Guaranteed Return (No-Hang Contract)

**Location:** above the `void video_play(const char *raw);` declaration.

`raw` is whatever path/string the game's `playVideo` JNI call carried (see
`source/java.c`).

`video_play()` blocks until the video finishes, the player skips it
(Cross/Start), or it fails to open/decode. It must **always return** — never
hang — so that the caller can unconditionally fire `onVideoCompleted()`
afterwards regardless of outcome.

This guarantee is the fix for the hang documented in `plan_portabilidad.md`
§9.20.
