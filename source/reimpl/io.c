/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file io.c
 * @brief Android I/O syscall reimplementations for the PS Vita so-loader.
 *        Handles path resolution, fd/FILE runtime bridging, and fake-stdio
 *        routing.  @note See docs/comments/reimpl_io.c.md for design rationale.
 */

#include "reimpl/io.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdarg.h>
#include <psp2/kernel/threadmgr.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "utils/logger.h"
#include "utils/utils.h"

// Includes the following inline utilities:
// int oflags_musl_to_newlib(int flags);
// dirent64_bionic * dirent_newlib_to_bionic(struct dirent* dirent_newlib);
// void stat_newlib_to_bionic(struct stat * src, stat64_bionic * dst);
#include "reimpl/bits/_struct_converters.c"

//! @see docs/comments/reimpl_io.c.md#path-resolution-and-save-data-redirection
#define ANDROID_DATA_PREFIX "/data/data/org.ubisoft.premium.POPClassic/"

static const char * resolve_data_path(const char * path, char * buf, size_t buf_size) {
    if (strncmp(path, ANDROID_DATA_PREFIX, sizeof(ANDROID_DATA_PREFIX) - 1) == 0) {
        snprintf(buf, buf_size, "%ssave/%s", DATA_PATH, path + sizeof(ANDROID_DATA_PREFIX) - 1);
        return buf;
    }
    if (path[0] != '/' && strchr(path, ':') == NULL) {
        snprintf(buf, buf_size, "%s%s", DATA_PATH, path);
        return buf;
    }
    return path;
}

//! @see docs/comments/reimpl_io.c.md#fd-to-path-registry-and-the-pc0x20-crash-family
#define FD_PATH_SLOTS 32
static struct {
    int fd; // 0 = free slot (open() can't return 0 here: stdin exists)
    char path[512];
} fd_path_registry[FD_PATH_SLOTS];

static void fd_path_remember(int fd, const char *path) {
    if (fd <= 0)
        return;
    for (int i = 0; i < FD_PATH_SLOTS; i++) {
        if (fd_path_registry[i].fd == 0 || fd_path_registry[i].fd == fd) {
            fd_path_registry[i].fd = fd;
            snprintf(fd_path_registry[i].path, sizeof(fd_path_registry[i].path), "%s", path);
            return;
        }
    }
}

static const char * fd_path_lookup(int fd) {
    if (fd <= 0)
        return NULL;
    for (int i = 0; i < FD_PATH_SLOTS; i++) {
        if (fd_path_registry[i].fd == fd)
            return fd_path_registry[i].path;
    }
    return NULL;
}

static void fd_path_forget(int fd) {
    if (fd <= 0)
        return;
    for (int i = 0; i < FD_PATH_SLOTS; i++) {
        if (fd_path_registry[i].fd == fd) {
            fd_path_registry[i].fd = 0;
            return;
        }
    }
}

FILE * fopen_soloader(const char * filename, const char * mode) {
    if (strcmp(filename, "/proc/cpuinfo") == 0) {
        return fopen_soloader("app0:/cpuinfo", mode);
    } else if (strcmp(filename, "/proc/meminfo") == 0) {
        return fopen_soloader("app0:/meminfo", mode);
    }

    char resolved[512];
    filename = resolve_data_path(filename, resolved, sizeof(resolved));

#ifdef USE_SCELIBC_IO
    FILE* ret = sceLibcBridge_fopen(filename, mode);
#else
    FILE* ret = fopen(filename, mode);
#endif

    if (ret)
        l_debug("fopen(%s, %s): %p", filename, mode, ret);
    else
        l_warn("fopen(%s, %s): %p", filename, mode, ret);

    return ret;
}

int open_soloader(const char * path, int oflag, ...) {
    if (strcmp(path, "/proc/cpuinfo") == 0) {
        return open_soloader("app0:/cpuinfo", oflag);
    } else if (strcmp(path, "/proc/meminfo") == 0) {
        return open_soloader("app0:/meminfo", oflag);
    }

    char resolved[512];
    path = resolve_data_path(path, resolved, sizeof(resolved));

    mode_t mode = 0666;
    if (((oflag & BIONIC_O_CREAT) == BIONIC_O_CREAT) ||
        ((oflag & BIONIC_O_TMPFILE) == BIONIC_O_TMPFILE)) {
        va_list args;
        va_start(args, oflag);
        mode = (mode_t)(va_arg(args, int));
        va_end(args);
    }

    oflag = oflags_bionic_to_newlib(oflag);
    int ret = open(path, oflag, mode);
    if (ret >= 0) {
        fd_path_remember(ret, path);
        l_debug("open(%s, %x): %i", path, oflag, ret);
    } else {
        l_warn("open(%s, %x): %i", path, oflag, ret);
    }
    return ret;
}

