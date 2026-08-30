#pragma once

// Fix for OpenOrbis/FreeBSD headers on Windows/clang environment
#define _BSD_SOURCE
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

// External log helper
#ifdef __cplusplus
extern "C" {
#endif
    void Log(const char* fmt, ...);
#ifdef __cplusplus
}
#endif

// Manually define BSD types if likely missing
#ifndef _USHORT_DECLARED
typedef unsigned short u_short;
#define _USHORT_DECLARED
#endif

#ifndef _UINT_DECLARED
typedef unsigned int u_int;
#define _UINT_DECLARED
#endif

// Forward declare nanosleep if needed to satisfy libc++
#ifdef __cplusplus
extern "C" {
#endif
    int nanosleep(const struct timespec *req, struct timespec *rem);
#ifdef __cplusplus
}
#endif
