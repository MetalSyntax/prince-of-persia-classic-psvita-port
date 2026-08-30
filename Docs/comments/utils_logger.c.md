# logger.c — Developer Comment Documentation

Migrated from inline comments in [`source/utils/logger.c`](../../source/utils/logger.c).

---

## Table of Contents

1. [Consecutive-Duplicate Suppression](#consecutive-duplicate-suppression)
2. [Log File Naming Without a Reliable Clock](#log-file-naming-without-a-reliable-clock)
3. [Log File Kept Open for the Process Lifetime](#log-file-kept-open-for-the-process-lifetime)
4. [Debugnet UDP Live Log Broadcast](#debugnet-udp-live-log-broadcast)

---

## Consecutive-Duplicate Suppression

**Location:** above the `last_msg` / `repeat_count` static variable declarations, just before `next_log_index()`.

Some call sites — a pre-existing FalsoJNI condition that only started reaching this file once its error/warn tiers were routed into the logger (see `Docs/Fixes_Log.md` item 13) — can fire the exact same message hundreds of times in a row during normal gameplay (once per touch/frame). Printing and writing each individual occurrence was cheap on its own, but added up to real, measurable overhead on the shared thread doing it, at a high enough rate to visibly slow the game down.

Collapsing immediate repeats keeps every *distinct* message (nothing is silently dropped forever) while cutting the dominant cost: spamming the exact same line every single frame. When the message changes, `flush_repeat_notice()` prints/writes a `(previous line repeated N more times)` note before the new line, so no information is lost — only the redundant duplicates are collapsed.

---

## Log File Naming Without a Reliable Clock

**Location:** above `next_log_index()`.

`next_log_index()` picks the next free `log_<N>_.txt` index by scanning the `logs/` directory, instead of stamping the filename with `time(NULL)`. Consoles without a battery-backed RTC (or one that has simply never been set) don't advance their clock across power cycles, so every run would compute the exact same "unique" timestamp and keep re-appending to one stale file forever — this looked like logging had stopped working entirely.

A sequential index has no clock dependency, so every run is guaranteed a fresh file regardless of what the RTC thinks the date is. The index is zero-padded so that lexicographic and numeric filename order agree.

---

## Log File Kept Open for the Process Lifetime

**Location:** inside `_log_print()`, in the `#ifdef DATA_PATH` block that lazily opens `log_fd`.

The log file is opened once and kept open for the process lifetime. Re-opening the file on every single log line (the previous behavior) meant every call here paid a full `sceIoOpen` + `sceIoClose` on top of the write, which is real filesystem work on a memory card, not a cheap syscall (see `Docs/Fixes_Log.md` item 12, where this was the fix for a game-wide slowdown and audio underruns caused by verbose per-call logging). `sceIoWrite` still lands on disk immediately, so crash durability is unchanged from the old per-line-open behavior; only the repeated open/close cost was removed.

---

## Debugnet UDP Live Log Broadcast

**Location:** `debugnet_init()`/`debugnet_send()`/`level_tag()`, above `_log_print()`; called from inside `_log_print()`'s lazy mutex-init block and from its print/write branch, plus from `flush_repeat_notice()`.

Added because the file-based log (above) can lose its tail on a long play session that ends in a crash or hard reset instead of a clean exit — `sceIoWrite` lands each line on disk, but nothing here ever `sceIoClose()`s or explicitly syncs the file, and a card's own filesystem metadata can still lag behind what's been written when the console dies mid-session (confirmed by report: several real minutes of gameplay produced a log file with none of it in it). A live network feed doesn't depend on the file surviving a crash at all.

Every line this file already prints/writes also gets broadcast as a UDP datagram to `255.255.255.255:9999` — the limited-broadcast address, not a specific configured host, so this needs no PC IP to be set anywhere and keeps working across whatever network the Vita and the dev machine happen to share, matching how `psvita-toolkit logs-live`'s listener (`0.0.0.0:9999`, same default port) already expects to receive it: one plain UTF-8 text line per datagram, no binary framing, with a bracketed `[DEBUG]`/`[INFO]`/`[WARN]`/`[ERROR]`/`[FATAL]` severity tag prefix that its regex-based color coding looks for (`LT_SUCCESS`/`LT_WAIT`, which have no bracket-tag equivalent on the listener side, are sent as `[INFO]` rather than dropped or left untagged). The payload is the plain user message re-rendered from a second `va_start`/`va_end` pass over the same `fmt`/`...`, not `buffer_b`, since `buffer_b` has this file's own ANSI color codes and bullet-symbol prefix baked in for the local console/file, which would just show up as escape-code noise in a plain-text UDP viewer.

`sceSysmoduleLoadModule(SCE_SYSMODULE_NET)`, `sceNetInit`/`sceNetSocket`/`sceNetSetsockopt(..., SCE_NET_SO_BROADCAST, ...)` run once, lazily, alongside the existing mutex creation. Every failure path here (module-load failure, `sceNetInit` error, socket creation failure) is silently tolerated — `debugnet_sock` simply stays `-1` and `debugnet_send()` becomes a no-op — so a Vita with no Wi-Fi connected, or any other network-layer failure, degrades to exactly today's file+console-only logging instead of blocking or crashing the game. This whole feature only compiles in behind the same `DEBUG_SOLOADER`/`ENABLE_VERBOSE_LOG` gate as every other log call in this project (`logger.h`), so it costs nothing in a normal release build.

**v01.23 shipped without the `sceSysmoduleLoadModule(SCE_SYSMODULE_NET)` call and crashed immediately on real hardware.** `SceNet`/`SceNetCtl` aren't resident modules on PS Vita by default — every networking call needs that module loaded first (the same requirement `source/video.cpp` already handles for `SCE_SYSMODULE_AVPLAYER`), and it was missed when this feature was first written. Confirmed via `psvita-toolkit analyze` + `arm-vita-eabi-addr2line` on the real `.psp2dmp`: `Prefetch abort`, `PC=0x0` (a call through a completely unresolved import — the module's NID table was never bound), `LR` pointing exactly at the `sceNetCtlInit()` line in `debugnet_init()`. Fixed in v01.24 by loading the module first and bailing out early (before touching `sceNetInit`/`sceNetCtlInit`/the socket) if that fails, matching the same silent-degrade posture as every other failure path here.
