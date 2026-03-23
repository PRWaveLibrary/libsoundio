/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "coreaudio_ios.h"
#include "soundio_private.h"
#include "util.h"

#include <cassert>
#include <AVFoundation/AVFoundation.h>
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
static int refresh_devices(std::shared_ptr<SoundIoPrivate> si) {
    SoundIoCoreAudioIOS& sica = si->backend_data->coreaudio_ios;

    std::unique_ptr<SoundIoDevicesInfo> devices_info = std::make_unique<SoundIoDevicesInfo>();

    devices_info->default_output_index = 0;
    devices_info->default_input_index = 0;
    
    AVAudioSession* session = AVAudioSession.sharedInstance;
    
    for (size_t aim_i = 0; aim_i < std::size(aims); aim_i += 1)
    {
        enum SoundIoDeviceAim aim = aims[aim_i];
        std::shared_ptr<SoundIoDevicePrivate> dev = std::make_shared<SoundIoDevicePrivate>();
        
        dev->soundio = si;
        dev->is_raw = false;
        dev->aim = aim;
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
static void shutdown_backend(std::shared_ptr<SoundIoPrivate> si, int err)
{
    SoundIoCoreAudioIOS& sica = si->backend_data->coreaudio_ios;
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
static void my_flush_events(std::shared_ptr<SoundIoPrivate>& si, bool wait)
{
    SoundIoCoreAudioIOS& sica = si->backend_data->coreaudio_ios;

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
static void flush_events_ca(std::shared_ptr<SoundIoPrivate> si)
{
    my_flush_events(si, false);
}

/**
 * @brief 含有阻塞等待特征的 iOS backend event flush hooks。
 */
static void wait_events_ca(std::shared_ptr<SoundIoPrivate> si)
{
    my_flush_events(si, true);
}

/**
 * @brief 在外部进行调用，唤醒正被 CoreAudio 逻辑阻塞中的 event loop
 */
static void wakeup_ca(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoCoreAudioIOS& sica = si->backend_data->coreaudio_ios;
    std::unique_lock lock(sica.mutex->get());
    sica.cond->signal(&lock);
}

/**
 * @brief 强行请求 background thread 执行 devices 扫描流程
 */
static void force_device_scan_ca(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoCoreAudioIOS& sica = si->backend_data->coreaudio_ios;
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
static void device_thread_run(std::shared_ptr<void> arg) {
    std::shared_ptr<SoundIoPrivate> si = std::static_pointer_cast<SoundIoPrivate>(arg);
    SoundIoCoreAudioIOS& sica = si->backend_data->coreaudio_ios;
    
    std::unique_lock lock(sica.scan_devices_mutex->get());

    while (!sica.abort_flag.test())
    {
        if (sica.service_restarted.load())
        {
            shutdown_backend(si, SoundIoErrorBackendDisconnected);
            break;
        }
        if (sica.device_scan_queued.exchange(false))
        {
            lock.unlock();
            if (int err = refresh_devices(si))
            {
                shutdown_backend(si, err);
                lock.lock();
                break;
            }
            flush_events_ca(si);
            lock.lock();
            continue;
        }

        sica.scan_devices_cond->wait(&lock);
    }
    lock.unlock();
}

/**
 * @brief 卸载 OutStream 关联分配的 iOS CoreAudio 运行实例资源
 * 
 * @param si 当前的 SoundIoPrivate instance
 * @param os 需要解绑设备的 output stream
 */
static void outstream_destroy_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    LOGI("destroy core audio outstream");
    os->backend_data.coreaudio_ios.instance = nullptr;
    si->backend_data->coreaudio_ios.callback->out_stream.reset();
}

/**
 * @brief 拦截并填充底层 AudioUnitRender 的原生写回回调 (Implementation)
 * 
 * 真正的 iOS 底层 callback，由 AudioBufferList io_data 将请求桥接到 libsoundio API 的 write_callback。
 */
OSStatus CoreAudioCallback::write_callback_ca(AudioUnitRenderActionFlags *io_action_flags, const AudioTimeStamp *in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames, AudioBufferList *io_data)
{
    std::shared_ptr<SoundIoOutStreamPrivate> os = out_stream.lock();
    if(os == nullptr)
    {
        for (UInt32 i = 0; i < io_data->mNumberBuffers; ++i)
        {
            memset(io_data->mBuffers[i].mData, 0, io_data->mBuffers[i].mDataByteSize);
        }
        LOGE("os is nullptr.");
        return noErr;
    }
    
    auto& osca = os->backend_data.coreaudio_ios;
    
    osca.io_data = io_data;
    osca.buffer_index = 0;
    osca.frames_left = in_number_frames;
    os->write_callback(os, osca.frames_left, osca.frames_left);
    osca.io_data = nullptr;

    return noErr;
}


/**
 * @brief iOS RemoteIO output unit 的 C 风格全局或静态写回调入口
 */
OSStatus CoreAudioCallback::write_callback(void *userdata, AudioUnitRenderActionFlags *io_action_flags,
    const AudioTimeStamp *in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
    AudioBufferList *io_data)
{
    CoreAudioCallback* callback = static_cast<CoreAudioCallback*>(userdata);
    return callback->write_callback_ca(io_action_flags, in_time_stamp, in_bus_number, in_number_frames, io_data);
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
static int outstream_open_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    SoundIoOutStreamCoreAudioIOS& osca = os->backend_data.coreaudio_ios;
    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(os->device);
    std::shared_ptr<SoundIoDeviceCoreAudioIOS> dca = dev->backend_data.coreaudio_ios;

    if (os->software_latency == 0.0)
    {
        // 外部未指定则使用五毫秒,48000约256byte
        os->software_latency = 0.005;
    }

    os->software_latency = soundio_double_clamp(dev->software_latency_min, os->software_latency, dev->software_latency_max);
    
    NSError* ns_err;
    AVAudioSession* session = AVAudioSession.sharedInstance;
    BOOL isOk = [session setPreferredIOBufferDuration:os->software_latency error:&ns_err];
    if(!isOk){
        LOGE("Audio Session Error: {}", ns_err.localizedDescription.UTF8String);
        outstream_destroy_ca(si,os);
        return SoundIoErrorOpeningDevice;
    }
    
    
    isOk = [session setActive:YES error:&ns_err];
    if (!isOk) {
        LOGE("Audio Session Error: {}", ns_err.localizedDescription.UTF8String);
        outstream_destroy_ca(si,os);
        return SoundIoErrorOpeningDevice;
    }
    
    
    AudioComponentDescription desc = {0};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    // 查找组件
    AudioComponent component = AudioComponentFindNext(NULL, &desc);
    if (!component) {
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    AudioComponentInstance instance;
    
    // 实例音频单元
    OSStatus os_err = AudioComponentInstanceNew(component, &instance);
    if (os_err != noErr)
    {
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    osca.instance = std::unique_ptr<OpaqueAudioComponentInstance,CoreAudioInstanceDeleter>(instance, CoreAudioInstanceDeleter());
    instance = nullptr;
    
    os_err = AudioUnitInitialize(osca.instance.get());
    if (os_err != noErr)
    {
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    AudioStreamBasicDescription format = {0};
    auto& infos = si->safe_devices_info;
    auto default_out_index = infos->default_output_index;
    if (infos->output_devices.size() <= default_out_index)
    {
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }
    
    auto default_output = infos->output_devices.at(infos->default_output_index);
    format.mSampleRate = default_output->sample_rate_current;
    format.mFormatID = kAudioFormatLinearPCM;
    int err = set_ca_desc(os->format, &format);
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

    AURenderCallbackStruct render_callback = {CoreAudioCallback::write_callback, si->backend_data->coreaudio_ios.callback.get()};
    os_err = AudioUnitSetProperty(osca.instance.get(), kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, OUTPUT_ELEMENT, &render_callback, sizeof(AURenderCallbackStruct));
    if (os_err != noErr)
    {
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    os->volume = 1.0;
    osca.hardware_latency = dca->latency_frames / static_cast<double>(os->sample_rate);
    os->sample_rate = format.mSampleRate;
    
    
    
    
    return 0;
}

/**
 * @brief 执行对于实例层面的 AudioOutputUnitStop 或 AudioOutputUnitStart 播放暂停操作
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param os 当前操作的 output stream
 * @param pause 指出状态是 pause 或 resume
 * @return int 操作返回
 */
static int outstream_pause_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, bool pause) {
    SoundIoOutStreamCoreAudioIOS& osca = os->backend_data.coreaudio_ios;
    OSStatus os_err;
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
        os_err = AudioOutputUnitStart(osca.instance.get());
        if (os_err != noErr)
        {
            return SoundIoErrorStreaming;
        }
    }

    return 0;
}

/**
 * @brief OutStream 设置后首次调用使得该流挂载进 callbacks 中
 */
static int outstream_start_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os) {
    si->backend_data->coreaudio_ios.callback->out_stream = os;
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
static int outstream_begin_write_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, struct SoundIoChannelArea **out_areas, int *frame_count)
{
    SoundIoOutStreamCoreAudioIOS& osca = os->backend_data.coreaudio_ios;

    if (osca.buffer_index >= osca.io_data->mNumberBuffers)
    {
        return SoundIoErrorInvalid;
    }

    if (*frame_count != osca.frames_left)
    {
        return SoundIoErrorInvalid;
    }

    AudioBuffer *audio_buffer = &osca.io_data->mBuffers[osca.buffer_index];
    assert(audio_buffer->mNumberChannels == os->layout.channel_count);
    osca.write_frame_count = audio_buffer->mDataByteSize / os->bytes_per_frame;
    *frame_count = osca.write_frame_count;
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
static int outstream_end_write_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    SoundIoOutStreamCoreAudioIOS& osca = os->backend_data.coreaudio_ios;
    osca.buffer_index += 1;
    osca.frames_left -= osca.write_frame_count;
    assert(osca.frames_left >= 0);
    return 0;
}

/**
 * @brief 清空 stream 的缓存
 */
static int outstream_clear_buffer_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    return SoundIoErrorIncompatibleBackend;
}

/**
 * @brief 获取 Stream iOS 内核的运行 hardware latency
 */
static int outstream_get_latency_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, double *out_latency)
{
    SoundIoOutStreamCoreAudioIOS& osca = os->backend_data.coreaudio_ios;
    *out_latency = osca.hardware_latency;
    return 0;
}

/**
 * @brief 配置独立 stream 当前可设的 volume
 */
static int outstream_set_volume_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, float volume)
{
    os->volume = volume;
    return 0;
}

/**
 * @brief 卸载 iOS InStream 申请的 RemoteIO 麦克风录音实例
 */
static void instream_destroy_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    // 先 stop AudioUnit（同步等待正在运行的回调结束），再清 weak_ptr
    SoundIoInStreamCoreAudioIOS& isca = is->backend_data.coreaudio_ios;
    isca.instance = nullptr;
    isca.buffer_list = nullptr;
    si->backend_data->coreaudio_ios.callback->in_stream.reset();
}

void CoreAudioCallback::on_notification(NSNotification* note,std::shared_ptr<SoundIoPrivate> si)
{
    si->backend_data->coreaudio_ios.callback->on_notification_ca(note);
}


void CoreAudioCallback::on_notification_ca(NSNotification* note){
    auto s = si.lock();
    if(!s){
        return;
    }
    NSDictionary* userInfo = note.userInfo;
    AVAudioSessionInterruptionType type = static_cast<AVAudioSessionInterruptionType>([userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue]);
    if (type == AVAudioSessionInterruptionTypeBegan){
        // begin
        
        LOGI("apple notification pause.");
        s->outstream_pause(s,std::dynamic_pointer_cast<SoundIoOutStreamPrivate>(s->outstream),true);
    }
    else{
        // end
        LOGI("apple notification resume begin.");
        AVAudioSessionInterruptionOptions options = [userInfo[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue];
        if (!(options & AVAudioSessionInterruptionOptionShouldResume))
        {
            LOGI("apple notification disable resume.");
            return;
        }
        s->outstream_pause(s,std::dynamic_pointer_cast<SoundIoOutStreamPrivate>(s->outstream),false);
        LOGI("apple notification resume finished.");
    }
}

/**
 * @brief C 风格调用签名，负责触发实例底层的方法去获取录制的数据
 */
OSStatus CoreAudioCallback::read_callback(void *userdata, AudioUnitRenderActionFlags *io_action_flags,
    const AudioTimeStamp *in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
                                             AudioBufferList *io_data)
{
    CoreAudioCallback* callback = static_cast<CoreAudioCallback*>(userdata);
    return callback->read_callback_ca(io_action_flags, in_time_stamp, in_bus_number, in_number_frames, io_data);
}


/**
 * @brief 本地调用真正的实现以提取接收到的 AudioBufferList 并返回应用可读取的 padding
 */
OSStatus CoreAudioCallback::read_callback_ca(AudioUnitRenderActionFlags *io_action_flags, const AudioTimeStamp *in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames, AudioBufferList *io_data)
{
    auto is = in_stream.lock();
    if(is == nullptr)
    {
        LOGE("is is nullptr.");
        return noErr;
    }
    
    
    SoundIoInStreamCoreAudioIOS& isca = is->backend_data.coreaudio_ios;

    for (int i = 0; i < isca.buffer_list->mNumberBuffers; i++)
    {
        isca.buffer_list->mBuffers[i].mData = NULL;
    }

    OSStatus os_err = AudioUnitRender(isca.instance.get(), io_action_flags, in_time_stamp, in_bus_number, in_number_frames, isca.buffer_list.get());
    if (os_err != noErr)
    {
        is->error_callback(is, SoundIoErrorStreaming);
        return noErr;
    }

    if (isca.buffer_list->mNumberBuffers == 1)
    {
        AudioBuffer *audio_buffer = &isca.buffer_list->mBuffers[0];
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
            AudioBuffer* audio_buffer = &isca.buffer_list->mBuffers[ch];
            assert(audio_buffer->mDataByteSize == in_number_frames * is->bytes_per_sample);
            isca.areas[ch].ptr = static_cast<char*>(audio_buffer->mData);
            isca.areas[ch].step = is->bytes_per_sample;
        }
    }

    isca.frames_left = in_number_frames;
    is->read_callback(is, isca.frames_left, isca.frames_left);

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
static int instream_open_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    SoundIoInStreamCoreAudioIOS& isca = is->backend_data.coreaudio_ios;
    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(is->device);
    std::shared_ptr<SoundIoDeviceCoreAudioIOS> dca = dev->backend_data.coreaudio_ios;
    UInt32 io_size;
    OSStatus os_err;

    if (is->software_latency == 0.0)
    {
        is->software_latency = dev->software_latency_current;
    }

    is->software_latency = soundio_double_clamp(dev->software_latency_min, is->software_latency, dev->software_latency_max);

    // 我们使用的 ASBD 是交错模式 (Interleaved)，所以 mNumberBuffers 应当固定为 1
    UInt32 num_buffers = 1;
    io_size = offsetof(AudioBufferList, mBuffers[0]) + (sizeof(AudioBuffer) * num_buffers);

    isca.buffer_list = std::unique_ptr<AudioBufferList,decltype(&std::free)>((AudioBufferList*)malloc(io_size),std::free);
    // 强制初始化结构体字段，防止 garbage value 导致崩溃
    isca.buffer_list->mNumberBuffers = num_buffers;
    isca.buffer_list->mBuffers[0].mNumberChannels = is->layout.channel_count;
    isca.buffer_list->mBuffers[0].mDataByteSize = 0;
    isca.buffer_list->mBuffers[0].mData = NULL;

    AudioComponentDescription desc = {0};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent component = AudioComponentFindNext(NULL, &desc);
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

    isca.instance = std::unique_ptr<OpaqueAudioComponentInstance,CoreAudioInstanceDeleter>(instance,CoreAudioInstanceDeleter());
    instance = nullptr;

    os_err = AudioUnitInitialize(isca.instance.get());
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    UInt32 enable_io = 1;
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
    
    
    AudioStreamBasicDescription format = {0};
    format.mSampleRate = is->sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mBytesPerPacket = is->bytes_per_frame;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = is->bytes_per_frame;
    format.mChannelsPerFrame = is->layout.channel_count;

    int err = set_ca_desc(is->format, &format);
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

    AURenderCallbackStruct input_callback = {CoreAudioCallback::read_callback, si->backend_data->coreaudio_ios.callback.get()};
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
static int instream_pause_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is, bool pause) {
    struct SoundIoInStreamCoreAudioIOS *isca = &is->backend_data.coreaudio_ios;
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
static int instream_start_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is) {
    si->backend_data->coreaudio_ios.callback->in_stream = is;
    return instream_pause_ca(si, is, false);
}

/**
 * @brief 读取并取得数据存储所需的 pointer reference
 */
static int instream_begin_read_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is,
        struct SoundIoChannelArea **out_areas, int *frame_count)
{
    struct SoundIoInStreamCoreAudioIOS *isca = &is->backend_data.coreaudio_ios;

