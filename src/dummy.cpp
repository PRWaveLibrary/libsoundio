/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "dummy.h"
#include "soundio_private.h"

#include <stdio.h>
#include <string.h>

static void playback_thread_run(std::shared_ptr<void> arg)
{
    std::shared_ptr<SoundIoOutStreamPrivate> os = std::static_pointer_cast<SoundIoOutStreamPrivate>(arg);
    std::shared_ptr<SoundIoOutStream> outstream = os;
    struct SoundIoOutStreamDummy* osd = &os->backend_data.dummy;

    int fill_bytes = soundio_ring_buffer_fill_count(osd->ring_buffer);
    int free_bytes = soundio_ring_buffer_capacity(osd->ring_buffer) - fill_bytes;
    int free_frames = free_bytes / outstream->bytes_per_frame;
    osd->frames_left = free_frames;
    if (free_frames > 0)
        outstream->write_callback(outstream, 0, free_frames);
    double start_time = soundio_os_get_time();
    long frames_consumed = 0;

    while (SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(osd->abort_flag))
    {
        double now = soundio_os_get_time();
        double time_passed = now - start_time;
        double next_period = start_time +
                             ceil_dbl(time_passed / osd->period_duration) * osd->period_duration;
        double relative_time = next_period - now;
        soundio_os_cond_timed_wait(osd->cond, NULL, relative_time);
        if (!SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(osd->clear_buffer_flag))
        {
            soundio_ring_buffer_clear(osd->ring_buffer);
            int free_bytes = soundio_ring_buffer_capacity(osd->ring_buffer);
            int free_frames = free_bytes / outstream->bytes_per_frame;
            osd->frames_left = free_frames;
            if (free_frames > 0)
                outstream->write_callback(outstream, 0, free_frames);
            frames_consumed = 0;
            start_time = soundio_os_get_time();
            continue;
        }

        if (SOUNDIO_ATOMIC_LOAD(osd->pause_requested))
        {
            start_time = now;
            frames_consumed = 0;
            continue;
        }

        int fill_bytes = soundio_ring_buffer_fill_count(osd->ring_buffer);
        int fill_frames = fill_bytes / outstream->bytes_per_frame;
        int free_bytes = soundio_ring_buffer_capacity(osd->ring_buffer) - fill_bytes;
        int free_frames = free_bytes / outstream->bytes_per_frame;

        double total_time = soundio_os_get_time() - start_time;
        long total_frames = total_time * outstream->sample_rate;
        int frames_to_kill = (int) (total_frames - frames_consumed);
        int read_count = soundio_int_min(frames_to_kill, fill_frames);
        int byte_count = read_count * outstream->bytes_per_frame;
        soundio_ring_buffer_advance_read_ptr(osd->ring_buffer, byte_count);
        frames_consumed += read_count;

        if (frames_to_kill > fill_frames)
        {
            outstream->underflow_callback(outstream);
            osd->frames_left = free_frames;
            if (free_frames > 0)
                outstream->write_callback(outstream, 0, free_frames);
            frames_consumed = 0;
            start_time = soundio_os_get_time();
        }
        else if (free_frames > 0)
        {
            osd->frames_left = free_frames;
            outstream->write_callback(outstream, 0, free_frames);
        }
    }
}

static void capture_thread_run(std::shared_ptr<void> arg)
{
    std::shared_ptr<SoundIoInStreamPrivate> is = std::static_pointer_cast<SoundIoInStreamPrivate>(arg);
    std::shared_ptr<SoundIoInStream> instream = is;
    struct SoundIoInStreamDummy* isd = &is->backend_data.dummy;

    long frames_consumed = 0;
    double start_time = soundio_os_get_time();
    while (SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(isd->abort_flag))
    {
        double now = soundio_os_get_time();
        double time_passed = now - start_time;
        double next_period = start_time +
                             ceil_dbl(time_passed / isd->period_duration) * isd->period_duration;
        double relative_time = next_period - now;
        soundio_os_cond_timed_wait(isd->cond, NULL, relative_time);

        if (SOUNDIO_ATOMIC_LOAD(isd->pause_requested))
        {
            start_time = now;
            frames_consumed = 0;
            continue;
        }

        int fill_bytes = soundio_ring_buffer_fill_count(isd->ring_buffer);
        int free_bytes = soundio_ring_buffer_capacity(isd->ring_buffer) - fill_bytes;
        int fill_frames = fill_bytes / instream->bytes_per_frame;
        int free_frames = free_bytes / instream->bytes_per_frame;

        double total_time = soundio_os_get_time() - start_time;
        long total_frames = total_time * instream->sample_rate;
        int frames_to_kill = (int) (total_frames - frames_consumed);
        int write_count = soundio_int_min(frames_to_kill, free_frames);
        int byte_count = write_count * instream->bytes_per_frame;
        soundio_ring_buffer_advance_write_ptr(isd->ring_buffer, byte_count);
        frames_consumed += write_count;

        if (frames_to_kill > free_frames)
        {
            instream->overflow_callback(instream);
            frames_consumed = 0;
            start_time = soundio_os_get_time();
        }
        if (fill_frames > 0)
        {
            isd->frames_left = fill_frames;
            instream->read_callback(instream, 0, fill_frames);
        }
    }
}

