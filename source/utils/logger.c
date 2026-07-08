/*
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/logger.h"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <time.h>

#include <stdbool.h>
#include <stdatomic.h>

#define COLOR_RED    "\x1B[38;5;196m"
#define COLOR_PINK   "\x1B[38;5;212m"
#define COLOR_ORANGE "\x1B[38;5;202m"
#define COLOR_BLUE   "\x1B[38;5;32m"
#define COLOR_GREEN  "\x1B[32m"
#define COLOR_CYAN   "\x1B[36m"

#define COLOR_END    "\033[0m"

static SceKernelLwMutexWork _log_mutex;
static atomic_bool _log_mutex_ready = ATOMIC_VAR_INIT(false);

// Buffer A is used to adjust the format string.
static char buffer_a[2048];
// Buffer B is used to compile the final log using the updated format string.
static char buffer_b[2048];

// Log file descriptor, opened once lazily and kept open for the process
// lifetime (see the file-writing section below).
static int log_fd = -1;

// Consecutive-duplicate suppression: some call sites (a pre-existing FalsoJNI
// condition surfaced once its error/warn tiers started reaching this file --
// see Docs/Fixes_Log.md #13) can fire the SAME message hundreds of times in
// a row during normal gameplay (once per touch/frame). Printing/writing each
// occurrence was cheap on its own but added up to real, measurable overhead
// on the shared thread doing it, at a high enough rate to visibly slow the
// game down. Collapsing immediate repeats keeps every DISTINCT message
// (nothing is silently dropped forever) while cutting the dominant cost:
// spamming the exact same line every single frame.
static char last_msg[2048] = {0};
static unsigned int repeat_count = 0;

static void flush_repeat_notice(void) {
    if (repeat_count == 0)
        return;
    char notice[96];
    int len = sceClibSnprintf(notice, sizeof(notice), " %s(previous line repeated %u more time%s)%s\n",
                               COLOR_CYAN, repeat_count, repeat_count == 1 ? "" : "s", COLOR_END);
    sceClibPrintf(notice);
    if (log_fd >= 0)
        sceIoWrite(log_fd, notice, (SceSize) len);
    repeat_count = 0;
}

void _log_print(int t, const char* fmt, ...) {
    if (!atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
        int ret = sceKernelCreateLwMutex(&_log_mutex, "log_lock", 0, 0, NULL);
        if (ret < 0) {
            sceClibPrintf("Error: failed to create log mutex: 0x%x\n", ret);
            return;
        }
        atomic_store_explicit(&_log_mutex_ready, true, memory_order_relaxed);
    }
    sceKernelLockLwMutex(&_log_mutex, 1, NULL);

    switch (t) {
        case LT_DEBUG:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s• debug%s    %s\n",
                            COLOR_PINK, COLOR_END, fmt); break;
        case LT_INFO:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %sℹ info%s     %s\n",
                            COLOR_BLUE, COLOR_END, fmt); break;
        case LT_WARN:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s⚠ warning%s  %s\n",
                            COLOR_ORANGE, COLOR_END, fmt); break;
        case LT_ERROR:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s⨯ error%s    %s\n",
                            COLOR_RED, COLOR_END, fmt); break;
        case LT_FATAL:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s! fatal%s    %s\n",
                            COLOR_RED, COLOR_END, fmt); break;
        case LT_SUCCESS:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s! success%s  %s\n",
                            COLOR_GREEN, COLOR_END, fmt); break;
        case LT_WAIT:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s… waiting%s  %s\n",
                            COLOR_CYAN, COLOR_END, fmt); break;
        default:
            if (atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
                sceKernelUnlockLwMutex(&_log_mutex, 1);
            }
            return;
    }

    va_list list;
    va_start(list, fmt);
    sceClibVsnprintf(buffer_b, sizeof(buffer_b), buffer_a, list);
    va_end(list);

#ifdef DATA_PATH
    // Opened once and kept open for the process lifetime: re-opening a file
    // on every single log line (the previous behavior) meant every call
    // here paid a full sceIoOpen+Close on top of the write, which is real
    // filesystem work on a memory card, not a cheap syscall.
    if (log_fd < 0) {
        sceIoMkdir(DATA_PATH "logs", 0777);
        char log_file_path[256];
        time_t tt = time(NULL);
        sceClibSnprintf(log_file_path, sizeof(log_file_path), "%slogs/log_%u_.txt", DATA_PATH, (unsigned int)tt);
        log_fd = sceIoOpen(log_file_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    }
#endif

    if (sceClibStrcmp(buffer_b, last_msg) == 0) {
        repeat_count++;
    } else {
        flush_repeat_notice();
        sceClibStrncpy(last_msg, buffer_b, sizeof(last_msg) - 1);
        last_msg[sizeof(last_msg) - 1] = '\0';

        sceClibPrintf(buffer_b);
#ifdef DATA_PATH
        if (log_fd >= 0) {
            sceIoWrite(log_fd, buffer_b, sceClibStrnlen(buffer_b, sizeof(buffer_b)));
        }
#endif
    }

    if (atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
        sceKernelUnlockLwMutex(&_log_mutex, 1);
    }
}
