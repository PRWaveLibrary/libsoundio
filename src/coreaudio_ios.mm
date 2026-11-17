/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "coreaudio_ios.h"
#include "soundio_private.h"
#include "util.h"
#include "util/logger.hpp"
#include "util/malloc_util.h"

#include <cassert>
#include <ranges>
#include <AVFoundation/AVFoundation.h>
#include <MediaPlayer/MediaPlayer.h>
#include "oc_method_swizzling.h"

static const int OUTPUT_ELEMENT = 0;
static const int INPUT_ELEMENT = 1;

static enum SoundIoDeviceAim aims[] = {
    SoundIoDeviceAimInput,
    SoundIoDeviceAimOutput,
};


void CoreAudioInstanceDeleter::operator()(OpaqueAudioComponentInstance* stream) const
{
    if(stream)
    {
        AudioOutputUnitStop(stream);
        AudioComponentInstanceDispose(stream);
    }
}


/**
 * @brief 通过 AVAudioSession 解析 iOS 的 device 并构建 SoundIo 属性信息
 * 
 * 从 iOS system audio 获取 hardware 参数并组装为 input/output 的 devices_info。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @return int 返回 0 提供正常响应构建
 */
static int refresh_devices(const std::shared_ptr<SoundIoPrivate>& si)
{
    auto& sica = si->backend_data->coreaudio_ios;

    auto devices_info = std::make_unique<SoundIoDevicesInfo>();

    devices_info->default_output_index = 0;
    devices_info->default_input_index = 0;
    
    AVAudioSession* session = AVAudioSession.sharedInstance;
    
    for (size_t aim_i = 0; aim_i < std::size(aims); aim_i += 1)
    {
        enum SoundIoDeviceAim aim = aims[aim_i];
        const auto dev = std::make_shared<SoundIoDevicePrivate>();
        
        dev->soundio = si;
        dev->is_raw = false;
        dev->aim = aim;
        dev->formats.push_back(SoundIoFormatFloat32LE);
        dev->is_default = true;

        if (aim == SoundIoDeviceAimOutput)
        {
            dev->current_layout = *soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdStereo);
        }
        else
        {
            dev->current_layout = *soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdMono);
        }

        dev->layouts.push_back(dev->current_layout);
        dev->current_layout.channel_count = static_cast<int>(aim == SoundIoDeviceAimOutput ? session.outputNumberOfChannels : session.inputNumberOfChannels);

        dev->sample_rate_current = static_cast<int>(session.sampleRate);
        dev->sample_rates.push_back(SoundIoSampleRateRange{dev->sample_rate_current, dev->sample_rate_current});
        
        dev->software_latency_current = aim == SoundIoDeviceAimOutput ? session.outputLatency : session.inputLatency;
        dev->software_latency_min = 0.0001;
        dev->software_latency_max = 0.02;
        
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

    std::unique_lock lock(sica.mutex->get());
    
    sica.ready_devices_info = std::move(devices_info);
    sica.have_devices_flag.store(true);
    sica.cond->signal(&lock);
    si->on_events_signal(si);

    return 0;
}

/**
 * @brief 关闭并在 backend 抛出异常报错
 * 
 * 由于严重 error 例如 AVAudioSession 重启而导致。发出唤醒 signal 让 main program 得知。
 * 
 * @param si 根级别 SoundIoPrivate context
 * @param err 返回终止的错误代码
 */
static void shutdown_backend(const std::shared_ptr<SoundIoPrivate>& si, int err)
{
    auto& sica = si->backend_data->coreaudio_ios;
    std::unique_lock lock(sica.mutex->get());

    sica.shutdown_err = err;
    sica.cond->signal(&lock);
    si->on_events_signal(si);
}

/**
 * @brief 阻塞或触发拉取最新的 iOS device 更新 events 的逻辑器
 * 
 * @param si 当前 SoundIoPrivate 实例引用
 * @param wait 若为 true 意味着执行一直等待的操作
 */
static void my_flush_events(const std::shared_ptr<SoundIoPrivate>& si, bool wait)
{
    auto& sica = si->backend_data->coreaudio_ios;

    bool change = false;
    bool cb_shutdown = false;

    std::unique_lock lock(sica.mutex->get());

    // block until have devices
    while (wait || (!sica.have_devices_flag.load() && !sica.shutdown_err))
    {
        sica.cond->wait(&lock);
        wait = false;
    }

    if (sica.shutdown_err && !sica.emitted_shutdown_cb)
    {
        sica.emitted_shutdown_cb = true;
        cb_shutdown = true;
    }
    else if (sica.ready_devices_info)
    {
        si->safe_devices_info = std::move(sica.ready_devices_info);
        change = true;
    }

    lock.unlock();

    if (cb_shutdown)
    {
        si->on_backend_disconnect(si, sica.shutdown_err);
    }
    else if (change)
    {
        si->on_devices_change(si);
    }
}

/**
 * @brief iOS backend 的非阻塞 event 暴露点
 * 
 * 调用内部带 false 标志的 my_flush_events。
 */
static void flush_events_ca(const std::shared_ptr<SoundIoPrivate>& si)
{
    my_flush_events(si, false);
}

/**
 * @brief 含有阻塞等待特征的 iOS backend event flush hooks。
 */
static void wait_events_ca(const std::shared_ptr<SoundIoPrivate>& si)
{
    my_flush_events(si, true);
}

/**
 * @brief 在外部进行调用，唤醒正被 CoreAudio 逻辑阻塞中的 event loop
 */
