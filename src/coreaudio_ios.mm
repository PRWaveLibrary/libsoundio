/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "coreaudio_ios.h"
#include "soundio_private.h"

#include <assert.h>
#include <AVFoundation/AVFoundation.h>

static const int OUTPUT_ELEMENT = 0;
static const int INPUT_ELEMENT = 1;

#define ERROR_LOG(msg) \
NSLog(@"%s:%d: [ERROR] %s\n", __FILE__, __LINE__, msg)

static enum SoundIoDeviceAim aims[] = {
    SoundIoDeviceAimInput,
    SoundIoDeviceAimOutput,
};


void CoreAudioInstanceDeleter::operator()(OpaqueAudioComponentInstance* stream) const{
    if(stream)
    {
        AudioOutputUnitStop(stream);
        AudioComponentInstanceDispose(stream);
    }
    
}


static void destroy_ca(std::shared_ptr<SoundIoPrivate> si) {
    SoundIoCoreAudioIOS& sica = si->backend_data->coreaudio_ios;


    if (sica.thread) {
        SOUNDIO_ATOMIC_FLAG_CLEAR(sica.abort_flag);
        soundio_os_cond_signal(sica.scan_devices_cond.get(), NULL);
        sica.thread = nullptr;
    }

    sica.cond = nullptr;
    sica.have_devices_cond = nullptr;
    sica.scan_devices_cond = nullptr;
    sica.mutex = nullptr;
    sica.ready_devices_info = nullptr;
}

