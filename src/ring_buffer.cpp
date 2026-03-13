/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "ring_buffer.h"
#include "soundio_private.h"
#include "util.h"

#ifdef _WIN32
#else
#include <unistd.h>
#endif


std::shared_ptr<SoundIoRingBuffer> soundio_ring_buffer_create(std::shared_ptr<SoundIo> soundio, int requested_capacity)
{
    assert(requested_capacity > 0);
    std::shared_ptr<SoundIoRingBuffer> rb = std::make_shared<SoundIoRingBuffer>();

    if (!rb)
    {
        // soundio_ring_buffer_destroy(rb);
        return NULL;
    }

    if (soundio_ring_buffer_init(rb, requested_capacity))
    {
        // soundio_ring_buffer_destroy(rb);
        return NULL;
    }

    return rb;
}

// void soundio_ring_buffer_destroy(std::shared_ptr<SoundIoRingBuffer> rb)
// {
//     if (!rb)
//         return;
//
//     soundio_ring_buffer_deinit(rb);
//
//     free(rb);
// }

// int soundio_ring_buffer_capacity(std::shared_ptr<SoundIoRingBuffer> rb)
// {
//     return rb->capacity;
// }

char* soundio_ring_buffer_write_ptr(std::shared_ptr<SoundIoRingBuffer> rb)
{
    unsigned long write_offset = SOUNDIO_ATOMIC_LOAD(rb->write_offset);
    return rb->mem->address.get() + (write_offset % rb->capacity());
}

void soundio_ring_buffer_advance_write_ptr(std::shared_ptr<SoundIoRingBuffer> rb, int count)
{
    SOUNDIO_ATOMIC_FETCH_ADD(rb->write_offset, count);
    assert(soundio_ring_buffer_fill_count(rb) >= 0);
}

char* soundio_ring_buffer_read_ptr(std::shared_ptr<SoundIoRingBuffer> rb)
{
    unsigned long read_offset = SOUNDIO_ATOMIC_LOAD(rb->read_offset);
    return rb->mem->address.get() + (read_offset % rb->capacity());
}

void soundio_ring_buffer_advance_read_ptr(std::shared_ptr<SoundIoRingBuffer> rb, int count)
{
    SOUNDIO_ATOMIC_FETCH_ADD(rb->read_offset, count);
    assert(soundio_ring_buffer_fill_count(rb) >= 0);
}

int soundio_ring_buffer_fill_count(std::shared_ptr<SoundIoRingBuffer> rb)
{
    // Whichever offset we load first might have a smaller value. So we load
    // the read_offset first.
    unsigned long read_offset = SOUNDIO_ATOMIC_LOAD(rb->read_offset);
    unsigned long write_offset = SOUNDIO_ATOMIC_LOAD(rb->write_offset);
    int count = static_cast<int>(write_offset - read_offset);
    assert(count >= 0);
    assert(count <= rb->capacity());
    return count;
}

int soundio_ring_buffer_free_count(std::shared_ptr<SoundIoRingBuffer> rb)
{
    return rb->capacity() - soundio_ring_buffer_fill_count(rb);
}

void soundio_ring_buffer_clear(std::shared_ptr<SoundIoRingBuffer> rb)
{
    unsigned long read_offset = SOUNDIO_ATOMIC_LOAD(rb->read_offset);
    SOUNDIO_ATOMIC_STORE(rb->write_offset, read_offset);
}


int soundio_ring_buffer_init(std::shared_ptr<SoundIoRingBuffer> rb, int requested_capacity)
{
    int err;
    if ((err = soundio_os_init_mirrored_memory(rb->mem, requested_capacity)))
    {
        return err;
    }
    SOUNDIO_ATOMIC_STORE(rb->write_offset, 0);
    SOUNDIO_ATOMIC_STORE(rb->read_offset, 0);

    return 0;
}

// void soundio_ring_buffer_deinit(std::shared_ptr<SoundIoRingBuffer> rb)
// {
//     soundio_os_deinit_mirrored_memory(&rb->mem);
// }


int SoundIoRingBuffer::init(int requested_capacity)
{
    int err;
    if ((err = init_mirrored_memory(requested_capacity)))
    {
        return err;
    }
    SOUNDIO_ATOMIC_STORE(write_offset, 0);
    SOUNDIO_ATOMIC_STORE(read_offset, 0);

    return 0;
}

struct MmapDeleter
{
    size_t capacity;
    void* priv = nullptr;

