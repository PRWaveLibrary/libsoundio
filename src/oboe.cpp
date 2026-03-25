//
// Created by Administrator on 2026/3/9.
//

#include "oboe.h"

#include "soundio_private.h"
#include "util.h"

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


/**
 * @brief 刷新并构建当前 Oboe 支持的设备信息集合
 * 
 * 构建 output 和 input devices，写入到 ready_devices_info 之后通过 cond 触发更新通知。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @return int 返回 0 表示成功构建
 */
static int refresh_devices(std::shared_ptr<SoundIoPrivate>& si)
{
    SoundIoOboe& sio = si->backend_data->oboe;

    std::unique_ptr<SoundIoDevicesInfo> devices_info = std::make_unique<SoundIoDevicesInfo>();

    devices_info->default_output_index = 0;
    devices_info->default_input_index = 0;

    for (int aim_i = 0; aim_i < std::size(aims); aim_i += 1)
    {
        enum SoundIoDeviceAim aim = aims[aim_i];
        std::shared_ptr<SoundIoDevicePrivate> dev = std::make_shared<SoundIoDevicePrivate>();

        dev->soundio = si;
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

    std::unique_lock lock(sio.mutex->get());

    sio.ready_devices_info = std::move(devices_info);
    sio.have_devices_flag.store(true);
    sio.cond->signal(&lock);
    si->on_events_signal(si);
    return 0;
}

// static void shutdown_backend(std::shared_ptr<SoundIoPrivate> si, int err)
// {
//     SoundIoOboe& sio = si->backend_data->oboe;
//     std::unique_lock lock(sio.mutex->get());
//     sio.shutdown_err = err;
//     sio.cond->signal(&lock);
//     si->on_events_signal(si);
// }


/**
 * @brief 内部等待并拉取 Oboe event 分发机制的实现
 * 
 * 带有阻塞特性或者非阻塞特性的 event 拉取，由系统决定是否触发 on_devices_change。
 * 
 * @param si SoundIoPrivate 实例引用
 * @param wait 指定是否采用阻塞模式一直等待 device 有效或报错
 */
static void my_flush_events(std::shared_ptr<SoundIoPrivate>& si, bool wait)
{
    std::shared_ptr<SoundIo> soundio = si;
    SoundIoOboe& sio = si->backend_data->oboe;

    bool change = false;
    bool cb_shutdown = false;

    std::unique_lock lock(sio.mutex->get());

    // block until have devices
    while (wait || (!sio.have_devices_flag.load() && !sio.shutdown_err))
    {
        sio.cond->wait(&lock);
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
    lock.unlock();

    if (cb_shutdown)
    {
        soundio->on_backend_disconnect(soundio, sio.shutdown_err);
    }
    else if (change)
    {
        soundio->on_devices_change(soundio);
    }
}


/**
 * @brief 对外的 Oboe event flush 接口 (非阻塞)
 * 
 * @param si 当前 SoundIoPrivate 的指针
 */
static void flush_events_oboe(std::shared_ptr<SoundIoPrivate> si)
{
    my_flush_events(si, false);
}

/**
 * @brief 对外的 Oboe event 处理接口 (阻塞模式)
 * 
 * @param si 当前 SoundIoPrivate 的指针
 */
static void wait_events_oboe(std::shared_ptr<SoundIoPrivate> si)
{
    my_flush_events(si, true);
}

/**
 * @brief 处理 Oboe device scan 流程的 background thread 回调主程序
 * 
 * 监听 device_scan_queued 或 abort_flag 并做出相应的设备加载反应。
 * 
 * @param arg 传递给 thread 的携带类型为 SoundIoPrivate 的通用指针
 */
static void device_thread_run(std::shared_ptr<void> arg)
{
    std::shared_ptr<SoundIoPrivate> si = std::static_pointer_cast<SoundIoPrivate>(arg);
    SoundIoOboe& sio = si->backend_data->oboe;

    std::unique_lock lock(sio.scan_devices_mutex->get());

    while (!sio.abort_flag.test())
    {
        if (sio.device_scan_queued.load())
        {
            sio.device_scan_queued.store(false);
            lock.unlock();

            refresh_devices(si);
            flush_events_oboe(si);

            lock.lock();
            continue;
        }
        sio.scan_devices_cond->wait(&lock);
    }
}

/**
 * @brief 主动唤醒等待由于 Oboe event 阻塞的主流程
 * 
 * @param si 根系统私有存储指针
 */
static void wakeup_oboe(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoOboe& sio = si->backend_data->oboe;
    std::unique_lock lock(sio.mutex->get());
    sio.cond->signal(&lock);
}

/**
 * @brief 强行引发 Oboe 设备列出扫描
 * 
 * 向 background 触发 device_scan_queued 标识使得 thread 开始重构建。
 * 
 * @param si 系统存储引用指针
 */
static void force_device_scan_oboe(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoOboe& sio = si->backend_data->oboe;

    std::unique_lock lock(sio.scan_devices_mutex->get());

    sio.have_devices_flag.store(false);
    sio.device_scan_queued.store(true);
    sio.scan_devices_cond->signal(&lock);
}

/**
 * @brief 回收释放在 Oboe backend 下的 output stream 资源和 callbacks
 * 
 * @param si Backend root 节点状态
 * @param os 即将销毁的单独的 output stream 私有变量
 */
static void outstream_destroy_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    LOGI("destroy oboe outstream");
    SoundIoOutStreamOboe& oso = os->backend_data.oboe;
    oso.audio_stream = nullptr;
    oso.callback = nullptr;
    oso.error_callback = nullptr;
}

void OboeStreamDeleter::operator()(oboe::AudioStream* stream) const
{
    if (stream)
    {
        stream->close();
        delete stream;
    }
}

/**
 * @brief 打开并在 Oboe 底层进行配置初始化 Oboe outstream
 * 
 * 设定 callbacks、buffer 大小并由 Oboe::AudioStreamBuilder 最终创建 raw_stream。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param os SoundIoOutStreamPrivate 的 shared_ptr
 * @return int 成功返回 0
 */
static int outstream_open_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    LOGI("open oboe outstream");
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
    oso.error_callback = std::make_unique<oboe_stream_error_callback>(si);

    oboe::AudioStreamBuilder builder;
    builder.setSharingMode(oboe::SharingMode::Exclusive)
            ->setUsage(oboe::Usage::Game)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setFormat(oboe::AudioFormat::Float)
            ->setChannelCount(outstream->layout.channel_count)
            ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
            ->setDataCallback(oso.callback.get())
            ->setErrorCallback(oso.error_callback.get());

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
    os->paused = false;
    return 0;
}