    if (*frame_count != isca->frames_left)
        return SoundIoErrorInvalid;

    *out_areas = isca->areas;

    return 0;
}

/**
 * @brief 清除当前获取数据的周期使用标志位
 */
static int instream_end_read_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is) {
    struct SoundIoInStreamCoreAudioIOS *isca = &is->backend_data.coreaudio_ios;
    isca->frames_left = 0;
    return 0;
}

/**
 * @brief 取回 instream latency
 */
static int instream_get_latency_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is,
        double *out_latency)
{
    struct SoundIoInStreamCoreAudioIOS *isca = &is->backend_data.coreaudio_ios;
    *out_latency = isca->hardware_latency;
    return 0;
}

/**
 * @brief 回收所有的 Backend 资源以及相关的 system thread
 *
 * 强制发送停止 event 给 background device 处理线程以确保安全。
 *
 * @param si Global root 实例的共享指针
 */
static void destroy_ca(std::shared_ptr<SoundIoPrivate> si)
{
    LOGI("destroy core audio");
    if(audio_session_guard_is_locked()){
        audio_session_guard_unlock();
    }
    
    SoundIoCoreAudioIOS& sica = si->backend_data->coreaudio_ios;

    if (sica.thread) {
        std::unique_lock lock(sica.scan_devices_mutex->get());
        
        sica.abort_flag.test_and_set();
        sica.scan_devices_cond->signal(&lock);
        lock.unlock();
        sica.thread = nullptr;
    }

    sica.cond = nullptr;
    sica.scan_devices_cond = nullptr;
    sica.mutex = nullptr;
    sica.ready_devices_info = nullptr;

    outstream_destroy_ca(si, std::dynamic_pointer_cast<SoundIoOutStreamPrivate>(si->outstream));
    if(sica.notifyCallback != nil)
    {
        NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
        [center removeObserver:sica.notifyCallback];
        sica.notifyCallback = nil;
    }
}



