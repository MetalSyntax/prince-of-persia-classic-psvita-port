/*
 * FalsoJNI_Logger.c
 *
 * Fake Java Native Interface, providing JavaVM and JNIEnv objects.
 *
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include <stdarg.h>
#include <pthread.h>
#include <malloc.h>
#include <string.h>

#include "FalsoJNI_Logger.h"
#include "FalsoJNI.h"

#include <psp2/kernel/clib.h>

// Routed into the loader's own logger (utils/logger.h) instead of a private
// sceClibPrintf-only path: on real hardware there is no attached console, so
// every JNI dispatch trace used to be invisible -- it only ever reached the
// screen under a debugger. This is what actually shows "todo el proceso del
// juego y su ejecucion secuencial" (every native<->"Java" call the game
// makes) instead of just crashes -- see Docs/Fixes_Log.md #11.
#include "utils/logger.h"

// Local (stack) message buffer, not static: JNI dispatch can be reached from
// more than one thread (e.g. SimpleAudioEngine's background thread calls
// back into the game via JNI env pointers), and a shared static buffer here
// would race between the vsnprintf below and the l_* call reading it -- the
// underlying logger.c mutex only protects _log_print's own internals, not
// this staging buffer.
#define FJNI_LOG_MSG_SIZE 512

void _fjni_log_info(const char *fi, int li, const char *fn, const char* fmt, ...) {
#if FALSOJNI_DEBUGLEVEL <= FALSOJNI_DEBUG_INFO
    char msg[FJNI_LOG_MSG_SIZE];
    va_list list;
    va_start(list, fmt);
    sceClibVsnprintf(msg, sizeof(msg), fmt, list);
    va_end(list);
    l_info("[JNI][%s:%d][%s] %s", fi, li, fn, msg);
#endif
}

void _fjni_log_warn(const char *fi, int li, const char *fn, const char* fmt, ...) {
#if FALSOJNI_DEBUGLEVEL <= FALSOJNI_DEBUG_WARN
    char msg[FJNI_LOG_MSG_SIZE];
    va_list list;
    va_start(list, fmt);
    sceClibVsnprintf(msg, sizeof(msg), fmt, list);
    va_end(list);
    l_warn("[JNI][%s:%d][%s] %s", fi, li, fn, msg);
#endif
}

void _fjni_log_debug(const char *fi, int li, const char *fn, const char* fmt, ...) {
#if FALSOJNI_DEBUGLEVEL <= FALSOJNI_DEBUG_ALL
    char msg[FJNI_LOG_MSG_SIZE];
    va_list list;
    va_start(list, fmt);
    sceClibVsnprintf(msg, sizeof(msg), fmt, list);
    va_end(list);
    l_debug("[JNI][%s:%d][%s] %s", fi, li, fn, msg);
#endif
}

void _fjni_log_error(const char *fi, int li, const char *fn, const char* fmt, ...) {
#if FALSOJNI_DEBUGLEVEL <= FALSOJNI_DEBUG_ERROR
    char msg[FJNI_LOG_MSG_SIZE];
    va_list list;
    va_start(list, fmt);
    sceClibVsnprintf(msg, sizeof(msg), fmt, list);
    va_end(list);
    l_error("[JNI][%s:%d][%s] %s", fi, li, fn, msg);
#endif
}
