/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef SOUNDIO_RING_BUFFER_H
#define SOUNDIO_RING_BUFFER_H

#include "os.h"
#include "atomics.h"

struct SoundIoRingBuffer
{
    std::shared_ptr<SoundIoOsMirroredMemory> mem;
    struct SoundIoAtomicULong write_offset;
    struct SoundIoAtomicULong read_offset;

    int init(int requested_capacity);

    int capacity() const
    {
        if (mem == nullptr)
        {
            return 0;
        }
        return mem->capacity;
    }

private:
    int init_mirrored_memory(int requested_capacity);
};

int soundio_ring_buffer_init(std::shared_ptr<SoundIoRingBuffer> rb, int requested_capacity);

// void soundio_ring_buffer_deinit(std::shared_ptr<SoundIoRingBuffer> rb);

#endif