static void wakeup_ca(const std::shared_ptr<SoundIoPrivate>& si)
{
    auto& sica = si->backend_data->coreaudio_ios;
    std::unique_lock lock(sica.mutex->get());
    sica.cond->signal(&lock);
}

/**
 * @brief 强行请求 background thread 执行 devices 扫描流程
 */
static void force_device_scan_ca(const std::shared_ptr<SoundIoPrivate>& si)
{
    auto& sica = si->backend_data->coreaudio_ios;
    sica.device_scan_queued.store(true);
    sica.have_devices_flag.store(false);

    std::unique_lock scan_lock(sica.scan_devices_mutex->get());
    sica.scan_devices_cond->signal(&scan_lock);
}
 
/**
 * @brief background 异步扫描与设备检测线程主入口
 * 
 * @param arg 用户传递的 SoundIoPrivate pointer 参数
 */
static void device_thread_run(const std::stop_token& token, const std::shared_ptr<void>& arg)
{
    const auto si = std::static_pointer_cast<SoundIoPrivate>(arg);
    auto& sica = si->backend_data->coreaudio_ios;
    int err = SoundIoErrorNone;
    while (!token.stop_requested())
    {
        {
            std::unique_lock lock(sica.scan_devices_mutex->get());
            sica.scan_devices_cond->wait(&lock,[&]
            {
                return sica.service_restarted.load(std::memory_order_acquire) || sica.device_scan_queued.load(std::memory_order_acquire);
            });

            if (token.stop_requested())
            {
                break;
            }

            if (sica.service_restarted.load(std::memory_order_acquire))
            {
                err = SoundIoErrorBackendDisconnected;
                break;
            }
            sica.device_scan_queued.store(false, std::memory_order_release);
        }

        err = refresh_devices(si);
        if (err != SoundIoErrorNone)
        {
            break;
        }
        flush_events_ca(si);
    }
    if (err != SoundIoErrorNone)
    {
        shutdown_backend(si, err);
    }

}

/**
 * @brief 卸载 OutStream 关联分配的 iOS CoreAudio 运行实例资源
 * 
 * @param si 当前的 SoundIoPrivate instance
 * @param os 需要解绑设备的 output stream
 */
static void outstream_destroy_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os)
{
    LOGI(L"destroy core audio outstream");
    os->backend_data.coreaudio_ios.instance = nullptr;
}

/**
 * @brief iOS RemoteIO 的渲染回调：userdata 是 SoundIoOutStreamPrivate 裸指针，
 *
 * 拿回 shared_ptr 后把 AudioBufferList 桥接给 libsoundio 的 write_callback。
 */
OSStatus CoreAudioCallback::write_callback(void* userdata, AudioUnitRenderActionFlags* io_action_flags,
                                           const AudioTimeStamp* in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
                                           AudioBufferList* io_data)
{
    auto* os = static_cast<SoundIoOutStreamPrivate*>(userdata);
    if (os == nullptr)
    {
        for (UInt32 i = 0; i < io_data->mNumberBuffers; ++i)
        {
            memset(io_data->mBuffers[i].mData, 0, io_data->mBuffers[i].mDataByteSize);
        }
        LOGE(L"os is nullptr.");
        return noErr;
    }

    // 裸指针 → shared_ptr 只能经由 enable_shared_from_this 的控制块
    const auto os_sp = os->weak_from_this().lock();
    if (!os_sp)
    {
        return noErr;
    }

    auto& osca = os->backend_data.coreaudio_ios;

    osca.io_data = io_data;
    osca.buffer_index = 0;
    osca.frames_left = static_cast<int>(in_number_frames);
    os_sp->write_callback(os_sp, osca.frames_left, osca.frames_left);
    osca.io_data = nullptr;

    return noErr;
}

/**
 * @brief 对照给定 SoundIoFormat 定义填充特定的 CoreAudio ASBD 原生类型结构
 * 
 * @param fmt 标准的被支持的 SoundIoFormat 
 * @param desc 将要被填入 flag 和 precision 属性的被指针对象
 * @return int 发现不兼容则返回对应 Code
 */
static int set_ca_desc(enum SoundIoFormat fmt, AudioStreamBasicDescription *desc) {
    switch (fmt) {
    case SoundIoFormatFloat32LE:
        desc->mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        desc->mBitsPerChannel = 32;
        break;
    case SoundIoFormatFloat64LE:
        desc->mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        desc->mBitsPerChannel = 64;
        break;
    case SoundIoFormatS32LE:
        desc->mFormatFlags = kAudioFormatFlagIsSignedInteger;
        desc->mBitsPerChannel = 32;
        break;
    case SoundIoFormatS16LE:
        desc->mFormatFlags = kAudioFormatFlagIsSignedInteger;
        desc->mBitsPerChannel = 16;
        break;
	case SoundIoFormatS24LE:
		desc->mFormatFlags = kAudioFormatFlagIsSignedInteger;
		desc->mBitsPerChannel = 24;
		break;
    default:
        return SoundIoErrorIncompatibleDevice;
    }
    return 0;
}

/**
 * @brief 初始化分配 iOS 的 AudioComponentInstance 以使用 OutStream
 * 
 * 通过 RemoteIO 将应用配置关联到底层硬件特性。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param os 输出流内部数据对象 SoundIoOutStreamPrivate
 * @return int 返回错误码或连接成功 0
 */
