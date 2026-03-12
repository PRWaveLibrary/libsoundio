/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef SOUNDIO_SOUNDIO_PRIVATE_H
#define SOUNDIO_SOUNDIO_PRIVATE_H

#include <memory>

#include "soundio_internal.h"
#include "config.h"
#include "list.h"
#include <vector>

#ifdef SOUNDIO_HAVE_JACK
#include "jack.h"
#endif

#ifdef SOUNDIO_HAVE_PULSEAUDIO
#include "pulseaudio.h"
#endif

#ifdef SOUNDIO_HAVE_ALSA
#include "alsa.h"
#endif

#ifdef SOUNDIO_HAVE_COREAUDIO
#include "coreaudio.h"
#endif

#ifdef SOUNDIO_HAVE_COREAUDIO_IOS
#include "coreaudio_ios.h"
#endif

#ifdef SOUNDIO_HAVE_OBOE
#include "oboe.h"
#endif

#ifdef SOUNDIO_HAVE_WASAPI
#include "wasapi.h"
#endif

#include "dummy.h"

struct SoundIoBackendData
{
#ifdef SOUNDIO_HAVE_JACK
    struct SoundIoJack jack;
#endif
#ifdef SOUNDIO_HAVE_PULSEAUDIO
    struct SoundIoPulseAudio pulseaudio;
#endif
#ifdef SOUNDIO_HAVE_ALSA
    struct SoundIoAlsa alsa;
#endif
#ifdef SOUNDIO_HAVE_COREAUDIO
    struct SoundIoCoreAudio coreaudio;
#endif
#ifdef SOUNDIO_HAVE_COREAUDIO_IOS
    struct SoundIoCoreAudioIOS coreaudio_ios;
#endif
#ifdef SOUNDIO_HAVE_WASAPI
    struct SoundIoWasapi wasapi;
#endif
#ifdef SOUNDIO_HAVE_OBOE
    struct SoundIoOboe oboe;
#endif

    struct SoundIoDummy dummy;
};

struct SoundIoDeviceBackendData
{
#ifdef SOUNDIO_HAVE_JACK
    struct SoundIoDeviceJack jack;
#endif
#ifdef SOUNDIO_HAVE_PULSEAUDIO
    struct SoundIoDevicePulseAudio pulseaudio;
#endif
#ifdef SOUNDIO_HAVE_ALSA
    struct SoundIoDeviceAlsa alsa;
#endif
#ifdef SOUNDIO_HAVE_COREAUDIO
    struct SoundIoDeviceCoreAudio coreaudio;
#endif
#ifdef SOUNDIO_HAVE_COREAUDIO_IOS
    struct SoundIoDeviceCoreAudioIOS coreaudio_ios;
#endif
#ifdef SOUNDIO_HAVE_WASAPI
    struct SoundIoDeviceWasapi wasapi;
#endif
#ifdef SOUNDIO_HAVE_OBOE
    struct SoundIoDeviceOboe oboe;
#endif
    struct SoundIoDeviceDummy dummy;
};

struct SoundIoOutStreamBackendData
{
#ifdef SOUNDIO_HAVE_JACK
    struct SoundIoOutStreamJack jack;
#endif
#ifdef SOUNDIO_HAVE_PULSEAUDIO
    struct SoundIoOutStreamPulseAudio pulseaudio;
#endif
#ifdef SOUNDIO_HAVE_ALSA
    struct SoundIoOutStreamAlsa alsa;
#endif
#ifdef SOUNDIO_HAVE_COREAUDIO
    struct SoundIoOutStreamCoreAudio coreaudio;
#endif
#ifdef SOUNDIO_HAVE_COREAUDIO_IOS
    struct SoundIoOutStreamCoreAudioIOS coreaudio_ios;
#endif
#ifdef SOUNDIO_HAVE_WASAPI
    struct SoundIoOutStreamWasapi wasapi;
#endif
#ifdef SOUNDIO_HAVE_OBOE
    struct SoundIoOutStreamOboe oboe;
#endif
    struct SoundIoOutStreamDummy dummy;
};

struct SoundIoInStreamBackendData
{
#ifdef SOUNDIO_HAVE_JACK
    struct SoundIoInStreamJack jack;
#endif
#ifdef SOUNDIO_HAVE_PULSEAUDIO
    struct SoundIoInStreamPulseAudio pulseaudio;
#endif
#ifdef SOUNDIO_HAVE_ALSA
    struct SoundIoInStreamAlsa alsa;
#endif
#ifdef SOUNDIO_HAVE_COREAUDIO
    struct SoundIoInStreamCoreAudio coreaudio;
#endif
#ifdef SOUNDIO_HAVE_COREAUDIO_IOS
    struct SoundIoInStreamCoreAudioIOS coreaudio_ios;
#endif
#ifdef SOUNDIO_HAVE_WASAPI
    struct SoundIoInStreamWasapi wasapi;
#endif
#ifdef SOUNDIO_HAVE_OBOE
    struct SoundIoInStreamOboe oboe;
#endif
    struct SoundIoInStreamDummy dummy;
};