static int refresh_devices(std::shared_ptr<SoundIoPrivate> si) {
    SoundIoCoreAudioIOS& sica = si->backend_data->coreaudio_ios;

    int err;
    
    std::unique_ptr<SoundIoDevicesInfo> devices_info = std::make_unique<SoundIoDevicesInfo>();

    devices_info->default_output_index = 0;
    devices_info->default_input_index = 0;
    
    AVAudioSession* session = AVAudioSession.sharedInstance;
    
    for (size_t aim_i = 0; aim_i < std::size(aims); aim_i += 1) {
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

        dev->sample_rate_current = session.sampleRate;
        dev->sample_rates.push_back(SoundIoSampleRateRange{dev->sample_rate_current, dev->sample_rate_current});
        
        dev->software_latency_current = aim == SoundIoDeviceAimOutput ? session.outputLatency : session.inputLatency;
        dev->software_latency_min = dev->software_latency_current;
        dev->software_latency_max = dev->software_latency_current;
        
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

    soundio_os_mutex_lock(sica.mutex);
    sica.ready_devices_info = std::move(devices_info);
    soundio_os_mutex_unlock(sica.mutex);

    return 0;
}

static void shutdown_backend(std::shared_ptr<SoundIoPrivate> si, int err)
{
    SoundIoCoreAudioIOS& sica = si->backend_data->coreaudio_ios;
    soundio_os_mutex_lock(sica.mutex);
    sica.shutdown_err = err;
    SOUNDIO_ATOMIC_STORE(sica.have_devices_flag, true);
    soundio_os_mutex_unlock(sica.mutex);
    soundio_os_cond_signal(sica.cond.get(), nullptr);
    soundio_os_cond_signal(sica.have_devices_cond.get(), nullptr);
    si->on_events_signal(si);
}

static void my_flush_events(std::shared_ptr<SoundIoPrivate>& si, bool wait)
{
    SoundIoCoreAudioIOS& sica = si->backend_data->coreaudio_ios;

    bool change = false;
    bool cb_shutdown = false;

    soundio_os_mutex_lock(sica.mutex);

    // block until have devices
    while (wait || (!SOUNDIO_ATOMIC_LOAD(sica.have_devices_flag) && !sica.shutdown_err))
    {
        soundio_os_cond_wait(sica.cond.get(), sica.mutex.get());
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

    soundio_os_mutex_unlock(sica.mutex);

    if (cb_shutdown)
        si->on_backend_disconnect(si, sica.shutdown_err);
    else if (change)
        si->on_devices_change(si);
}

static void flush_events_ca(std::shared_ptr<SoundIoPrivate> si)
{
    my_flush_events(si, false);
}

static void wait_events_ca(std::shared_ptr<SoundIoPrivate> si)
{
    my_flush_events(si, true);
}

static void wakeup_ca(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoCoreAudioIOS& sica = si->backend_data->coreaudio_ios;
    soundio_os_cond_signal(sica.cond.get(), NULL);
}

static void force_device_scan_ca(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoCoreAudioIOS& sica = si->backend_data->coreaudio_ios;
    SOUNDIO_ATOMIC_STORE(sica.device_scan_queued, true);
    soundio_os_cond_signal(sica.scan_devices_cond.get(), NULL);
}
 
static void device_thread_run(std::shared_ptr<void> arg) {
    std::shared_ptr<SoundIoPrivate> si = std::static_pointer_cast<SoundIoPrivate>(arg);
    SoundIoCoreAudioIOS& sica = si->backend_data->coreaudio_ios;
    int err;

    for (;;)
    {
        if (!SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(sica.abort_flag))
        {
            break;
        }
        if (SOUNDIO_ATOMIC_LOAD(sica.service_restarted))
        {
            shutdown_backend(si, SoundIoErrorBackendDisconnected);
            return;
        }
        if (SOUNDIO_ATOMIC_EXCHANGE(sica.device_scan_queued, false))
        {
            err = refresh_devices(si);
            if(err)
            {
                shutdown_backend(si,err);
            }
            if (!SOUNDIO_ATOMIC_EXCHANGE(sica.have_devices_flag, true))
            {
                soundio_os_cond_signal(sica.have_devices_cond.get(), NULL);
            }
            soundio_os_cond_signal(sica.cond.get(), NULL);
            si->on_events_signal(si);
        }
        soundio_os_cond_wait(sica.scan_devices_cond.get(), NULL);
    }
}

static void outstream_destroy_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    os->backend_data.coreaudio_ios.instance = nullptr;
}

OSStatus CoreAudioCallback::write_callback_ca(AudioUnitRenderActionFlags *io_action_flags, const AudioTimeStamp *in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames, AudioBufferList *io_data)
{
    std::shared_ptr<SoundIoOutStreamPrivate> os = out_stream.lock();
    if(os == nullptr)
    {
        ERROR_LOG("os is nullptr.");
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


OSStatus CoreAudioCallback::write_callback(void *userdata, AudioUnitRenderActionFlags *io_action_flags,
    const AudioTimeStamp *in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
    AudioBufferList *io_data)
{
    CoreAudioCallback* callback = static_cast<CoreAudioCallback*>(userdata);
    return callback->write_callback_ca(io_action_flags, in_time_stamp, in_bus_number, in_number_frames, io_data);
}

static int set_ca_desc(enum SoundIoFormat fmt, AudioStreamBasicDescription *desc) {
    switch (fmt) {
    case SoundIoFormatFloat32LE:
        desc->mFormatFlags = kAudioFormatFlagIsFloat;
        desc->mBitsPerChannel = 32;
        break;
    case SoundIoFormatFloat64LE:
        desc->mFormatFlags = kAudioFormatFlagIsFloat;
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

static int outstream_open_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os) {
    SoundIoOutStreamCoreAudioIOS& osca = os->backend_data.coreaudio_ios;
    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(os->device);
    std::shared_ptr<SoundIoDeviceCoreAudioIOS> dca = dev->backend_data.coreaudio_ios;

    if (os->software_latency == 0.0)
    {
        os->software_latency = dev->software_latency_current;
    }

    os->software_latency = soundio_double_clamp(dev->software_latency_min, os->software_latency, dev->software_latency_max);

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
    format.mSampleRate = os->sample_rate;
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
    os_err = AudioUnitSetProperty(osca.instance.get(), kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input, OUTPUT_ELEMENT, &format,sizeof(AudioStreamBasicDescription));
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

    // 由unity设置AVAudioSession来设置缓冲区
//    NSError* ns_err;
//    BOOL success = [AVAudioSession.sharedInstance setPreferredIOBufferDuration:outstream->software_latency error:&ns_err];
//    if(!success){
//        NSLog(@"setPreferredIOBufferDuration failed.");
//        outstream_destroy_ca(si, os);
//        return SoundIoErrorOpeningDevice;
//    }

    os->volume = 1.0;
    osca.hardware_latency = dca->latency_frames / (double)os->sample_rate;

    return 0;
}

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

static int outstream_start_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os) {
    return outstream_pause_ca(si, os, false);
}


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

static int outstream_end_write_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    SoundIoOutStreamCoreAudioIOS& osca = os->backend_data.coreaudio_ios;
    osca.buffer_index += 1;
    osca.frames_left -= osca.write_frame_count;
    assert(osca.frames_left >= 0);
    return 0;
}

static int outstream_clear_buffer_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    return SoundIoErrorIncompatibleBackend;
}

static int outstream_get_latency_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, double *out_latency)
{
    SoundIoOutStreamCoreAudioIOS& osca = os->backend_data.coreaudio_ios;
    *out_latency = osca.hardware_latency;
    return 0;
}

