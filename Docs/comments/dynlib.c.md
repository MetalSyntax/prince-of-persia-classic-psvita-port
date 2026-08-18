# dynlib.c — Developer Comment Documentation

Migrated from inline comments in [`source/dynlib.c`](../../source/dynlib.c).  
All Spanish comments have been translated to English.

---

## Table of Contents

1. [Fake __sF Array – Sizing and Zero-Initialization](#fake-__sf-array--sizing-and-zero-initialization)
2. [fflush, fputc, fputs, fwrite – Routing Through the *_soloader Wrappers](#fflush-fputc-fputs-fwrite--routing-through-the-_soloader-wrappers)
3. [fdopen and setvbuf – Must Match fopen's Runtime (PC=0x20 Crash Family)](#fdopen-and-setvbuf--must-match-fopens-runtime-pc0x20-crash-family)
4. [Functions Safe to Leave on newlib](#functions-safe-to-leave-on-newlib)
5. [fprintf and vfprintf – Routing Through the Wrappers](#fprintf-and-vfprintf--routing-through-the-wrappers)

---

## Fake __sF Array – Sizing and Zero-Initialization

**Location:** Comment above the `FILE __sF_fake[0x100][3];` declaration (original
lines 124–132), and the short rationale comment inside `resolve_imports()`
(original lines 1056–1057).

`__sF_fake` is the fake bionic `__sF` array (bionic's stdin/stdout/stderr
table) handed to the game in place of the real thing. It is deliberately
oversized on purpose, matching the sizes used by two earlier reference
ports: pop2-vita uses `[0x1000][3]`, deadspace-vita uses `[0x100][3]` (the
size adopted here).

The reason it must be oversized at all: bionic computes the game's `stderr`
as `&__sF[2]`, using **bionic's own** `FILE` struct stride — which is
different from newlib's `FILE` size. With a naively-sized 3-entry array,
that computed pointer lands **past the end** of the array, and every
`fprintf(stderr, ...)` call the game makes would spray formatted text over
whatever `.data` happens to sit right after it. That silent corruption is
what trampled `so_loader`'s own memory and caused the entire
**PC=0x20 / `_fseeko_r`** family of crashes (see plan §9.29–9.30).

The array is kept intentionally zeroed at all times: any read the game
performs through it sees a dead, zeroed-out `FILE`, and the `*_soloader`
print wrappers in `reimpl/io.c` detect any `FILE*` that points inside this
array — at whatever stride bionic used to compute it — and route the output
to the logger instead of letting either C runtime try to interpret a fake
`FILE` structure.

---

## fflush, fputc, fputs, fwrite – Routing Through the *_soloader Wrappers

**Location:** Comment above the `fflush`, `fputc`, `fputs`, `fwrite` entries
in `default_dynlib[]` (original lines 423–425).

Writing through a `FILE*` must **always** go through the `*_soloader`
wrappers: they detect the `__sF_fake` array (the game's `stderr`/`stdout`)
and redirect that output to the logger. For real files, the wrappers simply
delegate to whichever runtime `fopen_soloader` is currently using.

---

## fdopen and setvbuf – Must Match fopen's Runtime (PC=0x20 Crash Family)

**Location:** Comment above the `fdopen`, `setvbuf`, `putc` entries in
`default_dynlib[]` (original lines 457–463).

Warning: any function that reads or writes through a `FILE*` the game
opened must belong to the **same** runtime as `fopen` (SceLibc, under
`USE_SCELIBC_IO`). Mixing runtimes silently corrupts the other side's
`FILE` structures — `fdopen`/`setvbuf` resolved to newlib while
`fseek`/`fclose` were SceLibc trampled newlib's static `FILE` pool. This
was the real cause of the entire **PC=0x20 / `_fseeko_r`** crash family
from July 7th (Fixes_Log #9, plan §9.29), confirmed across 12 core dumps.

---

## Functions Safe to Leave on newlib

**Location:** Comment above the `fileno`, `freopen`, `fwide`, `getwc`,
`putchar`, `puts`, `putwc`, `ungetwc` entries in `default_dynlib[]`
(original lines 468–470).

These functions either don't operate on the `FILE*` handles the game opens
via `fopen` (they only touch `stdout`, or wide-char APIs nothing in the
game actually uses), or they don't exist in the SceLibc bridge and have no
real import in the `.so` files anyway — so resolving them to newlib is
safe here.

---

## fprintf and vfprintf – Routing Through the Wrappers

**Location:** Comment above the `fprintf`, `vfprintf` entries in
`default_dynlib[]` (original lines 516–517).

`fprintf`/`vfprintf` are routed through the `*_soloader` wrappers because
the game uses them on `stderr`/`stdout` (the `__sF_fake` array) in addition
to real files.