/**
 * @brief 在 Oboe stream 上执行 pause 或者 resume 恢复的操作控制
 * 
 * 通过底层的 requestPause, requestFlush 和 requestStart 来安全切换播放 state。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param os 当前操作的 output stream 私有数据指针
 * @param pause 传入 true 则表示暂停, false 意味着开始或恢复
 * @return int 成功返回 0，底层调用失败则报错
 */
static int outstream_pause_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, bool pause)
{
    SoundIoOutStreamOboe& osca = os->backend_data.oboe;
    std::unique_ptr<oboe::AudioStream, OboeStreamDeleter>& stream = osca.audio_stream;
    os->paused = false;
    oboe::Result result;
    if (pause)
    {
        result = stream->requestStop();
        if (result != oboe::Result::OK)
        {
            return SoundIoErrorStreaming;
        }
    }
    else
    {
        int64_t timeoutNanos = 1000 * oboe::kNanosPerMillisecond;
        auto state = stream->getState();
        bool isExit = false;
        while (!isExit)
        {
            switch (state)
            {
                case oboe::StreamState::Stopping:
                {
                    // 超时直接重启输出流
                    result = stream->waitForStateChange(oboe::StreamState::Stopping, &state, timeoutNanos);
                    if (result != oboe::Result::OK)
                    {
                        force_device_scan_oboe(si);
                        return SoundIoErrorNone;
                    }
                    break;
                }
                case oboe::StreamState::Paused:
                case oboe::StreamState::Flushed:
                case oboe::StreamState::Open:
                case oboe::StreamState::Stopped:
                {
                    isExit = true;
                    break;
                }
                default:
                {
                    LOGE("unknown state: {}", static_cast<int>(state));
                    return SoundIoErrorStreaming;
                }
            }
        }

        // 先播放音乐->插入耳机->切换到其他音乐软件->拔掉耳机->播放音乐->切回就能触发ErrorIllegalArgument
        result = stream->requestStart();
        if (result != oboe::Result::OK)
        {
            force_device_scan_oboe(si);
        }
    }

    return SoundIoErrorNone;
}

/**
 * @brief 开始或触发启动 Oboe outstream 的播放处理逻辑
 * 
 * 封装了对 outstream_pause_oboe 并传递 pause=false 的调用。
 * 
 * @param si 根节点状态
 * @param os 操作的目标 stream 对象
 * @return int 成功返回 0
 */
static int outstream_start_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    return outstream_pause_oboe(si, os, false);
}


/**
 * @brief 进入写入音频 buffer 操作上下文
 * 
 * 规划将要发送的 channel 内存区域，以及声明具体的音频写入步长参数。
 * 
 * @param si SoundIoPrivate 实例指针
 * @param os 输出流上下文指针
 * @param out_areas 存放分配或计算好待填充数据的 target 缓存区通道 layout
 * @param frame_count 希望写入的具体 frame 量
 * @return int 返回 0 提供正常响应
 */
