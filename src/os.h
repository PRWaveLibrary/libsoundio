/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef SOUNDIO_OS_H
#define SOUNDIO_OS_H

#include <memory>
#include <functional>
#include <cassert>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>


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
#else

#include <unistd.h>

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

#endif

using STATIC_CALLBACK_PTR = void (*)(std::shared_ptr<void>);

class SoundIoOsThread
{
    std::unique_ptr<std::thread> thread;

public:
    virtual ~SoundIoOsThread()
    {
        if (thread == nullptr)
        {
            return;
        }
        if (thread->joinable())
        {
            thread->join();
        }
    }

    static std::unique_ptr<SoundIoOsThread> create(STATIC_CALLBACK_PTR run, std::shared_ptr<void> arg);
};

class SoundIoOsMutex
{
    std::mutex mutex_;

public:
    void lock();

    void unlock();

    std::mutex& get()
    {
        return mutex_;
    }
};

class SoundIoOsCond
{
    std::condition_variable cond_;
    std::mutex default_mutex_;

public:
    void signal(std::unique_lock<std::mutex>* locked_mutex);

    void wait(std::unique_lock<std::mutex>* locked_mutex);

    template<class Predicate>
    void wait(std::unique_lock<std::mutex>* locked_mutex, Predicate pred);

    void timed_wait(std::unique_lock<std::mutex>* locked_mutex, double seconds);
};

// struct SoundIoRingBuffer;


// safe to call from any thread(s) multiple times, but
// must be called at least once before calling any other os functions
// soundio_create calls this function.
int soundio_os_init();

double soundio_os_get_time();


// std::unique_ptr<SoundIoOsThread> soundio_os_thread_create(void (*run)(std::shared_ptr<void> arg), std::shared_ptr<void> arg);

// void soundio_os_thread_destroy(std::shared_ptr<SoundIoOsThread> thread);


std::unique_ptr<SoundIoOsMutex> soundio_os_mutex_create();

std::unique_ptr<SoundIoOsCond> soundio_os_cond_create();

// void soundio_os_cond_destroy(struct SoundIoOsCond* cond);

// locked_mutex is optional. On systems that use mutexes for conditions, if you
// pass NULL, a mutex will be created and locked/unlocked for you. On systems
// that do not use mutexes for conditions, no mutex handling is necessary. If
// you already have a locked mutex available, pass it; this will be better on
// systems that use mutexes for conditions.
void soundio_os_cond_signal(std::unique_ptr<SoundIoOsCond> cond, std::unique_ptr<SoundIoOsMutex> locked_mutex);

void soundio_os_cond_timed_wait(std::unique_ptr<SoundIoOsCond> cond, std::unique_ptr<SoundIoOsMutex> locked_mutex, double seconds);

void soundio_os_cond_wait(std::unique_ptr<SoundIoOsCond> cond, std::unique_ptr<SoundIoOsMutex> locked_mutex);


int soundio_os_page_size();

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

#endif