int fstat_soloader(int fd, stat64_bionic * buf) {
    struct stat st;
    int res = fstat(fd, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("fstat(%i): %i", fd, res);
    return res;
}

int stat_soloader(const char * path, stat64_bionic * buf) {
    char resolved[512];
    path = resolve_data_path(path, resolved, sizeof(resolved));

    struct stat st;
    int res = stat(path, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("stat(%s): %i", path, res);
    return res;
}

int fclose_soloader(FILE * f) {
#ifdef USE_SCELIBC_IO
    int ret = sceLibcBridge_fclose(f);
#else
    int ret = fclose(f);
#endif

    l_debug("fclose(%p): %i", f, ret);
    return ret;
}

int close_soloader(int fd) {
    fd_path_forget(fd);
    int ret = close(fd);
    l_debug("close(%i): %i", fd, ret);
    return ret;
}

/** @brief Converts a low-level fd to a SceLibc FILE for the game's stdio calls.
 *  @note See docs/comments/reimpl_io.c.md#fdopen_soloader--scelibc-file-reopen */
FILE * fdopen_soloader(int fd, const char * mode) {
#ifdef USE_SCELIBC_IO
    const char *path = fd_path_lookup(fd);
    if (path) {
        FILE *f = sceLibcBridge_fopen(path, mode);
        if (f) {
            off_t off = lseek(fd, 0, SEEK_CUR);
            if (off > 0)
                sceLibcBridge_fseek(f, (long) off, SEEK_SET);
            fd_path_forget(fd);
            close(fd);
            l_debug("fdopen(%i -> %s, %s): %p (SceLibc)", fd, path, mode, f);
            return f;
        }
        l_warn("fdopen(%i): SceLibc reopen of %s failed, falling back to newlib", fd, path);
    } else {
        l_warn("fdopen(%i): fd not in registry, falling back to newlib (runtime mix!)", fd);
    }
#endif
    FILE *f = fdopen(fd, mode);
    l_debug("fdopen(%i, %s): %p", fd, mode, f);
    return f;
}

/** @brief Accepts and ignores a buffering hint to avoid cross-runtime FILE corruption.
 *  @note See docs/comments/reimpl_io.c.md#setvbuf_soloader--pure-buffering-hint--cross-runtime-corruption */
int setvbuf_soloader(FILE * f, char * buf, int mode, size_t size) {
    l_debug("setvbuf(%p, mode=%i, size=%u): ignored", f, mode, (unsigned) size);
    return 0;
}

// --- game printing (stdout/stderr) ---
//! @see docs/comments/reimpl_io.c.md#game-printing--stdoutstderr-section

extern FILE __sF_fake[0x100][3]; // dynlib.c

static int is_fake_std(FILE * f) {
    uintptr_t p = (uintptr_t) f;
    uintptr_t base = (uintptr_t) __sF_fake;
    return p >= base && p < base + sizeof(__sF_fake);
}

int vfprintf_soloader(FILE * f, const char * fmt, va_list va) {
    char buf[1024];
    int n = vsnprintf(buf, sizeof(buf), fmt, va);
    if (is_fake_std(f)) {
        if (n > 0)
            l_info("[game] %s", buf);
        return n;
    }
    if (n <= 0)
        return n;
    size_t w = ((size_t) n < sizeof(buf)) ? (size_t) n : sizeof(buf) - 1;
#ifdef USE_SCELIBC_IO
    return (int) sceLibcBridge_fwrite(buf, 1, w, f);
#else
    return (int) fwrite(buf, 1, w, f);
#endif
}

int fprintf_soloader(FILE * f, const char * fmt, ...) {
    va_list va;
    va_start(va, fmt);
    int n = vfprintf_soloader(f, fmt, va);
    va_end(va);
    return n;
}

int fputs_soloader(const char * s, FILE * f) {
    if (is_fake_std(f)) {
        l_info("[game] %s", s ? s : "(null)");
        return 0;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fputs(s, f);
#else
    return fputs(s, f);
#endif
}

int fputc_soloader(int c, FILE * f) {
    if (is_fake_std(f))
        return (unsigned char) c; // dropped; single chars aren't worth logging
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fputc(c, f);
#else
    return fputc(c, f);
#endif
}

size_t fwrite_soloader(const void * ptr, size_t size, size_t nmemb, FILE * f) {
    if (is_fake_std(f)) {
        int len = (int)(size * nmemb);
        if (len > 0 && ptr)
            l_info("[game] %.*s", len > 1023 ? 1023 : len, (const char *) ptr);
        return nmemb; // claim success so the game never retries
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fwrite(ptr, size, nmemb, f);
#else
    return fwrite(ptr, size, nmemb, f);
#endif
}

int fflush_soloader(FILE * f) {
    if (is_fake_std(f))
        return 0;
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fflush(f);
#else
    return fflush(f);
#endif
}

DIR* opendir_soloader(char* _pathname) {
    char resolved[512];
    _pathname = (char *) resolve_data_path(_pathname, resolved, sizeof(resolved));

    DIR* ret = opendir(_pathname);
    l_debug("opendir(\"%s\"): %p", _pathname, ret);
    return ret;
}

struct dirent64_bionic * readdir_soloader(DIR * dir) {
    static struct dirent64_bionic dirent_tmp;

    struct dirent* ret = readdir(dir);
    l_debug("readdir(%p): %p", dir, ret);

    if (ret) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(ret);
        memcpy(&dirent_tmp, entry_tmp, sizeof(dirent64_bionic));
        free(entry_tmp);
        return &dirent_tmp;
    }

    return NULL;
}

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result) {
    struct dirent dirent_tmp;
    struct dirent * pdirent_tmp;

    int ret = readdir_r(dirp, &dirent_tmp, &pdirent_tmp);

    if (ret == 0) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(&dirent_tmp);
        memcpy(entry, entry_tmp, sizeof(dirent64_bionic));
        *result = (pdirent_tmp != NULL) ? entry : NULL;
        free(entry_tmp);
    }

    l_debug("readdir_r(%p, %p, %p): %i", dirp, entry, result, ret);
    return ret;
}

int closedir_soloader(DIR * dir) {
    int ret = closedir(dir);
    l_debug("closedir(%p): %i", dir, ret);
    return ret;
}

int fcntl_soloader(int fd, int cmd, ...) {
    l_warn("fcntl(%i, %i, ...): not implemented", fd, cmd);
    return 0;
}

int ioctl_soloader(int fd, int request, ...) {
    l_warn("ioctl(%i, %i, ...): not implemented", fd, request);
    return 0;
}

int fsync_soloader(int fd) {
    int ret = fsync(fd);
    l_debug("fsync(%i): %i", fd, ret);
    return ret;
}