// SOUNDIO_MAKE_LIST_STRUCT(struct SoundIoDevice*, SoundIoListDevicePtr, SOUNDIO_LIST_NOT_STATIC)
//
// SOUNDIO_MAKE_LIST_PROTO(struct SoundIoDevice*, SoundIoListDevicePtr, SOUNDIO_LIST_NOT_STATIC)

struct SoundIoDevicesInfo
{
    std::vector<std::shared_ptr<SoundIoDevice> > input_devices;
    std::vector<std::shared_ptr<SoundIoDevice> > output_devices;
    // can be -1 when default device is unknown
    int default_output_index;
    int default_input_index;
};

struct SoundIoOutStreamPrivate : SoundIoOutStream
{
    struct SoundIoOutStreamBackendData backend_data;
};

struct SoundIoInStreamPrivate : SoundIoInStream
{
    struct SoundIoInStreamBackendData backend_data;
};

struct SoundIoPrivate : public SoundIo
{
    // Safe to read from a single thread without a mutex.
    std::shared_ptr<SoundIoDevicesInfo> safe_devices_info;

    void (*destroy)(std::shared_ptr<SoundIoPrivate>);

    void (*flush_events)(std::shared_ptr<SoundIoPrivate>);

    void (*wait_events)(std::shared_ptr<SoundIoPrivate>);

    void (*wakeup)(std::shared_ptr<SoundIoPrivate>);

    void (*force_device_scan)(std::shared_ptr<SoundIoPrivate>);

    int (*outstream_open)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoOutStreamPrivate>);

    void (*outstream_destroy)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoOutStreamPrivate>);

    int (*outstream_start)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoOutStreamPrivate>);

    int (*outstream_begin_write)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoOutStreamPrivate>, struct SoundIoChannelArea** out_areas, int* out_frame_count);

    int (*outstream_end_write)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoOutStreamPrivate>);

    int (*outstream_clear_buffer)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoOutStreamPrivate>);

    int (*outstream_pause)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoOutStreamPrivate>, bool pause);

    int (*outstream_get_latency)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoOutStreamPrivate>, double* out_latency);

    int (*outstream_set_volume)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoOutStreamPrivate>, float volume);

    int (*outstream_get_time)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoOutStreamPrivate>, double* out_time);

    int (*instream_open)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoInStreamPrivate>);

    void (*instream_destroy)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoInStreamPrivate>);

    int (*instream_start)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoInStreamPrivate>);

    int (*instream_begin_read)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoInStreamPrivate>, struct SoundIoChannelArea** out_areas, int* out_frame_count);

    int (*instream_end_read)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoInStreamPrivate>);

    int (*instream_pause)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoInStreamPrivate>, bool pause);

    int (*instream_get_latency)(std::shared_ptr<SoundIoPrivate>, std::shared_ptr<SoundIoInStreamPrivate>, double* out_latency);

    struct SoundIoBackendData backend_data;
};

// SOUNDIO_MAKE_LIST_STRUCT(struct SoundIoSampleRateRange, SoundIoListSampleRateRange, SOUNDIO_LIST_NOT_STATIC)
//
// SOUNDIO_MAKE_LIST_PROTO(struct SoundIoSampleRateRange, SoundIoListSampleRateRange, SOUNDIO_LIST_NOT_STATIC)

struct SoundIoDevicePrivate : SoundIoDevice
{
    ~SoundIoDevicePrivate() override
    {
        if (backend_data.wasapi.mm_device)
        {
            backend_data.wasapi.mm_device->Release();
            backend_data.wasapi.mm_device = nullptr;
        }
        // free(pub.id);
        // free(pub.name);
        //
        // if (pub.sample_rates != &prealloc_sample_rate_range &&
        //     pub.sample_rates != sample_rates.items)
        // {
        //     free(pub.sample_rates);
        // }
        // SoundIoListSampleRateRange_deinit(&sample_rates);
        // if (pub.formats != &prealloc_format)
        // {
        //     free(pub.formats);
        // }
        //
        // if (pub.layouts != &pub.current_layout)
        // {
        //     free(pub.layouts);
        // }
    }

    struct SoundIoDeviceBackendData backend_data;

    void (*destruct)(SoundIoDevicePrivate*);

    std::vector<SoundIoSampleRateRange> sample_rates;
    enum SoundIoFormat prealloc_format;
};

// #ifdef __cplusplus
// extern "C"
// {
// #endif
// void soundio_destroy_devices_info(std::shared_ptr<SoundIoDevicesInfo> devices_info);
//
// #ifdef __cplusplus
// }
// #endif


static const int SOUNDIO_MIN_SAMPLE_RATE = 8000;
static const int SOUNDIO_MAX_SAMPLE_RATE = 5644800;

#endif
