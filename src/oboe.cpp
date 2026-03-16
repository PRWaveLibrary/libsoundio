//
// Created by Administrator on 2026/3/9.
//

#include "oboe.h"

#include "soundio_private.h"

// static const int OUTPUT_ELEMENT = 0;
// static const int INPUT_ELEMENT = 1;

static enum SoundIoDeviceAim aims[] = {
    SoundIoDeviceAimInput,
    SoundIoDeviceAimOutput,
};

// static void deinit_sound_io_devices_info(struct SoundIoDevicesInfo** sidi)
// {
//     if (*sidi == NULL)
//     {
//         return;
//     }
//     soundio_destroy_devices_info(*sidi);
//     *sidi = NULL;
// }


static int refresh_devices(std::shared_ptr<SoundIoPrivate>& si)
{
    std::shared_ptr<SoundIo> soundio = si;
    SoundIoOboe& sio = si->backend_data->oboe;

    std::unique_ptr<SoundIoDevicesInfo> devices_info = std::make_unique<SoundIoDevicesInfo>();
    // if (!devices_info)
    // {
    //     return SoundIoErrorNoMem;
    // }

    devices_info->default_output_index = -1;
    devices_info->default_input_index = -1;

    for (int aim_i = 0; aim_i < std::size(aims); aim_i += 1)
    {
        enum SoundIoDeviceAim aim = aims[aim_i];
        std::shared_ptr<SoundIoDevicePrivate> dev = std::make_shared<SoundIoDevicePrivate>();

        // if (!dev)
        // {
        //     return SoundIoErrorNoMem;
        // }

        // dev->ref_count = 1;
        dev->soundio = soundio;
        dev->is_raw = false;
        dev->aim = aim;
        dev->name = aim == SoundIoDeviceAimInput ? L"Oboe Input" : L"Oboe Output";
        dev->id = aim == SoundIoDeviceAimInput ? L"Oboe Input Device" : L"Oboe Output Device";

        dev->formats.push_back(SoundIoFormatFloat32LE);

        if (aim == SoundIoDeviceAimOutput)
        {
            dev->current_layout = *soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdStereo);
        }
        else
        {
            dev->current_layout = *soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdMono);
        }

        dev->layouts.push_back(dev->current_layout);
        dev->sample_rate_current = 0; // 系统给什么用什么

        SoundIoSampleRateRange range{};
        range.min = dev->sample_rate_current;
        range.max = dev->sample_rate_current;
        dev->sample_rates.push_back(range);

        dev->software_latency_current = 0.01; // 默认十毫秒
        dev->software_latency_min = 0.001;
        dev->software_latency_max = 1;

        std::vector<std::shared_ptr<SoundIoDevice>>* device_list;
        if (dev->aim == SoundIoDeviceAimOutput)
        {
            device_list = &devices_info->output_devices;
        }
        else
        {
            assert(dev->aim == SoundIoDeviceAimInput);
            device_list = &devices_info->input_devices;
        }

        device_list->push_back(dev);
    }

    soundio_os_mutex_lock(sio.mutex);
    sio.ready_devices_info = std::move(devices_info);
    SOUNDIO_ATOMIC_STORE(sio.have_devices_flag, true);
    soundio_os_cond_signal(sio.cond.get(), sio.mutex.get());
    soundio->on_events_signal(soundio);
    soundio_os_mutex_unlock(sio.mutex);

    return 0;
}

static void shutdown_backend(std::shared_ptr<SoundIoPrivate> si, int err)
{
    std::shared_ptr<SoundIo> soundio = si;
    SoundIoOboe& sio = si->backend_data->oboe;
    soundio_os_mutex_lock(sio.mutex);
    sio.shutdown_err = err;
    soundio_os_cond_signal(sio.cond.get(), sio.mutex.get());
    soundio->on_events_signal(soundio);
    soundio_os_mutex_unlock(sio.mutex);
}