    MmapDeleter(size_t capacity, void* priv) : capacity(capacity), priv(priv)
    {
    }

    void operator()(char* ptr) const
    {
        if (ptr != nullptr)
        {
#ifdef _WIN32
            BOOL ok;
            ok = UnmapViewOfFile(ptr);
            assert(ok);
            ok = UnmapViewOfFile(ptr + capacity);
            assert(ok);
            ok = CloseHandle(priv);
            assert(ok);
#else
            if (ptr != MAP_FAILED)
            {
                munmap(ptr, capacity);
            }
#endif
        }
    }
};

static inline size_t ceil_dbl_to_size_t(double x)
{
    const double truncation = (size_t) x;
    return truncation + (truncation < x);
}


int SoundIoRingBuffer::init_mirrored_memory(int requested_capacity)
{
    auto page_size = soundio_os_page_size();
    size_t actual_capacity = ceil_dbl_to_size_t(requested_capacity / static_cast<double>(page_size)) * page_size;

#ifdef _WIN32
    BOOL ok;
    HANDLE hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, actual_capacity * 2, NULL);
    if (!hMapFile)
    {
        return SoundIoErrorNoMem;
    }

    for (;;)
    {
        // find a free address space with the correct size
        char* address = static_cast<char*>(MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, actual_capacity * 2));
        if (!address)
        {
            ok = CloseHandle(hMapFile);
            assert(ok);
            return SoundIoErrorNoMem;
        }

        // found a big enough address space. hopefully it will remain free
        // while we map to it. if not, we'll try again.
        ok = UnmapViewOfFile(address);
        assert(ok);

        char* addr1 = static_cast<char*>(MapViewOfFileEx(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, actual_capacity, address));
        if (addr1 != address)
        {
            DWORD err = GetLastError();
            if (err == ERROR_INVALID_ADDRESS)
            {
                continue;
            }
            else
            {
                ok = CloseHandle(hMapFile);
                assert(ok);
                return SoundIoErrorNoMem;
            }
        }

        char* addr2 = static_cast<char*>(MapViewOfFileEx(hMapFile, FILE_MAP_WRITE, 0, 0, actual_capacity, address + actual_capacity));
        if (addr2 != address + actual_capacity)
        {
            ok = UnmapViewOfFile(addr1);
            assert(ok);

            DWORD err = GetLastError();
            if (err == ERROR_INVALID_ADDRESS)
            {
                continue;
            }
            else
            {
                ok = CloseHandle(hMapFile);
                assert(ok);
                return SoundIoErrorNoMem;
            }
        }

        mem->priv = hMapFile;
        mem->address = std::unique_ptr<char, std::function<void(char*)>>(address, MmapDeleter(actual_capacity, hMapFile));
        break;
    }
#else
    char shm_path[] = "/dev/shm/soundio-XXXXXX";
    char tmp_path[] = "/tmp/soundio-XXXXXX";
    char* chosen_path;

    int fd = mkstemp(shm_path);
    if (fd < 0)
    {
        fd = mkstemp(tmp_path);
        if (fd < 0)
        {
            return SoundIoErrorSystemResources;
        }
        chosen_path = tmp_path;
    }
    else
    {
        chosen_path = shm_path;
    }

    if (unlink(chosen_path))
    {
        close(fd);
        return SoundIoErrorSystemResources;
    }

    if (ftruncate(fd, actual_capacity))
    {
        close(fd);
        return SoundIoErrorSystemResources;
    }

    char* address = static_cast<char*>(mmap(NULL, actual_capacity * 2, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
    if (address == MAP_FAILED)
    {
        close(fd);
        return SoundIoErrorNoMem;
    }

    char* other_address = static_cast<char*>(mmap(address, actual_capacity, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, fd, 0));
    if (other_address != address)
    {
        munmap(address, 2 * actual_capacity);
        close(fd);
        return SoundIoErrorNoMem;
    }

    other_address = static_cast<char*>(mmap(address + actual_capacity, actual_capacity, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, fd, 0));
    if (other_address != address + actual_capacity)
    {
        munmap(address, 2 * actual_capacity);
        close(fd);
        return SoundIoErrorNoMem;
    }

    actual_capacity = 2 * actual_capacity;
    mem->address = std::unique_ptr<char, std::function<void(char*)>>(address, MmapDeleter(actual_capacity, nullptr));

    if (close(fd))
        return SoundIoErrorSystemResources;
#endif

    mem->capacity = actual_capacity;
    return 0;
}
