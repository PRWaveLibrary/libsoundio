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
    std::unique_ptr<SoundIoOsThread> thread = nullptr;
    std::unique_ptr<SoundIoOsCond> cond;
    std::atomic_flag abort_flag;
    double period_duration;
    int buffer_frame_count;
    int frames_left;
    int write_frame_count;
    std::unique_ptr<SoundIoRingBuffer> ring_buffer;
    double playback_start_time;
    std::atomic_bool clear_buffer;
    std::atomic_bool pause_requested;
    struct SoundIoChannelArea areas[SOUNDIO_MAX_CHANNELS];
};

struct SoundIoInStreamDummy
{
    std::unique_ptr<SoundIoOsThread> thread = nullptr;
    std::unique_ptr<SoundIoOsCond> cond;
    std::atomic_flag abort_flag;
    double period_duration;
    int frames_left;
    int read_frame_count;
    int buffer_frame_count;
    std::unique_ptr<SoundIoRingBuffer> ring_buffer;
    std::atomic_bool pause_requested;
    SoundIoChannelArea areas[SOUNDIO_MAX_CHANNELS];
};

#endif