static void device_thread_run(std::shared_ptr<void> arg)
{
    std::shared_ptr<SoundIoPrivate> si = std::static_pointer_cast<SoundIoPrivate>(arg);
    SoundIoOboe& sio = si->backend_data->oboe;
    int err;

    soundio_os_mutex_lock(sio.scan_devices_mutex);
    for (;;)
    {
        if (!SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(sio.abort_flag))
        {
            break;
        }
        if (SOUNDIO_ATOMIC_LOAD(sio.device_scan_queued))
        {
            SOUNDIO_ATOMIC_STORE(sio.device_scan_queued, false);
            soundio_os_mutex_unlock(sio.scan_devices_mutex);
            err = refresh_devices(si);
            if (err)
            {
                shutdown_backend(si, err);
                return;
            }

            soundio_os_mutex_lock(sio.scan_devices_mutex);
            continue;
        }
        soundio_os_cond_wait(sio.scan_devices_cond.get(), sio.scan_devices_mutex.get());
    }
    soundio_os_mutex_unlock(sio.scan_devices_mutex);
}

static void my_flush_events(std::shared_ptr<SoundIoPrivate>& si, bool wait)
{
    std::shared_ptr<SoundIo> soundio = si;
    SoundIoOboe& sio = si->backend_data->oboe;

    bool change = false;
    bool cb_shutdown = false;
    // std::shared_ptr<SoundIoDevicesInfo> old_devices_info = NULL;

    soundio_os_mutex_lock(sio.mutex);

    // block until have devices
    while (wait || (!SOUNDIO_ATOMIC_LOAD(sio.have_devices_flag) && !sio.shutdown_err))
    {
        soundio_os_cond_wait(sio.cond.get(), sio.mutex.get());
        wait = false;
    }

    if (sio.shutdown_err && !sio.emitted_shutdown_cb)
    {
        sio.emitted_shutdown_cb = true;
        cb_shutdown = true;
    }
    else if (sio.ready_devices_info)
    {
        si->safe_devices_info = std::move(sio.ready_devices_info);
        change = true;
    }

    soundio_os_mutex_unlock(sio.mutex);

    if (cb_shutdown)
        soundio->on_backend_disconnect(soundio, sio.shutdown_err);
    else if (change)
        soundio->on_devices_change(soundio);
}


static void flush_events_ca(std::shared_ptr<SoundIoPrivate> si)
{
    my_flush_events(si, false);
}

static void wait_events_oboe(std::shared_ptr<SoundIoPrivate> si)
{
    my_flush_events(si, true);
}

static void wakeup_oboe(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoOboe& sio = si->backend_data->oboe;
    soundio_os_cond_signal(sio.cond.get(), sio.mutex.get());
}

static void force_device_scan_oboe(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoOboe& sio = si->backend_data->oboe;
    soundio_os_mutex_lock(sio.scan_devices_mutex);
    SOUNDIO_ATOMIC_STORE(sio.device_scan_queued, true);
    soundio_os_cond_signal(sio.scan_devices_cond.get(), sio.scan_devices_mutex.get());
    soundio_os_mutex_unlock(sio.scan_devices_mutex);
}

static void outstream_destroy_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    struct SoundIoOutStreamOboe* oso = &os->backend_data.oboe;
    oso->audio_stream = nullptr;
    oso->callback = nullptr;
}

void OboeStreamDeleter::operator()(oboe::AudioStream* stream) const
{
    if (stream)
    {
        stream->close();
        delete stream;
    }
}

