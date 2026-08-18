# reimpl/io.c – Developer Comment Reference

Extracted and translated developer/technical comments from
[`source/reimpl/io.c`](../../source/reimpl/io.c).

---

## Path Resolution and Save-Data Redirection

**Location:** above `#define ANDROID_DATA_PREFIX` (original lines 35–48)

The game reads certain files (e.g. `Data_960_576/Localization/*.loc`) via plain
relative paths with no device prefix, mirroring how Android resolves paths
relative to the app's data directory. There is no real per-process working
directory on the Vita (`chdir()`/`getcwd()` are simply passed through to
newlib and are unrelated to how `sceIo` resolves paths), so relative paths
must be rewritten to `DATA_PATH` by hand before they reach
`fopen()` / `open()` / `stat()` / `opendir()`.

Save data (profile, per-mode progress, etc.) is read/written against
Android's app-private storage path
(`/data/data/<package>/pop_save_*`, `tempBuffer.txt`, etc. — baked into
`libgame_logic.so`). That device does not exist on the Vita
("Cannot find device for path"), and critically the game does **not** handle a
failed save-file open gracefully: it goes on to dereference the
(never-populated) profile data, which crashes with a **null function pointer
call**. The path must therefore be redirected, not merely tolerated as missing.

---

## fd-to-Path Registry and the PC=0x20 Crash Family

**Location:** above `#define FD_PATH_SLOTS` / `fd_path_registry` declaration
(original lines 63–70)

`fd_path_registry` is an fd → resolved-path table filled by `open_soloader()`.
It exists so that `fdopen_soloader()` can hand the game a `FILE` from the
**same** C runtime (SceLibc) it uses for every other stdio call.

Before this fix, `fdopen` returned a **newlib** `FILE` while
`fseek` / `fread` / `fclose` were SceLibc: each runtime wrote into the other's
`FILE` layout, silently trampling newlib's static `FILE` pool. That was the
real cause of the **"PC=0x20" / fseek crash family** (Fixes_Log #8/#9,
plan §9.28/§9.29).

The table is a small linear array because the game holds only a handful of fds
open at any one time.

---

## fdopen_soloader – SceLibc FILE Reopen

**Location:** inside `fdopen_soloader()`, `#ifdef USE_SCELIBC_IO` block
(original lines 209–214)

The `FILE*` returned here will be fed back into
`sceLibcBridge_fseek` / `fread` / `fclose` by the game, so it **must** be a
SceLibc `FILE`. Using newlib's `fdopen` would return a newlib `FILE` whose
internal layout SceLibc then corrupts (see the fd-to-path registry section
above).

The workaround is to reopen the file **by path** (looked up from
`fd_path_registry`) and mirror the fd's current offset. Per `fdopen` semantics
the original fd is owned by the resulting `FILE`, so the newlib fd is closed
once the reopen succeeds.

---

## setvbuf_soloader – Pure Buffering Hint / Cross-Runtime Corruption

**Location:** inside `setvbuf_soloader()` (original lines 238–241)

`setvbuf` is a pure buffering hint. Implementing it would mean writing into a
`FILE` that belongs to the **other** C runtime (game `FILE`s are SceLibc; this
symbol used to be resolved to newlib's `setvbuf`) — the exact cross-runtime
struct corruption documented in Fixes_Log #9. It is therefore safe to accept
the call and silently ignore it.

---

## Game Printing – stdout/stderr Section

**Location:** above `extern FILE __sF_fake[]` declaration (original lines 246–254)

The game's `stderr` / `stdout` are entries of the fake `__sF` array declared
in `dynlib.c`. Bionic computes `&__sF[2]` using its own `struct` stride, so
the pointer can land anywhere inside that array.

Every printing function in this section first checks whether the `FILE*` points
into the fake array (at any stride) via `is_fake_std()`, and if so routes the
text to the logger instead of letting either C runtime interpret a fake `FILE`.
This is necessary because SceLibc treating a fake `FILE` as its own `FILE`
struct sprayed formatted text over `so_loader`'s `.data` segment (see plan
§9.30).