static void destroy_dummy(std::shared_ptr<SoundIoPrivate> si)
{
    struct SoundIoDummy* sid = &si->backend_data.dummy;

    if (sid->cond)
        soundio_os_cond_destroy(sid->cond);

    if (sid->mutex)
        soundio_os_mutex_destroy(sid->mutex);
}

static void flush_events_dummy(std::shared_ptr<SoundIoPrivate> si)
{
    std::shared_ptr<SoundIo> soundio = si;
    struct SoundIoDummy* sid = &si->backend_data.dummy;
    if (sid->devices_emitted)
        return;
    sid->devices_emitted = true;
    soundio->on_devices_change(soundio);
}

static void wait_events_dummy(std::shared_ptr<SoundIoPrivate> si)
{
    struct SoundIoDummy* sid = &si->backend_data.dummy;
    flush_events_dummy(si);
    soundio_os_cond_wait(sid->cond, NULL);
}

static void wakeup_dummy(std::shared_ptr<SoundIoPrivate> si)
{
    struct SoundIoDummy* sid = &si->backend_data.dummy;
    soundio_os_cond_signal(sid->cond, NULL);
}

static void force_device_scan_dummy(std::shared_ptr<SoundIoPrivate> si)
{
    // nothing to do; dummy devices never change
}

static void outstream_destroy_dummy(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    struct SoundIoOutStreamDummy* osd = &os->backend_data.dummy;

    if (osd->thread)
    {
        SOUNDIO_ATOMIC_FLAG_CLEAR(osd->abort_flag);
        soundio_os_cond_signal(osd->cond, NULL);
        // soundio_os_thread_destroy(osd->thread);
        osd->thread = NULL;
    }
    soundio_os_cond_destroy(osd->cond);
    osd->cond = NULL;
    osd->ring_buffer = nullptr;
    // soundio_ring_buffer_deinit(&osd->ring_buffer);
}

static int outstream_open_dummy(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    struct SoundIoOutStreamDummy* osd = &os->backend_data.dummy;
    std::shared_ptr<SoundIoOutStream> outstream = os;
    std::shared_ptr<SoundIoDevice> device = outstream->device;

    SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(osd->clear_buffer_flag);
    SOUNDIO_ATOMIC_STORE(osd->pause_requested, false);

    if (outstream->software_latency == 0.0)
    {
        outstream->software_latency = soundio_double_clamp(
            device->software_latency_min, 1.0, device->software_latency_max);
    }

    osd->period_duration = outstream->software_latency / 2.0;

    int err;
    int buffer_size = outstream->bytes_per_frame * outstream->sample_rate * outstream->software_latency;
    if ((err = soundio_ring_buffer_init(osd->ring_buffer, buffer_size)))
    {
        outstream_destroy_dummy(si, os);
        return err;
    }
    int actual_capacity = soundio_ring_buffer_capacity(osd->ring_buffer);
    osd->buffer_frame_count = actual_capacity / outstream->bytes_per_frame;
    outstream->software_latency = osd->buffer_frame_count / static_cast<double>(outstream->sample_rate);

    osd->cond = soundio_os_cond_create();
    if (!osd->cond)
    {
        outstream_destroy_dummy(si, os);
        return SoundIoErrorNoMem;
    }

    return 0;
}

static int outstream_pause_dummy(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, bool pause)
{
    struct SoundIoOutStreamDummy* osd = &os->backend_data.dummy;
    SOUNDIO_ATOMIC_STORE(osd->pause_requested, pause);
    return 0;
}