static int outstream_open_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    SoundIoOutStreamOboe& oso = os->backend_data.oboe;
    std::shared_ptr<SoundIoOutStream> outstream = os;
    std::shared_ptr<SoundIoDevice> device = outstream->device;

    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(device);
    std::shared_ptr<SoundIoDeviceOboe> sdo = dev->backend_data.oboe;

    if (outstream->software_latency == 0.0)
    {
        outstream->software_latency = device->software_latency_current;
    }

    outstream->software_latency = soundio_double_clamp(device->software_latency_min, outstream->software_latency, device->software_latency_max);

    // Allocate the callback object on the heap
    oso.callback = std::make_unique<oboe_callback>(os);

    oboe::AudioStreamBuilder builder;
    builder.setSharingMode(oboe::SharingMode::Exclusive)
            ->setUsage(oboe::Usage::Game)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setFormat(oboe::AudioFormat::Float)
            ->setChannelCount(outstream->layout.channel_count)
            ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
            ->setDataCallback(oso.callback.get());

    oboe::AudioStream* raw_stream = nullptr;
    oboe::Result result = builder.openStream(&raw_stream);
    if (result != oboe::Result::OK)
    {
        outstream_destroy_oboe(si, os);
        return SoundIoErrorOpeningDevice;
    }

    oso.audio_stream = std::unique_ptr<oboe::AudioStream, OboeStreamDeleter>(raw_stream);
    raw_stream = nullptr;

    outstream->sample_rate = oso.audio_stream->getSampleRate();
    outstream->bytes_per_frame = oso.audio_stream->getBytesPerFrame();
    outstream->bytes_per_sample = oso.audio_stream->getBytesPerSample();
    sdo->latency_frames = oso.audio_stream->getBufferSizeInFrames();
    if (sdo->latency_frames <= 0)
    {
        sdo->latency_frames = oso.audio_stream->getBufferCapacityInFrames();
    }

    oso.hardware_latency = static_cast<double>(sdo->latency_frames) / outstream->sample_rate;
    os->backend_data.oboe.volume = 1;

    return 0;
}

static int outstream_pause_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, bool pause)
{
    struct SoundIoOutStreamOboe* osca = &os->backend_data.oboe;
    std::unique_ptr<oboe::AudioStream, OboeStreamDeleter>& stream = osca->audio_stream;
    oboe::Result result;
    if (pause)
    {
        result = stream->requestPause();
        if (result != oboe::Result::OK)
        {
            return SoundIoErrorStreaming;
        }
    }
    else
    {
        if (stream->getState() == oboe::StreamState::Paused)
        {
            result = stream->requestFlush();
            if (result != oboe::Result::OK)
            {
                return SoundIoErrorStreaming;
            }
        }

        result = stream->requestStart();
        if (result != oboe::Result::OK)
        {
            return SoundIoErrorStreaming;
        }
    }

    return 0;
}

static int outstream_start_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    return outstream_pause_oboe(si, os, false);
}


static int outstream_begin_write_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, struct SoundIoChannelArea** out_areas, int* frame_count)
{
    std::shared_ptr<SoundIoOutStream> outstream = os;
    struct SoundIoOutStreamOboe& oso = os->backend_data.oboe;

    oso.write_frame_count = oso.request_num_frames;
    *frame_count = oso.write_frame_count;

    for (int ch = 0; ch < outstream->layout.channel_count; ch += 1)
    {
        oso.areas[ch].ptr = reinterpret_cast<char*>(oso.request_audio_data) + outstream->bytes_per_sample * ch;
        oso.areas[ch].step = outstream->bytes_per_frame;
    }
    *out_areas = oso.areas;
    return 0;
}

static int outstream_end_write_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    struct SoundIoOutStreamOboe* osca = &os->backend_data.oboe;
    osca->buffer_index += 1;
    return 0;
}

static int outstream_clear_buffer_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    return SoundIoErrorIncompatibleBackend;
}

static int outstream_get_latency_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, double* out_latency)
{
    struct SoundIoOutStreamOboe* oso = &os->backend_data.oboe;

    if (oso->audio_stream)
    {
        std::unique_ptr<oboe::AudioStream, OboeStreamDeleter>& stream = oso->audio_stream;
        // 通过 Oboe 进行实时运算，能测算出真实物理和缓冲折叠造成的完整精确延迟。
        auto result = stream->calculateLatencyMillis();
        if (result.error() == oboe::Result::OK)
        {
            *out_latency = result.value() / 1000.0;
            return 0;
        }
    }

    // 后备方案：如果在未运行等状态下出现错误，则回落到初始化估算结果。
    *out_latency = oso->hardware_latency;
    return 0;
}

