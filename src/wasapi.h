/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef SOUNDIO_WASAPI_H
#define SOUNDIO_WASAPI_H

#include <atomic>
#include <string>
#include "os.h"
#include "soundio_internal.h"

// #define CINTERFACE
#define COBJMACROS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <windows.h>

struct IMMDeviceDeleter
{
    void operator()(IMMDevice* device) const
    {
        if (device == nullptr)
        {
            return;
        }

        device->Release();
    }
};

struct SoundIoPrivate;

int soundio_wasapi_init(const std::shared_ptr<SoundIoPrivate>& si);

struct SoundIoDeviceWasapi
{
    double period_duration;
    std::shared_ptr<IMMDevice> mm_device;
};


struct SoundIoOutStreamWasapi
{
    IAudioClient* audio_client;
    IAudioClockAdjustment* audio_clock_adjustment;
    IAudioRenderClient* audio_render_client;
    IAudioSessionControl* audio_session_control;
    // ISimpleAudioVolume* audio_volume_control;
    IAudioClock* audio_clock;
    std::wstring stream_name;
    bool need_resample;
    std::unique_ptr<SoundIoOsThread> thread = nullptr;
    std::unique_ptr<SoundIoOsMutex> mutex;
    std::unique_ptr<SoundIoOsCond> cond;
    std::unique_ptr<SoundIoOsCond> start_cond;
    bool is_raw;
    int writable_frame_count;
    int buffer_frame_count;
    int write_frame_count;
    HANDLE h_event;
    std::atomic_bool desired_pause_state{false};
    std::atomic_flag pause_resume_flag = ATOMIC_FLAG_INIT;
    std::atomic_flag clear_buffer_flag = ATOMIC_FLAG_INIT;
    bool is_paused;
    bool open_complete;
    int open_err;
    bool started;
    int min_padding_frames;
    SoundIoChannelArea areas[SOUNDIO_MAX_CHANNELS];
};

struct SoundIoInStreamWasapi
{
    IAudioClient* audio_client;
    IAudioCaptureClient* audio_capture_client;
    IAudioSessionControl* audio_session_control;
    std::wstring stream_name;
    std::unique_ptr<SoundIoOsThread> thread = nullptr;
    std::unique_ptr<SoundIoOsMutex> mutex;
    std::unique_ptr<SoundIoOsCond> cond;
    std::unique_ptr<SoundIoOsCond> start_cond;
    bool is_raw;
    std::int32_t readable_frame_count;
    std::uint32_t buffer_frame_count;
    std::int32_t read_frame_count;
    HANDLE h_event;
    bool is_paused;
    bool open_complete;
    std::int32_t open_err;
    bool started;
    char* read_buf;
    std::int32_t read_buf_frames_left;
    std::int32_t opened_buf_frames;
    SoundIoChannelArea areas[SOUNDIO_MAX_CHANNELS];
};

class soundio_NotificationClient;

struct SoundIoWasapi
{
    std::unique_ptr<SoundIoOsMutex> mutex;
    std::unique_ptr<SoundIoOsCond> cond;
    std::unique_ptr<SoundIoOsCond> scan_devices_cond;
    std::unique_ptr<SoundIoOsMutex> scan_devices_mutex;
    std::unique_ptr<SoundIoOsThread> thread = nullptr;
    // this one is ready to be read with flush_events. protected by mutex
    std::unique_ptr<struct SoundIoDevicesInfo> ready_devices_info;
    std::atomic_bool have_devices_flag = {false};
    std::atomic_bool device_scan_queued = {false};
    int shutdown_err;
    bool emitted_shutdown_cb;

    IMMDeviceEnumerator* device_enumerator;
    std::unique_ptr<soundio_NotificationClient> device_events;
};

class soundio_NotificationClient : public IMMNotificationClient
{
public:
    virtual ~soundio_NotificationClient() = default;

    std::weak_ptr<SoundIoPrivate> si;

    soundio_NotificationClient(const std::weak_ptr<SoundIoPrivate>& si)
    {
        this->si = si;
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv);

    IFACEMETHODIMP_(ULONG) AddRef();

    IFACEMETHODIMP_(ULONG) Release();

    // IMMNotificationClient methods
    IFACEMETHODIMP OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState);

    IFACEMETHODIMP OnDeviceAdded(LPCWSTR pwstrDeviceId);

    IFACEMETHODIMP OnDeviceRemoved(LPCWSTR pwstrDeviceId);

    IFACEMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId);

    IFACEMETHODIMP OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key);
};

#endif