static int outstream_start_dummy(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    struct SoundIoOutStreamDummy* osd = &os->backend_data.dummy;
    std::shared_ptr<SoundIo> soundio = si;
    assert(!osd->thread);
    SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(osd->abort_flag);
    int err;
    if ((err = soundio_os_thread_create(playback_thread_run, os, soundio->emit_rtprio_warning, &osd->thread)))
    {
        return err;
    }
    return 0;
}

static int outstream_begin_write_dummy(std::shared_ptr<SoundIoPrivate> si,
                                       std::shared_ptr<SoundIoOutStreamPrivate> os, struct SoundIoChannelArea** out_areas, int* frame_count)
{
    std::shared_ptr<SoundIoOutStream> outstream = os;
    struct SoundIoOutStreamDummy* osd = &os->backend_data.dummy;

    if (*frame_count > osd->frames_left)
        return SoundIoErrorInvalid;

    char* write_ptr = soundio_ring_buffer_write_ptr(osd->ring_buffer);
    for (int ch = 0; ch < outstream->layout.channel_count; ch += 1)
    {
        osd->areas[ch].ptr = write_ptr + outstream->bytes_per_sample * ch;
        osd->areas[ch].step = outstream->bytes_per_frame;
    }

    osd->write_frame_count = *frame_count;
    *out_areas = osd->areas;
    return 0;
}

static int outstream_end_write_dummy(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    struct SoundIoOutStreamDummy* osd = &os->backend_data.dummy;
    std::shared_ptr<SoundIoOutStream> outstream = os;
    int byte_count = osd->write_frame_count * outstream->bytes_per_frame;
    soundio_ring_buffer_advance_write_ptr(osd->ring_buffer, byte_count);
    osd->frames_left -= osd->write_frame_count;
    return 0;
}

static int outstream_clear_buffer_dummy(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    struct SoundIoOutStreamDummy* osd = &os->backend_data.dummy;
    SOUNDIO_ATOMIC_FLAG_CLEAR(osd->clear_buffer_flag);
    soundio_os_cond_signal(osd->cond, NULL);
    return 0;
}

static int outstream_get_latency_dummy(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, double* out_latency)
{
    std::shared_ptr<SoundIoOutStream> outstream = os;
    struct SoundIoOutStreamDummy* osd = &os->backend_data.dummy;
    int fill_bytes = soundio_ring_buffer_fill_count(osd->ring_buffer);

    *out_latency = (fill_bytes / outstream->bytes_per_frame) / static_cast<double>(outstream->sample_rate);
    return 0;
}

static int outstream_get_time_dummy(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, double* out_time)
{
    *out_time = 0;
    return 0;
}


static void instream_destroy_dummy(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    struct SoundIoInStreamDummy* isd = &is->backend_data.dummy;

    if (isd->thread)
    {
        SOUNDIO_ATOMIC_FLAG_CLEAR(isd->abort_flag);
        soundio_os_cond_signal(isd->cond, NULL);
        // soundio_os_thread_destroy(isd->thread);
        isd->thread = NULL;
    }
    soundio_os_cond_destroy(isd->cond);
    isd->cond = NULL;
    isd->ring_buffer = nullptr;

    // soundio_ring_buffer_deinit(&isd->ring_buffer);
}

static int instream_open_dummy(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    struct SoundIoInStreamDummy* isd = &is->backend_data.dummy;
    std::shared_ptr<SoundIoInStream> instream = is;
    std::shared_ptr<SoundIoDevice> device = instream->device;

    SOUNDIO_ATOMIC_STORE(isd->pause_requested, false);

    if (instream->software_latency == 0.0)
    {
        instream->software_latency = soundio_double_clamp(
            device->software_latency_min, 1.0, device->software_latency_max);
    }

    isd->period_duration = instream->software_latency;

    double target_buffer_duration = isd->period_duration * 4.0;

    int err;
    int buffer_size = instream->bytes_per_frame * instream->sample_rate * target_buffer_duration;
    if ((err = soundio_ring_buffer_init(isd->ring_buffer, buffer_size)))
    {
        instream_destroy_dummy(si, is);
        return err;
    }

    int actual_capacity = soundio_ring_buffer_capacity(isd->ring_buffer);
    isd->buffer_frame_count = actual_capacity / instream->bytes_per_frame;

    isd->cond = soundio_os_cond_create();
    if (!isd->cond)
    {
        instream_destroy_dummy(si, is);
        return SoundIoErrorNoMem;
    }

    return 0;
}

