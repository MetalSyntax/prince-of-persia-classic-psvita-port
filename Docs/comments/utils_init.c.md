# init.c — Developer Comment Documentation

Migrated from inline comments in [`source/utils/init.c`](../../source/utils/init.c).

---

## Table of Contents

1. [kubridge Check Under Vita3K](#kubridge-check-under-vita3k)

---

## kubridge Check Under Vita3K

**Location:** inside `soloader_init_all()`, `#ifdef EMULATOR_BUILD` branch of the `module_loaded("kubridge")` check.

Vita3K implements `kuKernelCpuUnrestrictedMemcpy`/`kuKernelFlushCaches` at the HLE level without registering an actual "kubridge" kernel module, so `module_loaded("kubridge")` always reports false there — even though `so_util`'s calls into those functions work fine under the emulator. On real hardware, the check is meaningful: if it fails there, `kubridge.skprx` genuinely isn't installed and it's a fatal error. Under `EMULATOR_BUILD`, the failed check is therefore only logged as an expected warning rather than treated as fatal.
