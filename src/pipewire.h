//
// Created by mda on 2026/11/16.
//

#ifndef WAVEAUDIO_PIPEWIRE_H
#define WAVEAUDIO_PIPEWIRE_H

#include "soundio_internal.h"
#include "os.h"

struct SoundIoDeviceWasapi
{
    double period_duration;
};

struct SoundIoPipeWire
{
    std::unique_ptr<SoundIoOsMutex> mutex = nullptr;
    std::unique_ptr<SoundIoOsCond> cond = nullptr;
    std::unique_ptr<SoundIoOsThread> thread = nullptr;
    std::atomic_flag abort_flag = ATOMIC_FLAG_INIT;
    bool have_devices_flag;
    // this one is ready to be read with flush_events. protected by mutex
    std::unique_ptr<struct SoundIoDevicesInfo> ready_devices_info;
};

struct SoundIoOutStreamWasapi
{
    std::wstring stream_name;
    bool need_resample;
    std::unique_ptr<SoundIoOsThread> thread = nullptr;
    std::unique_ptr<SoundIoOsMutex> mutex;
    std::unique_ptr<SoundIoOsCond> cond;
    std::unique_ptr<SoundIoOsCond> start_cond;
    std::atomic_flag thread_exit_flag = ATOMIC_FLAG_INIT;
    bool is_raw;
    int writable_frame_count;
    int buffer_frame_count;
    int write_frame_count;
    std::atomic<bool> desired_pause_state{false};
    std::atomic_flag pause_resume_flag = ATOMIC_FLAG_INIT;
    std::atomic_flag clear_buffer_flag = ATOMIC_FLAG_INIT;
    bool is_paused;
    bool open_complete;
    int open_err;
    bool started;
    int min_padding_frames;
    float volume;
    SoundIoChannelArea areas[SOUNDIO_MAX_CHANNELS];
};

struct SoundIoInStreamPipeWire
{
    std::wstring stream_name;
    std::unique_ptr<SoundIoOsThread> thread = nullptr;
    std::unique_ptr<SoundIoOsMutex> mutex;
    std::unique_ptr<SoundIoOsCond> cond;
    std::unique_ptr<SoundIoOsCond> start_cond;
    std::atomic_flag thread_exit_flag = ATOMIC_FLAG_INIT;
    bool is_raw;
    int readable_frame_count;
    std::uint32_t buffer_frame_count;
    int read_frame_count;
    bool is_paused;
    bool open_complete;
    int open_err;
    bool started;
    char* read_buf;
    int read_buf_frames_left;
    int opened_buf_frames;
    SoundIoChannelArea areas[SOUNDIO_MAX_CHANNELS];
};

int soundio_pipewire_init(std::shared_ptr<SoundIoPrivate> si);

#endif // WAVEAUDIO_PIPEWIRE_H