static int instream_pause_dummy(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is, bool pause)
{
    struct SoundIoInStreamDummy* isd = &is->backend_data.dummy;
    SOUNDIO_ATOMIC_STORE(isd->pause_requested, pause);
    return 0;
}

static int instream_start_dummy(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    struct SoundIoInStreamDummy* isd = &is->backend_data.dummy;
    std::shared_ptr<SoundIo> soundio = si;
    assert(!isd->thread);
    SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(isd->abort_flag);
    int err;
    if ((err = soundio_os_thread_create(capture_thread_run, is,
                                        soundio->emit_rtprio_warning, &isd->thread)))
    {
        return err;
    }
    return 0;
}

static int instream_begin_read_dummy(std::shared_ptr<SoundIoPrivate> si,
                                     std::shared_ptr<SoundIoInStreamPrivate> is, struct SoundIoChannelArea** out_areas, int* frame_count)
{
    std::shared_ptr<SoundIoInStream> instream = is;
    struct SoundIoInStreamDummy* isd = &is->backend_data.dummy;

    assert(*frame_count <= isd->frames_left);

    char* read_ptr = soundio_ring_buffer_read_ptr(isd->ring_buffer);
    for (int ch = 0; ch < instream->layout.channel_count; ch += 1)
    {
        isd->areas[ch].ptr = read_ptr + instream->bytes_per_sample * ch;
        isd->areas[ch].step = instream->bytes_per_frame;
    }

    isd->read_frame_count = *frame_count;
    *out_areas = isd->areas;

    return 0;
}

static int instream_end_read_dummy(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    struct SoundIoInStreamDummy* isd = &is->backend_data.dummy;
    std::shared_ptr<SoundIoInStream> instream = is;
    int byte_count = isd->read_frame_count * instream->bytes_per_frame;
    soundio_ring_buffer_advance_read_ptr(isd->ring_buffer, byte_count);
    isd->frames_left -= isd->read_frame_count;
    return 0;
}

static int instream_get_latency_dummy(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is, double* out_latency)
{
    std::shared_ptr<SoundIoInStream> instream = is;
    struct SoundIoInStreamDummy* osd = &is->backend_data.dummy;
    int fill_bytes = soundio_ring_buffer_fill_count(osd->ring_buffer);

    *out_latency = (fill_bytes / instream->bytes_per_frame) / (double) instream->sample_rate;
    return 0;
}

static int set_all_device_formats(std::shared_ptr<SoundIoDevice> device)
{
    device->format_count = 18;
    device->formats = ALLOCATE(enum SoundIoFormat, device->format_count);
    if (!device->formats)
        return SoundIoErrorNoMem;

    device->formats[0] = SoundIoFormatFloat32NE;
    device->formats[1] = SoundIoFormatFloat32FE;
    device->formats[2] = SoundIoFormatS32NE;
    device->formats[3] = SoundIoFormatS32FE;
    device->formats[4] = SoundIoFormatU32NE;
    device->formats[5] = SoundIoFormatU32FE;
    device->formats[6] = SoundIoFormatS24NE;
    device->formats[7] = SoundIoFormatS24FE;
    device->formats[8] = SoundIoFormatU24NE;
    device->formats[9] = SoundIoFormatU24FE;
    device->formats[10] = SoundIoFormatFloat64NE;
    device->formats[11] = SoundIoFormatFloat64FE;
    device->formats[12] = SoundIoFormatS16NE;
    device->formats[13] = SoundIoFormatS16FE;
    device->formats[14] = SoundIoFormatU16NE;
    device->formats[15] = SoundIoFormatU16FE;
    device->formats[16] = SoundIoFormatS8;
    device->formats[17] = SoundIoFormatU8;

    return 0;
}

static void set_all_device_sample_rates(std::shared_ptr<SoundIoDevice> device)
{
    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(device);
    SoundIoSampleRateRange range{};
    range.min = SOUNDIO_MIN_SAMPLE_RATE;
    range.max = SOUNDIO_MAX_SAMPLE_RATE;
    device->sample_rates.push_back(range);
}

