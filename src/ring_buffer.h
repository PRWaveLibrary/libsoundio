/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef SOUNDIO_RING_BUFFER_H
#define SOUNDIO_RING_BUFFER_H

#include "os.h"

struct SoundIoRingBuffer
{
    std::shared_ptr<SoundIoOsMirroredMemory> mem;

    std::atomic<unsigned long> write_offset;
    std::atomic<unsigned long> read_offset;

    int init(int requested_capacity);

    int capacity() const
    {
        if (mem == nullptr)
        {
            return 0;
        }
        return mem->capacity;
    }

    int fill_count() const
    {
        int count = static_cast<int>(write_offset - read_offset);
        assert(count >= 0);
        assert(count <= capacity());
        return count;
    }

    char* write_ptr()
    {
        return mem->address.get() + (write_offset % capacity());
    }

    char* read_ptr()
    {
        return mem->address.get() + (read_offset % capacity());
    }


    void advance_write_ptr(int count)
    {
        write_offset.fetch_add(count);
        assert(fill_count() >= 0);
    }

    void advance_read_ptr(int count)
    {
        read_offset.fetch_add(count);
        assert(fill_count() >= 0);
    }

    int free_count() const
    {
        return capacity() - fill_count();
    }

    void clear()
    {
        write_offset.store(read_offset);
    }

private:
    int init_mirrored_memory(int requested_capacity);
};

int soundio_ring_buffer_init(std::shared_ptr<SoundIoRingBuffer> rb, int requested_capacity);


#endif