/**
 * @brief 为 iOS CoreAudio 支持挂载 system primitives, threads 和 callback assignments
 * 
 * @param si 初始化并设定好的系统级根指针状态
 */
int soundio_coreaudio_init(std::shared_ptr<SoundIoPrivate> si) {
    SoundIoCoreAudioIOS& sica = si->backend_data->coreaudio_ios;

    sica.have_devices_flag.store(false);
    sica.device_scan_queued.store(true);
    sica.service_restarted.store(false);
    sica.abort_flag.clear();
    
    NSError* ns_err = nil;
    AVAudioSession* session = AVAudioSession.sharedInstance;
    
    AVAudioSessionCategoryOptions options = AVAudioSessionCategoryOptionDefaultToSpeaker | AVAudioSessionCategoryOptionAllowBluetoothHFP | AVAudioSessionCategoryOptionAllowBluetoothA2DP;
    BOOL isOk = [session setCategory:AVAudioSessionCategoryPlayAndRecord withOptions:options error:&ns_err];
    if(!isOk){
        LOGE("Audio setCategory Error: {}", ns_err.localizedDescription.UTF8String);
        destroy_ca(si);
        return SoundIoErrorInitAudioBackend;
    }
    
    isOk = [session setMode:AVAudioSessionModeMeasurement error:&ns_err];
    if(!isOk){
        LOGE("Audio setMode Error: {}", ns_err.localizedDescription.UTF8String);
        destroy_ca(si);
        return SoundIoErrorInitAudioBackend;
    }
    
    isOk = [session setActive:YES error:&ns_err];
    if (!isOk) {
        LOGE("Audio Session Error: {}", ns_err.localizedDescription.UTF8String);
        destroy_ca(si);
        return SoundIoErrorInitAudioBackend;
    }
    
//    if(!audio_session_guard_is_locked()){
//        audio_session_guard_lock();
//    }
    
    
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
    sica.callback->si = si;
    
    if(sica.notifyCallback == nil)
    {
        NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
        sica.notifyCallback = [center addObserverForName:AVAudioSessionInterruptionNotification object:[AVAudioSession sharedInstance] queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification* note){
            CoreAudioCallback::on_notification(note,si);
        }];
    }
    
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