static int set_all_device_channel_layouts(std::shared_ptr<SoundIoDevice> device)
{
    int count = soundio_channel_layout_builtin_count();

    for (int i = 0; i < count; i++)
    {
        device->layouts.push_back(*soundio_channel_layout_get_builtin(i));
    }
    return 0;
}

int soundio_dummy_init(std::shared_ptr<SoundIoPrivate> si)
{
    std::shared_ptr<SoundIo> soundio = si;
    struct SoundIoDummy* sid = &si->backend_data.dummy;

    sid->mutex = soundio_os_mutex_create();
    if (!sid->mutex)
    {
        destroy_dummy(si);
        return SoundIoErrorNoMem;
    }

    sid->cond = soundio_os_cond_create();
    if (!sid->cond)
    {
        destroy_dummy(si);
        return SoundIoErrorNoMem;
    }

    assert(!si->safe_devices_info);
    si->safe_devices_info = std::make_shared<SoundIoDevicesInfo>();
    if (!si->safe_devices_info)
    {
        destroy_dummy(si);
        return SoundIoErrorNoMem;
    }

    si->safe_devices_info->default_input_index = 0;
    si->safe_devices_info->default_output_index = 0;

    // create output device
    {
        std::shared_ptr<SoundIoDevicePrivate> dev = std::make_shared<SoundIoDevicePrivate>();
        if (!dev)
        {
            destroy_dummy(si);
            return SoundIoErrorNoMem;
        }
        std::shared_ptr<SoundIoDevice> device = dev;

        device->ref_count = 1;
        device->soundio = soundio;
        device->id = *"dummy-out";
        device->name = *"Dummy Output Device";

        int err;
        if ((err = set_all_device_channel_layouts(device)))
        {
            device = nullptr;
            destroy_dummy(si);
            return err;
        }
        if ((err = set_all_device_formats(device)))
        {
            device = nullptr;
            destroy_dummy(si);
            return err;
        }
        set_all_device_sample_rates(device);

        device->software_latency_current = 0.1;
        device->software_latency_min = 0.01;
        device->software_latency_max = 4.0;

        device->sample_rate_current = 48000;
        device->aim = SoundIoDeviceAimOutput;

        si->safe_devices_info->output_devices.push_back(device);
    }

    // create input device
    {
        std::shared_ptr<SoundIoDevicePrivate> dev = std::make_shared<SoundIoDevicePrivate>();
        if (!dev)
        {
            destroy_dummy(si);
            return SoundIoErrorNoMem;
        }
        std::shared_ptr<SoundIoDevice> device = dev;

        device->ref_count = 1;
        device->soundio = soundio;
        device->id = *"dummy-in";
        device->name = *"Dummy Input Device";

        int err;
        if ((err = set_all_device_channel_layouts(device)))
        {
            device = nullptr;
            destroy_dummy(si);
            return err;
        }

        if ((err = set_all_device_formats(device)))
        {
            device = nullptr;
            destroy_dummy(si);
            return err;
        }
        set_all_device_sample_rates(device);
        device->software_latency_current = 0.1;
        device->software_latency_min = 0.01;
        device->software_latency_max = 4.0;
        device->sample_rate_current = 48000;
        device->aim = SoundIoDeviceAimInput;
        si->safe_devices_info->input_devices.push_back(device);
    }


    si->destroy = destroy_dummy;
    si->flush_events = flush_events_dummy;
    si->wait_events = wait_events_dummy;
    si->wakeup = wakeup_dummy;
    si->force_device_scan = force_device_scan_dummy;

    si->outstream_open = outstream_open_dummy;
    si->outstream_destroy = outstream_destroy_dummy;
    si->outstream_start = outstream_start_dummy;
    si->outstream_begin_write = outstream_begin_write_dummy;
    si->outstream_end_write = outstream_end_write_dummy;
    si->outstream_clear_buffer = outstream_clear_buffer_dummy;
    si->outstream_pause = outstream_pause_dummy;
    si->outstream_get_latency = outstream_get_latency_dummy;
    si->outstream_get_time = outstream_get_time_dummy;

    si->instream_open = instream_open_dummy;
    si->instream_destroy = instream_destroy_dummy;
    si->instream_start = instream_start_dummy;
    si->instream_begin_read = instream_begin_read_dummy;
    si->instream_end_read = instream_end_read_dummy;
    si->instream_pause = instream_pause_dummy;
    si->instream_get_latency = instream_get_latency_dummy;

    return 0;
}
