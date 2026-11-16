//
// Created by mda on 2026/11/16.
//

#ifndef WAVEAUDIO_PIPEWIRE_H
#define WAVEAUDIO_PIPEWIRE_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "os.h"
#include "soundio_internal.h"

struct SoundIoPrivate;
struct SoundIoDevicesInfo;

// PipeWire 原生资源定义在 pipewire.cpp 里，这里只保存不透明指针，
// 避免把 <pipewire/pipewire.h> 带进所有翻译单元（也避免和本文件名冲突）。
struct PipeWireBackend;
struct PipeWireStream;

struct SoundIoDevicePipeWire
{
    //! PipeWire 全局 node id，daemon 重启后会变，每次枚举刷新
    std::uint32_t node_id = 0;
    //! node.name，用于校验 / 重新查找 node
    std::wstring node_name;
    //! 一个 graph 周期的时长（秒），来自 node.latency 或默认 quantum
    double period_duration = 0.0;
};

struct SoundIoPipeWire
{
    std::unique_ptr<SoundIoOsMutex> mutex = nullptr;
    std::unique_ptr<SoundIoOsCond> cond = nullptr;
    std::unique_ptr<SoundIoOsCond> scan_devices_cond;
    std::unique_ptr<SoundIoOsMutex> scan_devices_mutex;
    std::unique_ptr<SoundIoOsThread> thread = nullptr;
    bool have_devices_flag = false;
    // this one is ready to be read with flush_events. protected by mutex
    std::unique_ptr<SoundIoDevicesInfo> ready_devices_info;
    std::atomic_bool device_scan_queued = {false};
    int shutdown_err = 0;
    bool emitted_shutdown_cb = false;


    //! 本实例是否已经 pw_init 过（对应 pw_deinit 的引用计数）
    bool pw_inited = false;

    //! registry / metadata 连接，由 device thread 独占创建和销毁
    std::shared_ptr<PipeWireBackend> backend = nullptr;
};

struct SoundIoOutStreamPipeWire
{
    std::wstring stream_name;
    std::unique_ptr<SoundIoOsThread> thread = nullptr;
    std::unique_ptr<SoundIoOsMutex> mutex;
    std::unique_ptr<SoundIoOsCond> cond;
    std::unique_ptr<SoundIoOsCond> start_cond;
    //! 本轮 process 回调期望写入的帧数（ring buffer 空间保证 >= 该值）
    int writable_frame_count = 0;
    //! 本轮 begin_write 实际分配出去的帧数
    int write_frame_count = 0;
    bool is_paused = false;
    bool open_complete = false;
    int open_err = 0;
    bool started = false;
    SoundIoChannelArea areas[SOUNDIO_MAX_CHANNELS] = {};

    //! pw_thread_loop / pw_core / pw_stream / ring buffer
    PipeWireStream* stream = nullptr;
};

struct SoundIoInStreamPipeWire
{
    std::wstring stream_name;
    std::unique_ptr<SoundIoOsThread> thread = nullptr;
    std::unique_ptr<SoundIoOsMutex> mutex;
    std::unique_ptr<SoundIoOsCond> cond;
    std::unique_ptr<SoundIoOsCond> start_cond;
    //! 本轮 process 回调从 graph 拿到的帧数
    int readable_frame_count = 0;
    //! 本轮 begin_read 实际交出去的帧数
    int read_frame_count = 0;
    bool is_paused = false;
    bool open_complete = false;
    int open_err = 0;
    bool started = false;
    SoundIoChannelArea areas[SOUNDIO_MAX_CHANNELS] = {};

    //! pw_thread_loop / pw_core / pw_stream / ring buffer
    PipeWireStream* stream = nullptr;
};

int soundio_pipewire_init(const std::shared_ptr<SoundIoPrivate>& si);

#endif // WAVEAUDIO_PIPEWIRE_H
