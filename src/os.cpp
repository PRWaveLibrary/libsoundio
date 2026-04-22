/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */


#include "os.h"
#include "soundio_internal.h"

#include <ctime>
#include <cassert>

static std::atomic_bool initialized = false;
static std::mutex init_mutex;

#if defined(SOUNDIO_OS_WINDOWS)

static double win32_time_resolution;
static SYSTEM_INFO win32_system_info;

#else

#if defined(__MACH__)
#include <mach/clock.h>
#include <mach/mach.h>
static clock_serv_t cclock;
#endif

#endif

static int page_size;

double soundio_os_get_time()
{
#if defined(SOUNDIO_OS_WINDOWS)
    unsigned __int64 time;
    QueryPerformanceCounter((LARGE_INTEGER*) &time);
    return time * win32_time_resolution;
#elif defined(__MACH__)
    mach_timespec_t mts;

    kern_return_t err = clock_get_time(cclock, &mts);
    assert(!err);

    double seconds = (double) mts.tv_sec;
    seconds += ((double) mts.tv_nsec) / 1000000000.0;

    return seconds;
#else
    struct timespec tms;
    clock_gettime(CLOCK_MONOTONIC, &tms);
    double seconds = static_cast<double>(tms.tv_sec);
    seconds += static_cast<double>(tms.tv_nsec) / 1000000000.0;
    return seconds;
#endif
}

#if defined(SOUNDIO_OS_WINDOWS)
#include <objbase.h>

static void run_win32_thread(void (*run)(std::shared_ptr<void> arg), std::shared_ptr<void> arg)
{
    HRESULT err = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    assert(err == S_OK);
    run(arg);
    CoUninitialize();
}

#endif


std::unique_ptr<SoundIoOsThread> SoundIoOsThread::create(STATIC_CALLBACK_PTR run, std::shared_ptr<void> arg)
{
    std::unique_ptr<SoundIoOsThread> thread = std::make_unique<SoundIoOsThread>();
#if defined(SOUNDIO_OS_WINDOWS)
    thread->thread = std::make_unique<std::thread>(run_win32_thread, run, arg);
#else
    thread->thread = std::make_unique<std::thread>(run, arg);
#endif
    return thread;
}


std::unique_ptr<SoundIoOsMutex> soundio_os_mutex_create()
{
    return std::make_unique<SoundIoOsMutex>();
}


std::unique_ptr<SoundIoOsCond> soundio_os_cond_create()
{
    return std::make_unique<SoundIoOsCond>();
}

void SoundIoOsCond::signal(std::unique_lock<std::mutex>* locked_mutex)
{
    if (locked_mutex != nullptr)
    {
        cond_.notify_one();
    }
    else
    {
        std::lock_guard lock(default_mutex_);
        cond_.notify_one();
    }
}

void SoundIoOsCond::timed_wait(std::unique_lock<std::mutex>* locked_mutex, double seconds)
{
    std::chrono::duration<double> duration(seconds);
    if (locked_mutex != nullptr)
    {
        cond_.wait_for(*locked_mutex, duration);
    }
    else
    {
        std::unique_lock lock(default_mutex_);
        cond_.wait_for(lock, duration);
    }
}

void SoundIoOsCond::wait(std::unique_lock<std::mutex>* locked_mutex)
{
    if (locked_mutex != nullptr)
    {
        cond_.wait(*locked_mutex);
    }
    else
    {
        std::unique_lock lock(default_mutex_);
        cond_.wait(lock);
    }
}

template<class Predicate>
void SoundIoOsCond::wait(std::unique_lock<std::mutex>* locked_mutex, Predicate pred)
{
    if (locked_mutex != nullptr)
    {
        cond_.wait(*locked_mutex, pred);
    }
    else
    {
        std::unique_lock lock(default_mutex_);
        cond_.wait(lock, pred);
    }
}

static int internal_init()
{
#if defined(SOUNDIO_OS_WINDOWS)
    unsigned __int64 frequency;
    if (QueryPerformanceFrequency((LARGE_INTEGER*) &frequency))
    {
        win32_time_resolution = 1.0 / (double) frequency;
    }
    else
    {
        return SoundIoErrorSystemResources;
    }
    GetSystemInfo(&win32_system_info);
    page_size = win32_system_info.dwAllocationGranularity;
#else
    page_size = (int) sysconf(_SC_PAGESIZE);
#if defined(__MACH__)
    host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &cclock);
#endif
#endif
    return 0;
}

void SoundIoOsMutex::lock()
{
    mutex_.lock();
}

void SoundIoOsMutex::unlock()
{
    mutex_.unlock();
}

int soundio_os_init()
{
    std::lock_guard lock(init_mutex);
    if (initialized.load(std::memory_order_acquire))
    {
        return 0;
    }
    initialized.store(true, std::memory_order_release);
    return internal_init();
}

int soundio_os_page_size()
{
    return page_size;
}



