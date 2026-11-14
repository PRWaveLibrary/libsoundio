/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef SOUNDIO_UTIL_H
#define SOUNDIO_UTIL_H

#include <cstdlib>

#ifdef _MSC_VER
#define SOUNDIO_ATTR_COLD
#define SOUNDIO_ATTR_NORETURN __declspec(noreturn)
#define SOUNDIO_ATTR_FORMAT(...)
#define SOUNDIO_ATTR_UNUSED __pragma(warning(suppress : 4100))
#define SOUNDIO_ATTR_WARN_UNUSED_RESULT _Check_return_
#else
#define SOUNDIO_ATTR_COLD __attribute__((cold))
#define SOUNDIO_ATTR_NORETURN __attribute__((noreturn))
#define SOUNDIO_ATTR_FORMAT(...) __attribute__((format(__VA_ARGS__)))
#define SOUNDIO_ATTR_UNUSED __attribute__((unused))
#define SOUNDIO_ATTR_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#endif

SOUNDIO_ATTR_NORETURN
void soundio_panic(const char* format, ...) SOUNDIO_ATTR_COLD SOUNDIO_ATTR_FORMAT(printf, 1, 2);

char* soundio_alloc_sprintf(int* len, const char* format, ...) SOUNDIO_ATTR_FORMAT(printf, 2, 3);

static inline int ceil_dbl_to_int(double x)
{
    const double truncation = (int) x;
    return truncation + (truncation < x);
}

static inline double ceil_dbl(double x)
{
    const double truncation = (long long) x;
    const double ceiling = truncation + (truncation < x);
    return ceiling;
}

#endif