static int outstream_open_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os)
{
    LOGI(L"open outstream.");
    auto& osca = os->backend_data.coreaudio_ios;
    const auto dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(os->device);
    const auto dca = dev->backend_data.coreaudio_ios;

    if (os->software_latency == 0.0)
    {
        // 外部未指定则使用五毫秒,48000约256byte
        os->software_latency = 0.005;
    }

    os->software_latency = std::clamp(os->software_latency, dev->software_latency_min, dev->software_latency_max);
    
    NSError* ns_err;
    AVAudioSession* session = AVAudioSession.sharedInstance;
    BOOL isOk = [session setPreferredIOBufferDuration:os->software_latency error:&ns_err];
    if(!isOk){
        LOGE(L"Audio Session Error: %hs", ns_err.localizedDescription.UTF8String);
        outstream_destroy_ca(si,os);
        return SoundIoErrorOpeningDevice;
    }
    
    
    isOk = [session setActive:YES error:&ns_err];
    if (!isOk) {
        LOGE(L"Audio Session Error: %hs", ns_err.localizedDescription.UTF8String);
        outstream_destroy_ca(si,os);
        return SoundIoErrorOpeningDevice;
    }
    
    
    auto desc = AudioComponentDescription{};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    // 查找组件
    const auto component = AudioComponentFindNext(nullptr, &desc);
    if (!component) {
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    AudioComponentInstance instance;
    
    // 实例音频单元
    auto os_err = AudioComponentInstanceNew(component, &instance);
    if (os_err != noErr)
    {
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    osca.instance = std::unique_ptr<OpaqueAudioComponentInstance, CoreAudioInstanceDeleter>(instance, CoreAudioInstanceDeleter());
    instance = nullptr;
    
    os_err = AudioUnitInitialize(osca.instance.get());
    if (os_err != noErr)
    {
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    auto format = AudioStreamBasicDescription{};
    const auto& infos = si->safe_devices_info;
    const auto default_out_index = infos->default_output_index;
    if (infos->output_devices.size() <= default_out_index)
    {
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }
    
    const auto default_output = infos->output_devices.at(infos->default_output_index);
    format.mSampleRate = default_output->sample_rate_current;
    format.mFormatID = kAudioFormatLinearPCM;
    const int err = set_ca_desc(os->format, &format);
    if (err)
    {
        outstream_destroy_ca(si, os);
        return err;
    }
    format.mBytesPerPacket = os->bytes_per_frame;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = os->bytes_per_frame;
    format.mChannelsPerFrame = os->layout.channel_count;
    os_err = AudioUnitSetProperty(osca.instance.get(), kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, OUTPUT_ELEMENT, &format,sizeof(AudioStreamBasicDescription));
    if (os_err != noErr)
    {
        outstream_destroy_ca(si, os);
        return SoundIoErrorIncompatibleDevice;
    }

    const AURenderCallbackStruct render_callback = {CoreAudioCallback::write_callback, os.get()};
    os_err = AudioUnitSetProperty(osca.instance.get(), kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, OUTPUT_ELEMENT, &render_callback, sizeof(AURenderCallbackStruct));
    if (os_err != noErr)
    {
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    os->volume.store(1.0f, std::memory_order_release);
    osca.hardware_latency = dca->latency_frames / static_cast<double>(os->sample_rate);
    os->sample_rate = format.mSampleRate;
    os->paused = false;
    
    return 0;
}

static void ForceAudioToSpeaker(){
    AVAudioSession *session = [AVAudioSession sharedInstance];
    // 1. 检查当前有没有连接耳机或蓝牙
    BOOL isHeadsetConnected = NO;
    AVAudioSessionRouteDescription *route = [session currentRoute];
    for (AVAudioSessionPortDescription *desc in route.outputs) {
        if ([desc.portType isEqualToString:AVAudioSessionPortHeadphones] ||
            [desc.portType isEqualToString:AVAudioSessionPortBluetoothA2DP] ||
            [desc.portType isEqualToString:AVAudioSessionPortBluetoothHFP] ||
            [desc.portType isEqualToString:AVAudioSessionPortBluetoothLE]) {
            isHeadsetConnected = YES;
            break;
        }
    }
    if (isHeadsetConnected) {
        [session overrideOutputAudioPort:AVAudioSessionPortOverrideNone error:nil];
        return;
    }
    [session overrideOutputAudioPort:AVAudioSessionPortOverrideSpeaker error:nil];
}

/**
 * @brief 执行对于实例层面的 AudioOutputUnitStop 或 AudioOutputUnitStart 播放暂停操作
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param os 当前操作的 output stream
 * @param pause 指出状态是 pause 或 resume
 * @return int 操作返回
 */
static int outstream_pause_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os, bool pause) {
    const auto& osca = os->backend_data.coreaudio_ios;
    OSStatus os_err;
    os->paused = pause;
    if (pause)
    {
        os_err = AudioOutputUnitStop(osca.instance.get());
        if (os_err != noErr)
        {
            return SoundIoErrorStreaming;
        }
    }
    else
    {
        UInt32 isRunning = 0;
        UInt32 size = sizeof(isRunning);
        os_err = AudioOutputUnitStart(osca.instance.get());
        if (os_err != noErr)
        {
            force_device_scan_ca(si);
            return SoundIoErrorNone;
        }
        ForceAudioToSpeaker();
        OSStatus status = AudioUnitGetProperty(osca.instance.get(), kAudioOutputUnitProperty_IsRunning, kAudioUnitScope_Global, OUTPUT_ELEMENT, &isRunning, &size);
        if(status != noErr)
        {
            force_device_scan_ca(si);
            return SoundIoErrorNone;
        }
        
        if(!isRunning)
        {
            force_device_scan_ca(si);
            return SoundIoErrorNone;
        }
    }

    return SoundIoErrorNone;
}

/**
 * @brief OutStream 设置后首次调用使得该流挂载进 callbacks 中
 */
static int outstream_start_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os) {
    return outstream_pause_ca(si, os, false);
}


/**
 * @brief 利用底层 callback 的 io_data 提供给用户层可填写的区域
 * 
 * @param si 系统调用节点
 * @param os 将用于导出的 stream
 * @param out_areas 导出 buffer 的位置和结构
 * @param frame_count 期望处理 frame 数
 */
static int outstream_begin_write_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os, SoundIoChannelArea** out_areas,
                                    int* out_frame_count)
{
    auto& osca = os->backend_data.coreaudio_ios;

    if (osca.buffer_index >= osca.io_data->mNumberBuffers)
    {
        return SoundIoErrorInvalid;
    }

    if (*out_frame_count != osca.frames_left)
    {
        return SoundIoErrorInvalid;
    }

    auto* audio_buffer = &osca.io_data->mBuffers[osca.buffer_index];
    assert(audio_buffer->mNumberChannels == os->layout.channel_count);
    osca.write_frame_count = audio_buffer->mDataByteSize / os->bytes_per_frame;
    *out_frame_count = osca.write_frame_count;
    assert((audio_buffer->mDataByteSize % os->bytes_per_frame) == 0);
    for (int ch = 0; ch < os->layout.channel_count; ch += 1)
    {
        osca.areas[ch].ptr = ((char*)audio_buffer->mData) + os->bytes_per_sample * ch;
        osca.areas[ch].step = os->bytes_per_frame;
    }
    *out_areas = osca.areas;
    return 0;
}

/**
 * @brief 结束填写使得系统获取可读 buffer
 */
static int outstream_end_write_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os)
{
    auto& osca = os->backend_data.coreaudio_ios;
    osca.buffer_index += 1;
    osca.frames_left -= osca.write_frame_count;
    assert(osca.frames_left >= 0);
    return 0;
}

