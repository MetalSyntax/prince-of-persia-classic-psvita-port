# logger.c — Developer Comment Documentation

Migrated from inline comments in [`source/utils/logger.c`](../../source/utils/logger.c).

---

## Table of Contents

1. [Consecutive-Duplicate Suppression](#consecutive-duplicate-suppression)
2. [Log File Naming Without a Reliable Clock](#log-file-naming-without-a-reliable-clock)
3. [Log File Kept Open for the Process Lifetime](#log-file-kept-open-for-the-process-lifetime)

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
