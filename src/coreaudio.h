/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef SOUNDIO_COREAUDIO_H
#define SOUNDIO_COREAUDIO_H

#include "atomics.h"
#include "list.h"
#include "os.h"
#include "soundio_internal.h"

#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>


#ifdef __cplusplus
extern "C"
{
#endif
struct SoundIoPrivate;
struct SoundIoOutStreamPrivate;
struct SoundIoInStreamPrivate;

int soundio_coreaudio_init(std::shared_ptr<SoundIoPrivate> si);

struct SoundIoDeviceCoreAudio
{
    AudioDeviceID device_id;
    UInt32 latency_frames;
};

// SOUNDIO_MAKE_LIST_STRUCT(AudioDeviceID, SoundIoListAudioDeviceID, SOUNDIO_LIST_STATIC)

struct CoreAudioCallback;

struct SoundIoCoreAudio
{
    std::unique_ptr<CoreAudioCallback> callback = std::make_unique<CoreAudioCallback>();
    std::unique_ptr<SoundIoOsMutex> mutex;
    std::unique_ptr<SoundIoOsCond> cond;
    std::unique_ptr<SoundIoOsThread, SoundIoOsThreadDeleter> thread = std::unique_ptr<SoundIoOsThread, SoundIoOsThreadDeleter>(nullptr, SoundIoOsThreadDeleter());
    struct SoundIoAtomicFlag abort_flag;

    // this one is ready to be read with flush_events. protected by mutex
    std::unique_ptr<struct SoundIoDevicesInfo> ready_devices_info;
    struct SoundIoAtomicBool have_devices_flag;
    std::unique_ptr<SoundIoOsCond> have_devices_cond;
    std::unique_ptr<SoundIoOsCond> scan_devices_cond;
    std::vector<AudioDeviceID> registered_listeners;

    struct SoundIoAtomicBool device_scan_queued;
    struct SoundIoAtomicBool service_restarted;
    int shutdown_err;
    bool emitted_shutdown_cb;
};


struct CoreAudioCallback
{
    std::weak_ptr<SoundIoOutStreamPrivate> out_stream;
    std::weak_ptr<SoundIoInStreamPrivate> in_stream;
    std::weak_ptr<SoundIoPrivate> si;

    OSStatus devices_changed(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[]);

    OSStatus service_restarted(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[]);

    OSStatus outstream_device_overload(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[]);

    OSStatus instream_device_overload(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[]) const;

    OSStatus write_callback_ca(AudioUnitRenderActionFlags* io_action_flags, const AudioTimeStamp* in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
                               AudioBufferList* io_data);

    OSStatus read_callback_ca(AudioUnitRenderActionFlags* io_action_flags, const AudioTimeStamp* in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
                              AudioBufferList* io_data);


    void unsubscribe_device_listeners() const;

    static OSStatus on_devices_changed(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[], void* in_client_data);

    static OSStatus on_service_restarted(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[], void* in_client_data);

    static OSStatus on_outstream_device_overload(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[], void* in_client_data);

    static OSStatus on_instream_device_overload(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[], void* in_client_data);

    static OSStatus write_callback(void* userdata, AudioUnitRenderActionFlags* io_action_flags, const AudioTimeStamp* in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
                                   AudioBufferList* io_data);

    static OSStatus read_callback(void* userdata, AudioUnitRenderActionFlags* io_action_flags, const AudioTimeStamp* in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
                                  AudioBufferList* io_data);

    static void unsubscribe_device_listeners(std::shared_ptr<SoundIoPrivate> si);
};


struct SoundIoOutStreamCoreAudio
{
    AudioComponentInstance instance;
    AudioBufferList* io_data;
    int buffer_index;
    int frames_left;
    int write_frame_count;
    double hardware_latency;
    float volume;
    struct SoundIoChannelArea areas[SOUNDIO_MAX_CHANNELS];
};

struct SoundIoInStreamCoreAudio
{
    AudioComponentInstance instance;
    std::unique_ptr<AudioBufferList, decltype(&std::free)> buffer_list{nullptr, std::free};
    int frames_left;
    double hardware_latency;
    struct SoundIoChannelArea areas[SOUNDIO_MAX_CHANNELS];
};
#ifdef __cplusplus
}
#endif
#endif