/**
 * @brief 清空 stream 的缓存
 */
static int outstream_clear_buffer_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os)
{
    return SoundIoErrorIncompatibleBackend;
}

/**
 * @brief 获取 Stream iOS 内核的运行 hardware latency
 */
static int outstream_get_latency_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os, double* out_latency)
{
    const auto& osca = os->backend_data.coreaudio_ios;
    *out_latency = osca.hardware_latency;
    return 0;
}

/**
 * @brief 配置独立 stream 当前可设的 volume
 */
static int outstream_set_volume_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os, float volume)
{
    os->volume = volume;
    return 0;
}

/**
 * @brief 卸载 iOS InStream 申请的 RemoteIO 麦克风录音实例
 */
static void instream_destroy_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is)
{
    // 先 stop AudioUnit（同步等待正在运行的回调结束），再清 instance
    auto& isca = is->backend_data.coreaudio_ios;
    isca.instance = nullptr;
    isca.buffer_list = nullptr;
}

/**
 * @brief 异步激活 AVAudioSession
 *
 * 通知回调是挂在 [NSOperationQueue mainQueue] 上的，所以 InterruptionEnded 里
 * 如果在主线程直接调同步版 -setActive:error:，iOS 27 会打这条运行时提示：
 *   "This method can lead to UI unresponsiveness if called on the main thread.
 *    Consider using the asynchronous activate/deactivate API instead..."
 * 蓝牙设备（AirPods 的 A2DP<->HFP 切换等）激活尤其慢，主线程会被卡住，
 * 所以这里统一走 iOS 27 起提供的异步 API。
 *
 * iOS 27 以下没有这个 API（也不存在那条提示），退回到同步调用。
 *
 * @param completion 激活结束后回调，activated 表示是否成功
 */
static void activate_session_async(void (^completion)(BOOL activated, NSError* _Nullable error))
{
    AVAudioSession* session = AVAudioSession.sharedInstance;

#if defined(__IPHONE_OS_VERSION_MAX_ALLOWED) && __IPHONE_OS_VERSION_MAX_ALLOWED >= 270000
    if (@available(iOS 27.0, *))
    {
        [session activateWithOptions:AVAudioSessionActivationOptionNone completionHandler:completion];
        return;
    }
#endif

    NSError* error = nil;
    const BOOL ok = [session setActive:YES error:&error];
    completion(ok, error);
}