static int outstream_set_volume_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, float volume)
{
    std::shared_ptr<SoundIoOutStream> outstream = os;
    outstream->volume = volume;
    return 0;
}


static int instream_open_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    return SoundIoErrorBackendUnavailable;
}

static int instream_pause_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is, bool pause)
{
    return SoundIoErrorBackendUnavailable;
}

static int instream_start_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    return SoundIoErrorBackendUnavailable;
}

static int instream_begin_read_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is, struct SoundIoChannelArea** out_areas, int* frame_count)
{
    return SoundIoErrorBackendUnavailable;
}

static int instream_end_read_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    return SoundIoErrorBackendUnavailable;
}

static int instream_get_latency_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is, double* out_latency)
{
    return SoundIoErrorBackendUnavailable;
}


static void instream_destroy_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
}


static void destroy_oboe(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoOboe& sio = si->backend_data->oboe;

    if (sio.thread)
    {
        soundio_os_mutex_lock(sio.scan_devices_mutex);
        SOUNDIO_ATOMIC_FLAG_CLEAR(sio.abort_flag);
        soundio_os_cond_signal(sio.scan_devices_cond.get(), sio.scan_devices_mutex.get());
        soundio_os_mutex_unlock(sio.scan_devices_mutex);
        sio.thread = nullptr;
    }

    sio.cond = nullptr;
    sio.mutex = nullptr;
    sio.scan_devices_cond = nullptr;
    sio.scan_devices_mutex = nullptr;
}

int soundio_oboe_init(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoOboe& sio = si->backend_data->oboe;

    int32_t err;
    SOUNDIO_ATOMIC_STORE(sio.have_devices_flag, false);
    SOUNDIO_ATOMIC_STORE(sio.device_scan_queued, true);
    SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(sio.abort_flag);

    sio.mutex = soundio_os_mutex_create();
    if (!sio.mutex)
    {
        destroy_oboe(si);
        return SoundIoErrorNoMem;
    }

    sio.cond = soundio_os_cond_create();
    if (!sio.cond)
    {
        destroy_oboe(si);
        return SoundIoErrorNoMem;
    }

    sio.scan_devices_mutex = soundio_os_mutex_create();
    if (!sio.scan_devices_mutex)
    {
        destroy_oboe(si);
        return SoundIoErrorNoMem;
    }

    sio.scan_devices_cond = soundio_os_cond_create();
    if (!sio.scan_devices_cond)
    {
        destroy_oboe(si);
        return SoundIoErrorNoMem;
    }

    if ((err = soundio_os_thread_create(device_thread_run, si, nullptr, &sio.thread)))
    {
        destroy_oboe(si);
        return err;
    }

    si->destroy = destroy_oboe;
    si->flush_events = flush_events_ca;
    si->wait_events = wait_events_oboe;
    si->wakeup = wakeup_oboe;
    si->force_device_scan = force_device_scan_oboe;

    si->outstream_open = outstream_open_oboe;
    si->outstream_destroy = outstream_destroy_oboe;
    si->outstream_start = outstream_start_oboe;
    si->outstream_begin_write = outstream_begin_write_oboe;
    si->outstream_end_write = outstream_end_write_oboe;
    si->outstream_clear_buffer = outstream_clear_buffer_oboe;
    si->outstream_pause = outstream_pause_oboe;
    si->outstream_get_latency = outstream_get_latency_oboe;
    si->outstream_set_volume = outstream_set_volume_oboe;

    si->instream_open = instream_open_oboe;
    si->instream_destroy = instream_destroy_oboe;
    si->instream_start = instream_start_oboe;
    si->instream_begin_read = instream_begin_read_oboe;
    si->instream_end_read = instream_end_read_oboe;
    si->instream_pause = instream_pause_oboe;
    si->instream_get_latency = instream_get_latency_oboe;

    return 0;
}