static int outstream_set_volume_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, float volume)
{
    os->volume = volume;
    return 0;
}

static void instream_destroy_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    SoundIoInStreamCoreAudioIOS& isca = is->backend_data.coreaudio_ios;
    isca.instance = nullptr;
    isca.buffer_list = nullptr;
}

OSStatus CoreAudioCallback::read_callback(void *userdata, AudioUnitRenderActionFlags *io_action_flags,
    const AudioTimeStamp *in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
                                             AudioBufferList *io_data)
{
    CoreAudioCallback* callback = static_cast<CoreAudioCallback*>(userdata);
    return callback->read_callback_ca(io_action_flags, in_time_stamp, in_bus_number, in_number_frames, io_data);
}


OSStatus CoreAudioCallback::read_callback_ca(AudioUnitRenderActionFlags *io_action_flags, const AudioTimeStamp *in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames, AudioBufferList *io_data)
{
    auto is = in_stream.lock();
    if(is == nullptr)
    {
        ERROR_LOG("is is nullptr.");
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
    
    AudioStreamBasicDescription input_format = {};
    io_size = sizeof(AudioStreamBasicDescription);
    os_err = AudioUnitGetProperty(isca.instance.get(), kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, INPUT_ELEMENT, &input_format, &io_size);
    
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    UInt32 num_buffers = input_format.mChannelsPerFrame;
    io_size = offsetof(AudioBufferList, mBuffers[0]) + (sizeof(AudioBuffer) * num_buffers);

    isca.buffer_list = std::unique_ptr<AudioBufferList,decltype(&std::free)>((AudioBufferList*)malloc(io_size),std::free);

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

    instance = nullptr;
    isca.instance = std::unique_ptr<OpaqueAudioComponentInstance,CoreAudioInstanceDeleter>(instance,CoreAudioInstanceDeleter());
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

static int instream_start_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is) {
    return instream_pause_ca(si, is, false);
}

static int instream_begin_read_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is,
        struct SoundIoChannelArea **out_areas, int *frame_count)
{
    struct SoundIoInStreamCoreAudioIOS *isca = &is->backend_data.coreaudio_ios;

    if (*frame_count != isca->frames_left)
        return SoundIoErrorInvalid;

    *out_areas = isca->areas;

    return 0;
}

static int instream_end_read_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is) {
    struct SoundIoInStreamCoreAudioIOS *isca = &is->backend_data.coreaudio_ios;
    isca->frames_left = 0;
    return 0;
}

static int instream_get_latency_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is,
        double *out_latency)
{
    struct SoundIoInStreamCoreAudioIOS *isca = &is->backend_data.coreaudio_ios;
    *out_latency = isca->hardware_latency;
    return 0;
}

int soundio_coreaudio_init(std::shared_ptr<SoundIoPrivate> si) {
    SoundIoCoreAudioIOS& sica = si->backend_data->coreaudio_ios;
    int err;

    SOUNDIO_ATOMIC_STORE(sica.have_devices_flag, false);
    SOUNDIO_ATOMIC_STORE(sica.device_scan_queued, true);
    SOUNDIO_ATOMIC_STORE(sica.service_restarted, false);
    SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(sica.abort_flag);
    
    // 这里unity会处理
//    NSError* ns_err = nil;
//    AVAudioSession* session = AVAudioSession.sharedInstance;
//    [session setCategory:AVAudioSessionCategoryPlayback error:&ns_err];
//    if(ns_err){
//        NSLog(@"Audio Session Error: %@", ns_err.localizedDescription);
//        destroy_ca(si);
//        return SoundIoErrorNoMem;
//    }
//    
//    [session setActive:YES error:&ns_err];
//    if (ns_err) {
//        NSLog(@"Audio Session Error: %@", ns_err.localizedDescription);
//        destroy_ca(si);
//        return SoundIoErrorNoMem;
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

    sica.have_devices_cond = soundio_os_cond_create();
    if (!sica.have_devices_cond) {
        destroy_ca(si);
        return SoundIoErrorNoMem;
    }

    sica.scan_devices_cond = soundio_os_cond_create();
    if (!sica.scan_devices_cond) {
        destroy_ca(si);
        return SoundIoErrorNoMem;
    }

    err = soundio_os_thread_create(device_thread_run, si, NULL, &sica.thread);
    if (err != SoundIoErrorNone) {
        destroy_ca(si);
        return err;
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