void CoreAudioCallback::on_notification(NSNotification* note, const std::shared_ptr<SoundIoPrivate>& s)
{
    if (s == nullptr)
    {
        return;
    }
    NSDictionary* userInfo = note.userInfo;
    AVAudioSessionInterruptionType type = static_cast<AVAudioSessionInterruptionType>([userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue]);
    switch(type){
        case AVAudioSessionInterruptionTypeBegan:
        {
            LOGI(L"apple notification pause.");
            if (s->default_out_stream)
            {
                s->outstream_pause(s, std::dynamic_pointer_cast<SoundIoOutStreamPrivate>(s->default_out_stream), true);
            }
        }
            break;
        case AVAudioSessionInterruptionTypeEnded:
        {
            LOGI(L"apple notification resume begin.");
            AVAudioSessionInterruptionOptions options = [userInfo[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue];
            if (!(options & AVAudioSessionInterruptionOptionShouldResume))
            {
                LOGI(L"apple notification disable resume.");
                return;
            }

            // 先复制到局部变量再捕获：直接捕获 `const std::shared_ptr&` 参数时，
            // block 不会 copy-construct 捕获对象，拿到的是悬空副本。
            const std::shared_ptr<SoundIoPrivate> keep = s;
            activate_session_async(^(BOOL activated, NSError* error)
            {
                if (!activated)
                {
                    LOGE(L"restart session failed.%hs",
                         error != nil ? error.localizedDescription.UTF8String : "unknown");
                    return;
                }
                // 激活是异步的，AudioUnit 必须等这里再启动
                if (keep->outstream_pause && keep->default_out_stream)
                {
                    keep->outstream_pause(keep, std::dynamic_pointer_cast<SoundIoOutStreamPrivate>(keep->default_out_stream), false);
                }
                LOGI(L"apple notification resume finished.");
            });
        }
        break;
            
    }
}

void CoreAudioCallback::on_route_notification(NSNotification* note, const std::shared_ptr<SoundIoPrivate>& s)
{
    if (s == nullptr)
    {
        return;
    }
    NSDictionary* userInfo = note.userInfo;
    AVAudioSessionRouteChangeReason type = static_cast<AVAudioSessionRouteChangeReason>([userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue]);
    switch(type){
        case AVAudioSessionRouteChangeReasonOldDeviceUnavailable:
        {
            LOGI(L"AVAudioSessionRouteChangeReasonOldDeviceUnavailable");
            if (s->default_out_stream)
            {
                soundio_outstream_pause(s->default_out_stream, false);
            }
            break;
        }
        case AVAudioSessionRouteChangeReasonNewDeviceAvailable: {
            LOGI(L"AVAudioSessionRouteChangeReasonNewDeviceAvailable");
            break;
        }
        case AVAudioSessionRouteChangeReasonCategoryChange: {
            LOGI(L"AVAudioSessionRouteChangeReasonCategoryChange");
            break;
        }
        case AVAudioSessionRouteChangeReasonOverride: {
            LOGI(L"AVAudioSessionRouteChangeReasonOverride");
            break;
        }
        case AVAudioSessionRouteChangeReasonWakeFromSleep: {
            LOGI(L"AVAudioSessionRouteChangeReasonWakeFromSleep");
            break;
        }
        case AVAudioSessionRouteChangeReasonNoSuitableRouteForCategory: {
            LOGI(L"AVAudioSessionRouteChangeReasonNoSuitableRouteForCategory");
            break;
        }
        case AVAudioSessionRouteChangeReasonRouteConfigurationChange: {
            LOGI(L"AVAudioSessionRouteChangeReasonRouteConfigurationChange");
            break;
        }
        case AVAudioSessionRouteChangeReasonUnknown: {
            LOGI(L"AVAudioSessionRouteChangeReasonUnknown");
            break;
        }
    }
}


/**
 * @brief iOS RemoteIO 的采集回调：userdata 是 SoundIoInStreamPrivate 裸指针，
 *
 * 拿回 shared_ptr 后用 AudioUnitRender 取出数据并交给 libsoundio 的 read_callback。
 */
OSStatus CoreAudioCallback::read_callback(void* userdata, AudioUnitRenderActionFlags* io_action_flags,
                                          const AudioTimeStamp* in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
                                          AudioBufferList* io_data)
{
    auto* is = static_cast<SoundIoInStreamPrivate*>(userdata);
    if (is == nullptr)
    {
        LOGE(L"is is nullptr.");
        return noErr;
    }

    // 裸指针 → shared_ptr 只能经由 enable_shared_from_this 的控制块
    const auto is_sp = is->weak_from_this().lock();
    if (!is_sp)
    {
        return noErr;
    }

    auto& isca = is->backend_data.coreaudio_ios;

    for (int i = 0; i < isca.buffer_list->mNumberBuffers; i++)
    {
        isca.buffer_list->mBuffers[i].mData = NULL;
    }

    const OSStatus os_err = AudioUnitRender(isca.instance.get(), io_action_flags, in_time_stamp, in_bus_number, in_number_frames, isca.buffer_list.get());
    if (os_err != noErr)
    {
        is_sp->error_callback(is_sp, SoundIoErrorStreaming);
        return noErr;
    }

    if (isca.buffer_list->mNumberBuffers == 1)
    {
        auto* audio_buffer = &isca.buffer_list->mBuffers[0];
        assert(audio_buffer->mNumberChannels == is->layout.channel_count);
        assert(audio_buffer->mDataByteSize == in_number_frames * is->bytes_per_frame);
        for (int ch = 0; ch < is->layout.channel_count; ch += 1)
        {
            isca.areas[ch].ptr = static_cast<char*>(audio_buffer->mData) + (is->bytes_per_sample * ch);
            isca.areas[ch].step = is->bytes_per_frame;
        }
    }
    else
    {
        assert(isca.buffer_list->mNumberBuffers == is->layout.channel_count);
        for (int ch = 0; ch < is->layout.channel_count; ch++)
        {
            auto* audio_buffer = &isca.buffer_list->mBuffers[ch];
            assert(audio_buffer->mDataByteSize == in_number_frames * is->bytes_per_sample);
            isca.areas[ch].ptr = static_cast<char*>(audio_buffer->mData);
            isca.areas[ch].step = is->bytes_per_sample;
        }
    }

    isca.frames_left = static_cast<int>(in_number_frames);
    is_sp->read_callback(is_sp, isca.frames_left, isca.frames_left);

    return noErr;
}

/**
 * @brief 为 iOS 环境下进行捕获（Input）流初始化和启动配置
 * 
 * 处理了针对 Interleaved ASBD 和 kAudioOutputUnitProperty_SetInputCallback 环境的支持。
 * 
 * @param si 提供系统句柄引用的 SoundIoPrivate
 * @param is 正在构建并连接设备的输入流对象
 */
static int instream_open_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is)
{
    auto& isca = is->backend_data.coreaudio_ios;
    const auto dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(is->device);
    const auto dca = dev->backend_data.coreaudio_ios;
    UInt32 io_size;
    OSStatus os_err;

    if (is->software_latency == 0.0)
    {
        is->software_latency = dev->software_latency_current;
    }

    is->software_latency = std::clamp(is->software_latency, dev->software_latency_min, dev->software_latency_max);

    // 我们使用的 ASBD 是交错模式 (Interleaved)，所以 mNumberBuffers 应当固定为 1
    const auto num_buffers = static_cast<UInt32>(1);
    io_size = offsetof(AudioBufferList, mBuffers[0]) + (sizeof(AudioBuffer) * num_buffers);

    isca.buffer_list.reset(static_cast<AudioBufferList*>(soundio_malloc(io_size)));
    // 强制初始化结构体字段，防止 garbage value 导致崩溃
    isca.buffer_list->mNumberBuffers = num_buffers;
    isca.buffer_list->mBuffers[0].mNumberChannels = is->layout.channel_count;
    isca.buffer_list->mBuffers[0].mDataByteSize = 0;
    isca.buffer_list->mBuffers[0].mData = NULL;

    auto desc = AudioComponentDescription{};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    const auto component = AudioComponentFindNext(nullptr, &desc);
    if (!component) {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }
    
    AudioComponentInstance instance;
    
    os_err = AudioComponentInstanceNew(component, &instance);
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    isca.instance = std::unique_ptr<OpaqueAudioComponentInstance, CoreAudioInstanceDeleter>(instance, CoreAudioInstanceDeleter());
    instance = nullptr;

    os_err = AudioUnitInitialize(isca.instance.get());
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    auto enable_io = static_cast<UInt32>(1);
    os_err = AudioUnitSetProperty(isca.instance.get(), kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, INPUT_ELEMENT, &enable_io, sizeof(UInt32));
    
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    enable_io = 0;
    os_err = AudioUnitSetProperty(isca.instance.get(), kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, OUTPUT_ELEMENT, &enable_io, sizeof(UInt32));
    
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }
    
    
    auto format = AudioStreamBasicDescription{};
    format.mSampleRate = is->sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mBytesPerPacket = is->bytes_per_frame;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = is->bytes_per_frame;
    format.mChannelsPerFrame = is->layout.channel_count;

    const int err = set_ca_desc(is->format, &format);
    if (err)
    {
        instream_destroy_ca(si, is);
        return err;
    }
    os_err = AudioUnitSetProperty(isca.instance.get(), kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, INPUT_ELEMENT, &format, sizeof(AudioStreamBasicDescription));
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    const AURenderCallbackStruct input_callback = {CoreAudioCallback::read_callback, is.get()};
    os_err = AudioUnitSetProperty(isca.instance.get(), kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Output, INPUT_ELEMENT, &input_callback, sizeof(AURenderCallbackStruct));
    
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    isca.hardware_latency = dca->latency_frames / (double)is->sample_rate;

    return 0;
}

