/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef SOUNDIO_COREAUDIO_H
#define SOUNDIO_COREAUDIO_H

#include <atomic>
#include "os.h"
#include "soundio_internal.h"

#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>

struct SoundIoPrivate;
struct SoundIoOutStreamPrivate;
struct SoundIoInStreamPrivate;

int soundio_coreaudio_init(const std::shared_ptr<SoundIoPrivate>& si);

struct SoundIoDeviceCoreAudio
{
    AudioDeviceID device_id;
    UInt32 latency_frames;
};

// SOUNDIO_MAKE_LIST_STRUCT(AudioDeviceID, SoundIoListAudioDeviceID, SOUNDIO_LIST_STATIC)

/// 所有回调的 userdata（refCon）都是对应对象的裸指针：
///   * 设备级/全局 listener  → SoundIoPrivate*
///   * 渲染/采集/overload 回调 → 对应 stream 的 SoundIo*StreamPrivate*
/// 需要 shared_ptr 时统一用 enable_shared_from_this 还原（无额外成员、无生命周期约定）
struct CoreAudioCallback
{
    static OSStatus on_devices_changed(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[], void* in_client_data);

    static OSStatus on_service_restarted(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[], void* in_client_data);

    static OSStatus on_outstream_device_overload(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[], void* in_client_data);

    static OSStatus on_instream_device_overload(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[], void* in_client_data);

    static OSStatus write_callback(void* userdata, AudioUnitRenderActionFlags* io_action_flags, const AudioTimeStamp* in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
                                   AudioBufferList* io_data);

    static OSStatus read_callback(void* userdata, AudioUnitRenderActionFlags* io_action_flags, const AudioTimeStamp* in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
                                  AudioBufferList* io_data);

    static void unsubscribe_device_listeners(const std::shared_ptr<SoundIoPrivate>& si);
};

struct SoundIoCoreAudio
{
    std::unique_ptr<SoundIoOsMutex> mutex;
    std::unique_ptr<SoundIoOsCond> cond;
    std::unique_ptr<SoundIoOsThread> thread = nullptr;

    // this one is ready to be read with flush_events. protected by mutex
    std::unique_ptr<struct SoundIoDevicesInfo> ready_devices_info;
    std::atomic_bool have_devices_flag{false};

    std::unique_ptr<SoundIoOsCond> scan_devices_cond;
    std::unique_ptr<SoundIoOsMutex> scan_devices_mutex;

    std::vector<AudioDeviceID> registered_listeners;

    std::atomic_bool device_scan_queued{false};
    std::atomic_bool service_restarted{false};
    std::atomic_int32_t shutdown_err;
    std::atomic_bool emitted_shutdown_cb;
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
    //CoreAudioy
    std::unique_ptr<AudioBufferList, decltype(&std::free)> buffer_list{nullptr, std::free};
    int frames_left;
    double hardware_latency;
    struct SoundIoChannelArea areas[SOUNDIO_MAX_CHANNELS];
};

#endif
