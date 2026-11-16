/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef SOUNDIO_SOUNDIO_PRIVATE_H
#define SOUNDIO_SOUNDIO_PRIVATE_H

#include <memory>

#include <vector>
#include "config.h"
#include "soundio_internal.h"

#ifdef SOUNDIO_HAVE_PIPEWIRE
#include "pipewire.h"
#endif

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
#ifdef SOUNDIO_HAVE_PIPEWIRE
    SoundIoPipeWire pipe_wire;
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
    std::shared_ptr<SoundIoDeviceJack> jack = std::make_shared<SoundIoDeviceJack>();
#endif
#ifdef SOUNDIO_HAVE_PULSEAUDIO
    std::shared_ptr<SoundIoDevicePulseAudio> pulseaudio = std::make_shared<SoundIoDevicePulseAudio>();
#endif
#ifdef SOUNDIO_HAVE_ALSA
    std::shared_ptr<SoundIoDeviceAlsa> alsa = std::make_shared<SoundIoDeviceAlsa>();
#endif
#ifdef SOUNDIO_HAVE_PIPEWIRE
    std::shared_ptr<SoundIoDevicePipeWire> pipe_wire = std::make_shared<SoundIoDevicePipeWire>();
#endif
#ifdef SOUNDIO_HAVE_COREAUDIO
    std::shared_ptr<SoundIoDeviceCoreAudio> coreaudio = std::make_shared<SoundIoDeviceCoreAudio>();
#endif
#ifdef SOUNDIO_HAVE_COREAUDIO_IOS
    std::shared_ptr<SoundIoDeviceCoreAudioIOS> coreaudio_ios = std::make_shared<SoundIoDeviceCoreAudioIOS>();
#endif
#ifdef SOUNDIO_HAVE_WASAPI
    std::shared_ptr<SoundIoDeviceWasapi> wasapi = std::make_shared<SoundIoDeviceWasapi>();
#endif
#ifdef SOUNDIO_HAVE_OBOE
    std::shared_ptr<SoundIoDeviceOboe> oboe = std::make_shared<SoundIoDeviceOboe>();
#endif
    std::shared_ptr<SoundIoDeviceDummy> dummy = std::make_shared<SoundIoDeviceDummy>();
};

struct SoundIoOutStreamBackendData
{
#ifdef SOUNDIO_HAVE_JACK
    SoundIoOutStreamJack jack;
#endif
#ifdef SOUNDIO_HAVE_PULSEAUDIO
    SoundIoOutStreamPulseAudio pulseaudio;
#endif
#ifdef SOUNDIO_HAVE_ALSA
    SoundIoOutStreamAlsa alsa;
#endif
#ifdef SOUNDIO_HAVE_PIPEWIRE
    SoundIoOutStreamPipeWire pipe_wire;
#endif
#ifdef SOUNDIO_HAVE_COREAUDIO
    SoundIoOutStreamCoreAudio coreaudio;
#endif
#ifdef SOUNDIO_HAVE_COREAUDIO_IOS
    SoundIoOutStreamCoreAudioIOS coreaudio_ios;
#endif
#ifdef SOUNDIO_HAVE_WASAPI
    SoundIoOutStreamWasapi wasapi;
#endif
#ifdef SOUNDIO_HAVE_OBOE
    SoundIoOutStreamOboe oboe;
#endif
    SoundIoOutStreamDummy dummy;
};

struct SoundIoInStreamBackendData
{
#ifdef SOUNDIO_HAVE_JACK
    SoundIoInStreamJack jack;
#endif
#ifdef SOUNDIO_HAVE_PULSEAUDIO
    SoundIoInStreamPulseAudio pulseaudio;
#endif
#ifdef SOUNDIO_HAVE_ALSA
    SoundIoInStreamAlsa alsa;
#endif
#ifdef SOUNDIO_HAVE_PIPEWIRE
    SoundIoInStreamPipeWire pipe_wire;
#endif
#ifdef SOUNDIO_HAVE_COREAUDIO
    SoundIoInStreamCoreAudio coreaudio;
#endif
#ifdef SOUNDIO_HAVE_COREAUDIO_IOS
    SoundIoInStreamCoreAudioIOS coreaudio_ios;
#endif
#ifdef SOUNDIO_HAVE_WASAPI
    SoundIoInStreamWasapi wasapi;
#endif
#ifdef SOUNDIO_HAVE_OBOE
    SoundIoInStreamOboe oboe;
#endif
    SoundIoInStreamDummy dummy;
};

// SOUNDIO_MAKE_LIST_STRUCT(struct SoundIoDevice*, SoundIoListDevicePtr, SOUNDIO_LIST_NOT_STATIC)
//
// SOUNDIO_MAKE_LIST_PROTO(struct SoundIoDevice*, SoundIoListDevicePtr, SOUNDIO_LIST_NOT_STATIC)

struct SoundIoDevicesInfo
{
    std::vector<std::shared_ptr<SoundIoDevice>> input_devices;
    std::vector<std::shared_ptr<SoundIoDevice>> output_devices;
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
    std::unique_ptr<SoundIoDevicesInfo> safe_devices_info;
    std::unique_ptr<SoundIoBackendData> backend_data;

    void (*destroy)(const std::shared_ptr<SoundIoPrivate>&);

    void (*flush_events)(const std::shared_ptr<SoundIoPrivate>&);

    void (*wait_events)(const std::shared_ptr<SoundIoPrivate>&);

    void (*wakeup)(const std::shared_ptr<SoundIoPrivate>&);

    void (*force_device_scan)(const std::shared_ptr<SoundIoPrivate>&);

    int (*outstream_open)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoOutStreamPrivate>&);

    void (*outstream_destroy)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoOutStreamPrivate>&);

    int (*outstream_start)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoOutStreamPrivate>&);

    int (*outstream_begin_write)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoOutStreamPrivate>&, SoundIoChannelArea** out_areas, int frame_count);

    int (*outstream_end_write)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoOutStreamPrivate>&);

    int (*outstream_clear_buffer)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoOutStreamPrivate>&);

    int (*outstream_pause)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoOutStreamPrivate>&, bool pause);

    int (*outstream_get_latency)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoOutStreamPrivate>&, double* out_latency);

    int (*outstream_set_volume)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoOutStreamPrivate>&, float volume);

    int (*outstream_get_time)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoOutStreamPrivate>&, double* out_time);

    int (*instream_open)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoInStreamPrivate>&);

    void (*instream_destroy)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoInStreamPrivate>&);

    int (*instream_start)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoInStreamPrivate>&);

    int (*instream_begin_read)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoInStreamPrivate>&, SoundIoChannelArea** out_areas, int* out_frame_count);

    int (*instream_end_read)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoInStreamPrivate>&);

    int (*instream_pause)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoInStreamPrivate>&, bool pause);

    int (*instream_get_latency)(const std::shared_ptr<SoundIoPrivate>&, const std::shared_ptr<SoundIoInStreamPrivate>&, double* out_latency);
};

// SOUNDIO_MAKE_LIST_STRUCT(struct SoundIoSampleRateRange, SoundIoListSampleRateRange, SOUNDIO_LIST_NOT_STATIC)
//
// SOUNDIO_MAKE_LIST_PROTO(struct SoundIoSampleRateRange, SoundIoListSampleRateRange, SOUNDIO_LIST_NOT_STATIC)

struct SoundIoDevicePrivate : SoundIoDevice
{
    SoundIoDeviceBackendData backend_data;

    void (*destruct)(SoundIoDevicePrivate*);

    std::vector<SoundIoSampleRateRange> sample_rates;
    SoundIoFormat prealloc_format;
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