/**
 * @brief 处理暂停或重新运行 input 操作状态转换
 */
static int instream_pause_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is, bool pause) {
    auto* isca = &is->backend_data.coreaudio_ios;
    OSStatus os_err;
    if (pause)
    {
        os_err = AudioOutputUnitStop(isca->instance.get());
        if (os_err != noErr) {
            return SoundIoErrorStreaming;
        }
    }
    else
    {
        os_err = AudioOutputUnitStart(isca->instance.get());
        if (os_err != noErr)
        {
            return SoundIoErrorStreaming;
        }
    }

    return 0;
}

/**
 * @brief 解除屏蔽并绑定 stream 且令 capture 执行其首个 start 循环
 */
static int instream_start_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is) {
    return instream_pause_ca(si, is, false);
}

/**
 * @brief 读取并取得数据存储所需的 pointer reference
 */
static int instream_begin_read_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is,
                                  SoundIoChannelArea** out_areas, int* out_frame_count)
{
    auto* isca = &is->backend_data.coreaudio_ios;

    if (*out_frame_count != isca->frames_left)
        return SoundIoErrorInvalid;

    *out_areas = isca->areas;

    return 0;
}

/**
 * @brief 清除当前获取数据的周期使用标志位
 */
static int instream_end_read_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is) {
    auto* isca = &is->backend_data.coreaudio_ios;
    isca->frames_left = 0;
    return 0;
}

/**
 * @brief 取回 instream latency
 */
static int instream_get_latency_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is,
                                   double* out_latency)
{
    const auto* isca = &is->backend_data.coreaudio_ios;
    *out_latency = isca->hardware_latency;
    return 0;
}

/**
 * @brief 拒绝蓝牙耳机（AirPods 等）的「按下暂停」媒体键
 *
 * category = Playback 时我们是当前的音频焦点，AirPods 捏一下会由 MediaRemote
 * 发一条 togglePlayPause 命令过来；如果 App 不接管，系统就直接把 session
 * deactivate 掉（表现就是收到 AVAudioSessionInterruptionTypeBegan、AudioUnit
 * 被停）。对游戏/实时音频来说这是不可接受的。
 *
 * 这里把 togglePlayPause 认领下来并返回 Success（但什么都不做），MediaRemote
 * 认为命令已被处理，就不会再自动暂停我们；play/pause/stop 也一并禁用，避免误触。
 *
 * 注意：真正的外部打断（来电、闹钟、Siri、其它 App 抢焦点）无法拒绝，
 * 只能事后恢复。
 *
 * @param si 当前 SoundIoPrivate
 */
