/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef SOUNDIO_DUMMY_H
#define SOUNDIO_DUMMY_H

#include "soundio_internal.h"
#include "os.h"
#include "ring_buffer.h"
#include "atomics.h"
#include <memory>

struct SoundIoPrivate;

int soundio_dummy_init(std::shared_ptr<SoundIoPrivate> si);

struct SoundIoDummy
{
    std::unique_ptr<SoundIoOsMutex> mutex;
    std::unique_ptr<SoundIoOsCond> cond;
    bool devices_emitted;
};

struct SoundIoDeviceDummy
{
    int make_the_struct_not_empty;
};

struct SoundIoOutStreamDummy
{
    std::unique_ptr<SoundIoOsThread, SoundIoOsThreadDeleter> thread = std::unique_ptr<SoundIoOsThread, SoundIoOsThreadDeleter>(nullptr, SoundIoOsThreadDeleter());
    std::unique_ptr<SoundIoOsCond> cond;
    struct SoundIoAtomicFlag abort_flag;
    double period_duration;
    int buffer_frame_count;
    int frames_left;
    int write_frame_count;
    std::unique_ptr<SoundIoRingBuffer> ring_buffer;
    double playback_start_time;
    struct SoundIoAtomicFlag clear_buffer_flag;
    struct SoundIoAtomicBool pause_requested;
    struct SoundIoChannelArea areas[SOUNDIO_MAX_CHANNELS];
};

struct SoundIoInStreamDummy
{
    std::unique_ptr<SoundIoOsThread, SoundIoOsThreadDeleter> thread = std::unique_ptr<SoundIoOsThread, SoundIoOsThreadDeleter>(nullptr, SoundIoOsThreadDeleter());
    std::unique_ptr<SoundIoOsCond> cond;
    struct SoundIoAtomicFlag abort_flag;
    double period_duration;
    int frames_left;
    int read_frame_count;
    int buffer_frame_count;
    std::unique_ptr<SoundIoRingBuffer> ring_buffer;
    struct SoundIoAtomicBool pause_requested;
    struct SoundIoChannelArea areas[SOUNDIO_MAX_CHANNELS];
};

#endif
