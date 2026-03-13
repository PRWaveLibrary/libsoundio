/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef SOUNDIO_OS_H
#define SOUNDIO_OS_H

#if defined(__APPLE__)
#else
#define _GNU_SOURCE
#endif

#include <stdbool.h>
#include <stddef.h>
#include <memory>
#include <functional>
#include <cassert>

#if defined(__FreeBSD__) || defined(__MACH__)
#define SOUNDIO_OS_KQUEUE
#endif


#if defined(_WIN32)
#define SOUNDIO_OS_WINDOWS

#if !defined(NOMINMAX)
#define NOMINMAX
#endif

#if !defined(VC_EXTRALEAN)
#define VC_EXTRALEAN
#endif

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif

#if !defined(UNICODE)
#define UNICODE
#endif

// require Windows 7 or later
#if WINVER < 0x0601
#undef WINVER
#define WINVER 0x0601
#endif
#if _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <mmsystem.h>
#include <objbase.h>
#else

#include <pthread.h>
#include <unistd.h>

#include <utility>
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

#endif


#if defined(__FreeBSD__) || defined(__MACH__)
#define SOUNDIO_OS_KQUEUE
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#endif

#if defined(__MACH__)
#include <mach/clock.h>
#include <mach/mach.h>
#endif


#ifdef SOUNDIO_OS_KQUEUE
static const uintptr_t notify_ident = 1;
#endif


#ifdef __cplusplus
extern "C"
{
#endif


struct SoundIoOsThread
{
    ~SoundIoOsThread()
    {
#ifdef _WIN32
        if (handle)
        {
            DWORD err = WaitForSingleObject(handle, INFINITE);
            assert(err != WAIT_FAILED);
            BOOL ok = CloseHandle(handle);
            assert(ok);
        }
#else

        if (running)
        {
            assert(!pthread_join(id, nullptr));
        }

        if (attr_init)
        {
            assert(!pthread_attr_destroy(&attr));
        }
#endif
    }

#ifdef _WIN32
    HANDLE handle;
    DWORD id;
#else
    pthread_attr_t attr;
    bool attr_init;

    pthread_t id;
    bool running;
#endif
    std::shared_ptr<void> arg;

    void (*run)(std::shared_ptr<void> arg);
};

struct SoundIoOsMutex
{
#ifdef _WIN32
    CRITICAL_SECTION id;
#else
    pthread_mutex_t id;
    bool id_init;
#endif
    ~SoundIoOsMutex()
    {
#ifdef _WIN32
        DeleteCriticalSection(&id);
#else
        if (id_init)
        {
            assert(!pthread_mutex_destroy(&id));
        }
#endif
    }
};

struct SoundIoOsCond
{
#ifdef SOUNDIO_OS_KQUEUE
    int kq_id;
#elif defined(_WIN32)
    CONDITION_VARIABLE id;
    CRITICAL_SECTION default_cs_id;
#else
    pthread_cond_t id;
    bool id_init;

    pthread_condattr_t attr;
    bool attr_init;

    pthread_mutex_t default_mutex_id;
    bool default_mutex_init;
#endif

    ~SoundIoOsCond()
    {
#ifdef SOUNDIO_OS_KQUEUE
        close(kq_id);
#elif defined(SOUNDIO_OS_WINDOWS)
        DeleteCriticalSection(&default_cs_id);
#else
        if (id_init)
        {
            assert(!pthread_cond_destroy(&id));
        }

        if (attr_init)
        {
            assert(!pthread_condattr_destroy(&attr));
        }
        if (default_mutex_init)
        {
            assert(!pthread_mutex_destroy(&default_mutex_id));
        }
#endif
    }
};

// struct SoundIoRingBuffer;


// safe to call from any thread(s) multiple times, but
// must be called at least once before calling any other os functions
// soundio_create calls this function.
int soundio_os_init(void);

double soundio_os_get_time(void);


int soundio_os_thread_create(void (*run)(std::shared_ptr<void> arg), std::shared_ptr<void> arg, void (*emit_rtprio_warning)(void), std::unique_ptr<SoundIoOsThread>* out_thread);

// void soundio_os_thread_destroy(std::shared_ptr<SoundIoOsThread> thread);


std::unique_ptr<SoundIoOsMutex> soundio_os_mutex_create();

// void soundio_os_mutex_destroy(struct SoundIoOsMutex* mutex);

void soundio_os_mutex_lock(std::unique_ptr<SoundIoOsMutex>& mutex);

void soundio_os_mutex_unlock(std::unique_ptr<SoundIoOsMutex>& mutex);


std::unique_ptr<SoundIoOsCond> soundio_os_cond_create(void);

// void soundio_os_cond_destroy(struct SoundIoOsCond* cond);

// locked_mutex is optional. On systems that use mutexes for conditions, if you
// pass NULL, a mutex will be created and locked/unlocked for you. On systems
// that do not use mutexes for conditions, no mutex handling is necessary. If
// you already have a locked mutex available, pass it; this will be better on
// systems that use mutexes for conditions.
void soundio_os_cond_signal(SoundIoOsCond* cond, SoundIoOsMutex* locked_mutex);

void soundio_os_cond_timed_wait(SoundIoOsCond* cond, SoundIoOsMutex* locked_mutex, double seconds);

void soundio_os_cond_wait(SoundIoOsCond* cond, SoundIoOsMutex* locked_mutex);


int soundio_os_page_size(void);

// You may rely on the size of this struct as part of the API and ABI.
struct SoundIoOsMirroredMemory
{
    size_t capacity = 0;
    std::unique_ptr<char, std::function<void(char*)>> address;
    void* priv = nullptr;
};

// returned capacity might be increased from capacity to be a multiple of the
// system page size
// int soundio_os_init_mirrored_memory(std::shared_ptr<SoundIoOsMirroredMemory> mem, size_t capacity);

// void soundio_os_deinit_mirrored_memory(struct SoundIoOsMirroredMemory* mem);

#ifdef __cplusplus
}
#endif

#endif