static void grab_remote_pause_commands(const std::shared_ptr<SoundIoPrivate>& si)
{
    auto& sica = si->backend_data->coreaudio_ios;
    if (sica.remoteToggleTarget != nil)
    {
        // 已经装过了
        return;
    }

    MPRemoteCommandCenter* cmd_center = MPRemoteCommandCenter.sharedCommandCenter;

    // 返回的 token 由 MPRemoteCommandCenter 持有强引用，这里只留指针用于摘除
    // 这个必须要设置,不设置下面管了
    sica.remoteToggleTarget = [cmd_center.togglePlayPauseCommand
            addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event)
            {
                LOGI(L"remote togglePlayPause ignored");
                return MPRemoteCommandHandlerStatusSuccess;
            }];

    cmd_center.playCommand.enabled = NO;
    cmd_center.pauseCommand.enabled = NO;
    cmd_center.stopCommand.enabled = NO;
    cmd_center.nextTrackCommand.enabled = NO;
    cmd_center.previousTrackCommand.enabled = NO;

    LOGI(L"remote play/pause commands are disabled (game mode)");
}

/**
 * @brief 把上面禁用的媒体命令还原，避免影响同进程里别的播放器
 *
 * @param si 当前 SoundIoPrivate
 */
static void release_remote_pause_commands(const std::shared_ptr<SoundIoPrivate>& si)
{
    auto& sica = si->backend_data->coreaudio_ios;
    MPRemoteCommandCenter* cmd_center = MPRemoteCommandCenter.sharedCommandCenter;

    if (sica.remoteToggleTarget != nil)
    {
        [cmd_center.togglePlayPauseCommand removeTarget:sica.remoteToggleTarget];
        sica.remoteToggleTarget = nil;
    }

    cmd_center.playCommand.enabled = YES;
    cmd_center.pauseCommand.enabled = YES;
    cmd_center.stopCommand.enabled = YES;
    cmd_center.nextTrackCommand.enabled = YES;
    cmd_center.previousTrackCommand.enabled = YES;
    LOGI(L"remote play/pause commands enable");
}

/**
 * @brief 回收所有的 Backend 资源以及相关的 system thread
 *
 * 强制发送停止 event 给 background device 处理线程以确保安全。
 *
 * @param si Global root 实例的共享指针
 */
static void destroy_ca(const std::shared_ptr<SoundIoPrivate>& si)
{
    LOGI(L"destroy core audio");
    if(audio_session_guard_is_locked()){
        audio_session_guard_unlock();
    }
    
    auto& sica = si->backend_data->coreaudio_ios;

    if (sica.thread) {
        std::unique_lock lock(sica.scan_devices_mutex->get());
        
        sica.thread->request_stop();
        sica.scan_devices_cond->signal(&lock);
        lock.unlock();
        sica.thread = nullptr;
    }

    sica.cond = nullptr;
    sica.scan_devices_cond = nullptr;
    sica.mutex = nullptr;
    sica.ready_devices_info = nullptr;

    // 用弱引用注册表找回已打开的流：即使宿主没登记强引用，也要把 AU 停掉
    for (const auto& weak_os: si->outstreams | std::ranges::views::values)
    {
        if (const auto os = weak_os.lock())
        {
            outstream_destroy_ca(si, std::dynamic_pointer_cast<SoundIoOutStreamPrivate>(os));
        }
    }
    si->ClearOutStreams();

    // 两个观察者都要摘掉（之前只摘了 notifyCallback，routeCallback 一直留在
    // NSNotificationCenter 里，过期后还会被回调）
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    if (sica.notifyCallback != nil)
    {
        [center removeObserver:sica.notifyCallback];
        sica.notifyCallback = nil;
    }
    if (sica.routeCallback != nil)
    {
        [center removeObserver:sica.routeCallback];
        sica.routeCallback = nil;
    }

    // 还原媒体键（否则同进程里其它播放器会受影响）
    release_remote_pause_commands(si);
}

static int set_record_route(const std::shared_ptr<SoundIoPrivate>& si){
    NSError* ns_err = nil;
    AVAudioSession* session = AVAudioSession.sharedInstance;
    AVAudioSessionCategoryOptions options = AVAudioSessionCategoryOptionDefaultToSpeaker | AVAudioSessionCategoryOptionAllowBluetoothHFP | AVAudioSessionCategoryOptionAllowBluetoothA2DP;
    BOOL isOk = [session setCategory:AVAudioSessionCategoryPlayAndRecord withOptions:options error:&ns_err];
    if(!isOk){
        LOGE(L"Audio setCategory Error: %hs", ns_err.localizedDescription.UTF8String);
        destroy_ca(si);
        return SoundIoErrorInitAudioBackend;
    }
    
    isOk = [session setMode:AVAudioSessionModeMeasurement error:&ns_err];
    if(!isOk){
        LOGE(L"Audio setMode Error: %hs", ns_err.localizedDescription.UTF8String);
        destroy_ca(si);
        return SoundIoErrorInitAudioBackend;
    }
    
    isOk = [session setActive:YES error:&ns_err];
    if (!isOk) {
        LOGE(L"Audio Session Error: %hs", ns_err.localizedDescription.UTF8String);
        destroy_ca(si);
        return SoundIoErrorInitAudioBackend;
    }
    return SoundIoErrorNone;
}