static int outstream_begin_write_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, struct SoundIoChannelArea** out_areas, int* frame_count)
{
    std::shared_ptr<SoundIoOutStream> outstream = os;
    SoundIoOutStreamOboe& oso = os->backend_data.oboe;

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

/**
 * @brief 结束当次 buffer 的装载行为并确认数据
 * 
 * 推进 Oboe stream 内部的 buffer index。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param os 正在操作的 Stream 指针
 * @return int 结束响应总是返回 0
 */
static int outstream_end_write_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    os->backend_data.oboe.buffer_index += 1;
    return 0;
}

/**
 * @brief 清理 Oboe 的 internal buffer 数据（不支持）
 * 
 * 由于 Oboe API 基于快速返回无此提供，会返回 Backend Unavailable。
 * 
 * @param si 系统根节点指针
 * @param os 指定清空的 output stream 指针
 * @return int 返回不兼容错误代码
 */
static int outstream_clear_buffer_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    return SoundIoErrorIncompatibleBackend;
}

/**
 * @brief 获取并在 Oboe backend 计算其实时的执行 offset 时间与 latency
 * 
 * 通过 calculateLatencyMillis 返回精确的 delay。如果遇到 error，回落到初始化指定的硬件延迟时间。
 * 
 * @param si SoundIoPrivate 的指针
 * @param os 被询问延迟所在的 output stream
 * @param out_latency 返回出来的总滞后秒数
 * @return int 始终执行成功并返回 0
 */
static int outstream_get_latency_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, double* out_latency)
{
    SoundIoOutStreamOboe& oso = os->backend_data.oboe;

    if (oso.audio_stream)
    {
        std::unique_ptr<oboe::AudioStream, OboeStreamDeleter>& stream = oso.audio_stream;
        // 通过 Oboe 进行实时运算，能测算出真实物理和缓冲折叠造成的完整精确延迟。
        auto result = stream->calculateLatencyMillis();
        if (result.error() == oboe::Result::OK)
        {
            *out_latency = result.value() / 1000.0;
            return 0;
        }
    }

    // 后备方案：如果在未运行等状态下出现错误，则回落到初始化估算结果。
    *out_latency = oso.hardware_latency;
    return 0;
}

/**
 * @brief 应用指定音量缩放倍频到当前绑定的 output stream 层面
 * 
 * 单纯在 Stream object 侧配置该参数以备使用。
 * 
 * @param si 系统调用节点
 * @param os 操作的目标 stream 对象
 * @param volume 需要分配的增益尺度 (float)
 * @return int 返回 0
 */
static int outstream_set_volume_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, float volume)
{
    os->volume = volume;
    return 0;
}


/**
 * @brief 初始化分配 Oboe 捕获输入流 (Placeholder)
 * 
 * 目前抛出未支持异常 (BackendUnavailable)。
 * 
 * @param si Backend root 节点状态
 * @param is 被实例化的 input stream 句柄
 * @return int 返回错误代码
 */
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


/**
 * @brief 清理并在 system level 解除指定 Oboe instream 的结构体注册资源
 * 
 * @param si 当前的系统状态节点指针
 * @param is 需要被解绑析构处理的对应 input stream 对象
 */
static void instream_destroy_oboe(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
}


/**
 * @brief 回收所有与 Oboe 有关的 system resources 与 scan threads 的顶级函数
 * 
 * 设置退出标识强制释放 mutex 保证安全退出。
 * 
 * @param si 全局顶层的 SoundIoPrivate 指针引用
 */
static void destroy_oboe(std::shared_ptr<SoundIoPrivate> si)
{
    LOGI("destroy oboe");
    SoundIoOboe& sio = si->backend_data->oboe;

    if (sio.thread)
    {
        std::unique_lock lock(sio.scan_devices_mutex->get());
        sio.abort_flag.test_and_set();
        sio.scan_devices_cond->signal(&lock);
        lock.unlock();

        sio.thread = nullptr;
    }

    sio.cond = nullptr;
    sio.mutex = nullptr;
    sio.scan_devices_cond = nullptr;
    sio.scan_devices_mutex = nullptr;

    outstream_destroy_oboe(si, std::dynamic_pointer_cast<SoundIoOutStreamPrivate>(si->outstream));
}

/**
 * @brief 为框架挂载及配置对应的 Oboe engine 设施环境
 * 
 * 挂载各类型回调 (OutStream / InStream 以及 event hooks)。并设置 system locks 启动 scan thread。
 * 
 * @param si 全局的 SoundIoPrivate 类型封装
 * @return int 操作状态或者缺乏对应内存的无关联错误代码
 */
int soundio_oboe_init(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoOboe& sio = si->backend_data->oboe;

    sio.have_devices_flag.store(false);
    sio.device_scan_queued.store(true);
    sio.abort_flag.clear();

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

    sio.thread = SoundIoOsThread::create(device_thread_run, si);

    si->destroy = destroy_oboe;
    si->flush_events = flush_events_oboe;
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
