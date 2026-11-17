/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef SOUNDIO_COREAUDIO_IOS_H
#define SOUNDIO_COREAUDIO_IOS_H

#include "soundio_internal.h"
#include "os.h"
#include <atomic>

#include <AudioToolbox/AudioToolbox.h>

struct SoundIoPrivate;
struct SoundIoDevicesInfo;
struct SoundIoOutStreamPrivate;
struct SoundIoInStreamPrivate;

int soundio_coreaudio_init(const std::shared_ptr<SoundIoPrivate>& si);

struct CoreAudioInstanceDeleter
{
    void operator()(OpaqueAudioComponentInstance* stream) const;
};


struct CoreAudioCallback
{
#ifdef __OBJC__
    static void on_notification(NSNotification* note, const std::shared_ptr<SoundIoPrivate>& si);

    static void on_route_notification(NSNotification* note, const std::shared_ptr<SoundIoPrivate>& si);
#endif

    static OSStatus write_callback(void* userdata, AudioUnitRenderActionFlags* io_action_flags, const AudioTimeStamp* in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
                                   AudioBufferList* io_data);

    static OSStatus read_callback(void* userdata, AudioUnitRenderActionFlags* io_action_flags, const AudioTimeStamp* in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
                                  AudioBufferList* io_data);
};

struct SoundIoDeviceCoreAudioIOS
{
    UInt32 latency_frames;
};

struct SoundIoCoreAudioIOS
{
    std::unique_ptr<SoundIoOsMutex> mutex;
    std::unique_ptr<SoundIoOsCond> cond;
    std::unique_ptr<SoundIoOsThread> thread = nullptr;

    // this one is ready to be read with flush_events. protected by mutex
    std::unique_ptr<SoundIoDevicesInfo> ready_devices_info;
    std::atomic<bool> have_devices_flag{false};
    std::unique_ptr<SoundIoOsCond> scan_devices_cond;
    std::unique_ptr<SoundIoOsMutex> scan_devices_mutex;

    std::atomic<bool> device_scan_queued{false};
    std::atomic<bool> service_restarted{false};
    int shutdown_err;
    bool emitted_shutdown_cb;
#ifdef __OBJC__
    id notifyCallback;
    id routeCallback;
#endif
};

struct SoundIoOutStreamCoreAudioIOS
{
    std::unique_ptr<OpaqueAudioComponentInstance, CoreAudioInstanceDeleter> instance = std::unique_ptr<OpaqueAudioComponentInstance, CoreAudioInstanceDeleter>(nullptr, CoreAudioInstanceDeleter());
    AudioBufferList* io_data;
    int buffer_index;
    int frames_left;
    int write_frame_count;
    double hardware_latency;
    float volume;
    struct SoundIoChannelArea areas[SOUNDIO_MAX_CHANNELS];
};

struct SoundIoInStreamCoreAudioIOS
{
    std::unique_ptr<OpaqueAudioComponentInstance, CoreAudioInstanceDeleter> instance = std::unique_ptr<OpaqueAudioComponentInstance, CoreAudioInstanceDeleter>(nullptr, CoreAudioInstanceDeleter());
    std::unique_ptr<AudioBufferList, decltype(&std::free)> buffer_list = std::unique_ptr<AudioBufferList, decltype(&std::free)>(nullptr, std::free);
    int frames_left;
    double hardware_latency;
    struct SoundIoChannelArea areas[SOUNDIO_MAX_CHANNELS];
};

#endif