static int set_playback_route(const std::shared_ptr<SoundIoPrivate>& si){
    NSError* ns_err = nil;
    AVAudioSession* session = AVAudioSession.sharedInstance;
    
    BOOL isOk = [session setCategory:AVAudioSessionCategoryPlayback withOptions:NULL error:&ns_err];
    if(!isOk){
        LOGE(L"Audio setCategory Error: %hs", ns_err.localizedDescription.UTF8String);
        destroy_ca(si);
        return SoundIoErrorInitAudioBackend;
    }
    
    isOk = [session setMode:AVAudioSessionModeDefault error:&ns_err];
    if(!isOk){
        LOGE(L"Audio setMode Error: %hs", ns_err.localizedDescription.UTF8String);
        destroy_ca(si);
        return SoundIoErrorInitAudioBackend;
    }
    
    isOk = [session setActive:YES error:&ns_err];
    if (!isOk) {
        LOGE(L"Audio Session Error: %hs", ns_err.localizedDescription.UTF8String);
        destroy_ca(si);
        return SoundIoErrorInitAudioBackend;
    }
    return SoundIoErrorNone;
}


/**
 * @brief 为 iOS CoreAudio 支持挂载 system primitives, threads 和 callback assignments
 * 
 * @param si 初始化并设定好的系统级根指针状态
 */
int soundio_coreaudio_init(const std::shared_ptr<SoundIoPrivate>& si) 
{
    auto& sica = si->backend_data->coreaudio_ios;
    sica.service_restarted.store(false);
    
    // 延迟测试把下一行取消注释,否则麦克风不能被打开
//#define RECORD_MODE
    
#ifndef RECORD_MODE
    int err = set_playback_route(si);
#else
    int err = set_record_route(si);
#endif
    if(err){
        destroy_ca(si);
        return err;
    }
    
    sica.mutex = soundio_os_mutex_create();
    if (!sica.mutex) {
        destroy_ca(si);
        return SoundIoErrorNoMem;
    }

    sica.cond = soundio_os_cond_create();
    if (!sica.cond) {
        destroy_ca(si);
        return SoundIoErrorNoMem;
    }

    sica.scan_devices_mutex = soundio_os_mutex_create();
    if (!sica.scan_devices_mutex)
    {
        destroy_ca(si);
        return SoundIoErrorNoMem;
    }

    sica.scan_devices_cond = soundio_os_cond_create();
    if (!sica.scan_devices_cond) {
        destroy_ca(si);
        return SoundIoErrorNoMem;
    }

    sica.thread = SoundIoOsThread::create(device_thread_run, si);
    
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;

    // 注意：**不能**直接在 block 里捕获 si。
    // si 是 `const std::shared_ptr<SoundIoPrivate>&` 这种**引用参数**，clang 对这种
    // 引用参数的 block 捕获不会 copy-construct 捕获对象（实测 use_count 不变），
    // 于是 block 里拿到的是一个悬空副本：宿主一释放（ar_shutdown 后），回调里
    // s 就变成 nullptr，或者 use_count 是垃圾值。
    // 所以先落到局部变量再捕获；并且用 weak_ptr，避免观察者反过来把
    // SoundIoPrivate 钉住不放（否则 soundio_destroy 之后对象永远不析构）。
    const std::weak_ptr<SoundIoPrivate> weak_si = si;

    if(sica.notifyCallback == nil)
    {
        sica.notifyCallback = [center addObserverForName:AVAudioSessionInterruptionNotification object:[AVAudioSession sharedInstance] queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification* note){
            if (const auto s = weak_si.lock())
            {
                CoreAudioCallback::on_notification(note, s);
            }
        }];
    }
    
    if(sica.routeCallback == nil){
        sica.routeCallback = [center addObserverForName:AVAudioSessionRouteChangeNotification object:[AVAudioSession sharedInstance] queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification* note){
            if (const auto s = weak_si.lock())
            {
                CoreAudioCallback::on_route_notification(note, s);
            }
        }];
    }

    // 拒绝 AirPods / 蓝牙耳机的「按下暂停」：接管 MediaRemote 的播放类命令
    grab_remote_pause_commands(si);
    
    
    si->destroy = destroy_ca;
    si->flush_events = flush_events_ca;
    si->wait_events = wait_events_ca;
    si->wakeup = wakeup_ca;
    si->force_device_scan = force_device_scan_ca;

    si->outstream_open = outstream_open_ca;
    si->outstream_destroy = outstream_destroy_ca;
    si->outstream_start = outstream_start_ca;
    si->outstream_begin_write = outstream_begin_write_ca;
    si->outstream_end_write = outstream_end_write_ca;
    si->outstream_clear_buffer = outstream_clear_buffer_ca;
    si->outstream_pause = outstream_pause_ca;
    si->outstream_get_latency = outstream_get_latency_ca;
    si->outstream_set_volume = outstream_set_volume_ca;

    si->instream_open = instream_open_ca;
    si->instream_destroy = instream_destroy_ca;
    si->instream_start = instream_start_ca;
    si->instream_begin_read = instream_begin_read_ca;
    si->instream_end_read = instream_end_read_ca;
    si->instream_pause = instream_pause_ca;
    si->instream_get_latency = instream_get_latency_ca;

    return 0;
}
