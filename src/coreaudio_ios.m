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

static enum SoundIoDeviceAim aims[] = {
    SoundIoDeviceAimInput,
    SoundIoDeviceAimOutput,
};


static void destroy_ca(struct SoundIoPrivate *si) {
    struct SoundIoCoreAudioIOS *sica = &si->backend_data.coreaudio_ios;


    if (sica->thread) {
        SOUNDIO_ATOMIC_FLAG_CLEAR(sica->abort_flag);
        soundio_os_cond_signal(sica->scan_devices_cond, NULL);
        soundio_os_thread_destroy(sica->thread);
    }

    if (sica->cond)
        soundio_os_cond_destroy(sica->cond);

    if (sica->have_devices_cond)
        soundio_os_cond_destroy(sica->have_devices_cond);

    if (sica->scan_devices_cond)
        soundio_os_cond_destroy(sica->scan_devices_cond);

    if (sica->mutex)
        soundio_os_mutex_destroy(sica->mutex);

    soundio_destroy_devices_info(sica->ready_devices_info);
}


static void deinit_sound_io_devices_info(struct SoundIoDevicesInfo** sidi) {
    if(*sidi == NULL){
        return;
    }
    free(*sidi);
    *sidi = NULL;
}


static int refresh_devices(struct SoundIoPrivate *si) {
    struct SoundIo *soundio = &si->pub;
    struct SoundIoCoreAudioIOS *sica = &si->backend_data.coreaudio_ios;

    int err;

    struct SoundIoDevicesInfo* devices_info = NULL;

    if (!(devices_info = ALLOCATE(struct SoundIoDevicesInfo, 1))) {
        return SoundIoErrorNoMem;
    }

    devices_info->default_output_index = 0;
    devices_info->default_input_index = 0;
    
    AVAudioSession* session = AVAudioSession.sharedInstance;
    
    for (int aim_i = 0; aim_i < ARRAY_LENGTH(aims); aim_i += 1) {
        enum SoundIoDeviceAim aim = aims[aim_i];
        struct SoundIoDevicePrivate *dev = ALLOCATE(struct SoundIoDevicePrivate, 1);
        
        if (!dev) {
            deinit_sound_io_devices_info(&devices_info);
            return SoundIoErrorNoMem;
        }
        
        struct SoundIoDevice* device = &dev->pub;
        
        device->ref_count = 1;
        device->soundio = soundio;
        device->is_raw = false;
        device->aim = aim;
        
        device->layout_count = 1;
        device->layouts = &device->current_layout;
        device->current_layout.channel_count = aim == SoundIoDeviceAimOutput ? (int)session.outputNumberOfChannels : (int)session.inputNumberOfChannels;
        device->format_count = 1;
        device->formats = ALLOCATE(enum SoundIoFormat, device->format_count);
        
        if (!device->formats)
        {
            free(dev);
            deinit_sound_io_devices_info(&devices_info);
            return SoundIoErrorNoMem;
        }
        
        device->formats[0] = SoundIoFormatS32LE;
        device->sample_rate_current = session.sampleRate;
        device->sample_rate_count = 1;
        device->sample_rates = &dev->prealloc_sample_rate_range;
        device->sample_rates[0].min = device->sample_rate_current;
        device->sample_rates[0].max = device->sample_rate_current;
        
        device->software_latency_current = aim == SoundIoDeviceAimOutput ? session.outputLatency : session.inputLatency;
        device->software_latency_min = device->software_latency_current;
        device->software_latency_max = device->software_latency_current;
        
        struct SoundIoListDevicePtr *device_list;
        if (device->aim == SoundIoDeviceAimOutput) {
            device_list = &devices_info->output_devices;
                
        } else {
            assert(device->aim == SoundIoDeviceAimInput);
            device_list = &devices_info->input_devices;
        }

        if ((err = SoundIoListDevicePtr_append(device_list, device))) {
            free(dev);
            deinit_sound_io_devices_info(&devices_info);
            return err;
        }
    }

    soundio_os_mutex_lock(sica->mutex);
    soundio_destroy_devices_info(sica->ready_devices_info);
    sica->ready_devices_info = devices_info;
    soundio_os_mutex_unlock(sica->mutex);

    return 0;
}

static void shutdown_backend(struct SoundIoPrivate *si, int err) {
    struct SoundIo *soundio = &si->pub;
    struct SoundIoCoreAudioIOS *sica = &si->backend_data.coreaudio_ios;
    soundio_os_mutex_lock(sica->mutex);
    sica->shutdown_err = err;
    SOUNDIO_ATOMIC_STORE(sica->have_devices_flag, true);
    soundio_os_mutex_unlock(sica->mutex);
    soundio_os_cond_signal(sica->cond, NULL);
    soundio_os_cond_signal(sica->have_devices_cond, NULL);
    soundio->on_events_signal(soundio);
}

static void flush_events_ca(struct SoundIoPrivate *si) {
    struct SoundIo *soundio = &si->pub;
    struct SoundIoCoreAudioIOS *sica = &si->backend_data.coreaudio_ios;

    // block until have devices
    while (!SOUNDIO_ATOMIC_LOAD(sica->have_devices_flag))
        soundio_os_cond_wait(sica->have_devices_cond, NULL);

    bool change = false;
    bool cb_shutdown = false;
    struct SoundIoDevicesInfo *old_devices_info = NULL;

    soundio_os_mutex_lock(sica->mutex);

    if (sica->shutdown_err && !sica->emitted_shutdown_cb) {
        sica->emitted_shutdown_cb = true;
        cb_shutdown = true;
    } else if (sica->ready_devices_info) {
        old_devices_info = si->safe_devices_info;
        si->safe_devices_info = sica->ready_devices_info;
        sica->ready_devices_info = NULL;
        change = true;
    }

    soundio_os_mutex_unlock(sica->mutex);

    if (cb_shutdown)
        soundio->on_backend_disconnect(soundio, sica->shutdown_err);
    else if (change)
        soundio->on_devices_change(soundio);

    soundio_destroy_devices_info(old_devices_info);
}

static void wait_events_ca(struct SoundIoPrivate *si) {
    struct SoundIoCoreAudioIOS *sica = &si->backend_data.coreaudio_ios;
    flush_events_ca(si);
    soundio_os_cond_wait(sica->cond, NULL);
}

static void wakeup_ca(struct SoundIoPrivate *si) {
    struct SoundIoCoreAudioIOS *sica = &si->backend_data.coreaudio_ios;
    soundio_os_cond_signal(sica->cond, NULL);
}

static void force_device_scan_ca(struct SoundIoPrivate *si) {
    struct SoundIoCoreAudioIOS *sica = &si->backend_data.coreaudio_ios;
    SOUNDIO_ATOMIC_STORE(sica->device_scan_queued, true);
    soundio_os_cond_signal(sica->scan_devices_cond, NULL);
}
 
static void device_thread_run(void *arg) {
    struct SoundIoPrivate *si = (struct SoundIoPrivate *)arg;
    struct SoundIo *soundio = &si->pub;
    struct SoundIoCoreAudioIOS *sica = &si->backend_data.coreaudio_ios;
    int err;

    for (;;) {
        if (!SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(sica->abort_flag))
            break;
        if (SOUNDIO_ATOMIC_LOAD(sica->service_restarted)) {
            shutdown_backend(si, SoundIoErrorBackendDisconnected);
            return;
        }
        if (SOUNDIO_ATOMIC_EXCHANGE(sica->device_scan_queued, false)) {
            err = refresh_devices(si);
            if(err){
                shutdown_backend(si,err);
            }
            
            if (!SOUNDIO_ATOMIC_EXCHANGE(sica->have_devices_flag, true))
                soundio_os_cond_signal(sica->have_devices_cond, NULL);
            soundio_os_cond_signal(sica->cond, NULL);
            soundio->on_events_signal(soundio);
        }
        soundio_os_cond_wait(sica->scan_devices_cond, NULL);
    }
}

//static OSStatus on_outstream_device_overload(void *in_client_data)
//{
//    struct SoundIoOutStreamPrivate *os = (struct SoundIoOutStreamPrivate *)in_client_data;
//    struct SoundIoOutStream *outstream = &os->pub;
//    outstream->underflow_callback(outstream);
//    return noErr;
//}

static void outstream_destroy_ca(struct SoundIoPrivate *si, struct SoundIoOutStreamPrivate *os) {
    struct SoundIoOutStreamCoreAudioIOS *osca = &os->backend_data.coreaudio_ios;
//    struct SoundIoOutStream *outstream = &os->pub;
//    struct SoundIoDevice *device = outstream->device;
//    struct SoundIoDevicePrivate *dev = (struct SoundIoDevicePrivate *)device;
//    struct SoundIoDeviceCoreAudioIOS *dca = &dev->backend_data.coreaudio_ios;

    if (osca->instance) {
        AudioOutputUnitStop(osca->instance);
        AudioComponentInstanceDispose(osca->instance);
        osca->instance = NULL;
    }
}

static OSStatus write_callback_ca(void *userdata, AudioUnitRenderActionFlags *io_action_flags,
    const AudioTimeStamp *in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
    AudioBufferList *io_data)
{
    struct SoundIoOutStreamPrivate *os = (struct SoundIoOutStreamPrivate *) userdata;
    struct SoundIoOutStream *outstream = &os->pub;
    struct SoundIoOutStreamCoreAudioIOS *osca = &os->backend_data.coreaudio_ios;

    osca->io_data = io_data;
    osca->buffer_index = 0;
    osca->frames_left = in_number_frames;
    outstream->write_callback(outstream, osca->frames_left, osca->frames_left);
    osca->io_data = NULL;

    return noErr;
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

static int outstream_open_ca(struct SoundIoPrivate *si, struct SoundIoOutStreamPrivate *os) {
    struct SoundIoOutStreamCoreAudioIOS *osca = &os->backend_data.coreaudio_ios;
    struct SoundIoOutStream *outstream = &os->pub;
    struct SoundIoDevice *device = outstream->device;
    struct SoundIoDevicePrivate *dev = (struct SoundIoDevicePrivate *)device;
    struct SoundIoDeviceCoreAudioIOS *dca = &dev->backend_data.coreaudio_ios;

    if (outstream->software_latency == 0.0)
    {
        outstream->software_latency = device->software_latency_current;
    }

    outstream->software_latency = soundio_double_clamp(
            device->software_latency_min,
            outstream->software_latency,
            device->software_latency_max);

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

    // 实例音频单元
    OSStatus os_err;
    if ((os_err = AudioComponentInstanceNew(component, &osca->instance))) {
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    if ((os_err = AudioUnitInitialize(osca->instance))) {
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    AudioStreamBasicDescription format = {0};
    format.mSampleRate = outstream->sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    int err;
    if ((err = set_ca_desc(outstream->format, &format))) {
        outstream_destroy_ca(si, os);
        return err;
    }
    format.mBytesPerPacket = outstream->bytes_per_frame;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = outstream->bytes_per_frame;
    format.mChannelsPerFrame = outstream->layout.channel_count;

    if ((os_err = AudioUnitSetProperty(osca->instance, kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input, OUTPUT_ELEMENT, &format, sizeof(AudioStreamBasicDescription))))
    {
        outstream_destroy_ca(si, os);
        return SoundIoErrorIncompatibleDevice;
    }

    AURenderCallbackStruct render_callback = {write_callback_ca, os};
    if ((os_err = AudioUnitSetProperty(osca->instance, kAudioUnitProperty_SetRenderCallback,
        kAudioUnitScope_Input, OUTPUT_ELEMENT, &render_callback, sizeof(AURenderCallbackStruct))))
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

    outstream->volume = 1.0;
    osca->hardware_latency = dca->latency_frames / (double)outstream->sample_rate;

    return 0;
}

static int outstream_pause_ca(struct SoundIoPrivate *si, struct SoundIoOutStreamPrivate *os, bool pause) {
    struct SoundIoOutStreamCoreAudioIOS *osca = &os->backend_data.coreaudio_ios;
    OSStatus os_err;
    if (pause) {
        if ((os_err = AudioOutputUnitStop(osca->instance))) {
            return SoundIoErrorStreaming;
        }
    } else {
        if ((os_err = AudioOutputUnitStart(osca->instance))) {
            return SoundIoErrorStreaming;
        }
    }

    return 0;
}

static int outstream_start_ca(struct SoundIoPrivate *si, struct SoundIoOutStreamPrivate *os) {
    return outstream_pause_ca(si, os, false);
}


static int outstream_begin_write_ca(struct SoundIoPrivate *si, struct SoundIoOutStreamPrivate *os,
        struct SoundIoChannelArea **out_areas, int *frame_count)
{
    struct SoundIoOutStream *outstream = &os->pub;
    struct SoundIoOutStreamCoreAudioIOS *osca = &os->backend_data.coreaudio_ios;

    if (osca->buffer_index >= osca->io_data->mNumberBuffers)
        return SoundIoErrorInvalid;

    if (*frame_count != osca->frames_left)
        return SoundIoErrorInvalid;

    AudioBuffer *audio_buffer = &osca->io_data->mBuffers[osca->buffer_index];
    assert(audio_buffer->mNumberChannels == outstream->layout.channel_count);
    osca->write_frame_count = audio_buffer->mDataByteSize / outstream->bytes_per_frame;
    *frame_count = osca->write_frame_count;
    assert((audio_buffer->mDataByteSize % outstream->bytes_per_frame) == 0);
    for (int ch = 0; ch < outstream->layout.channel_count; ch += 1) {
        osca->areas[ch].ptr = ((char*)audio_buffer->mData) + outstream->bytes_per_sample * ch;
        osca->areas[ch].step = outstream->bytes_per_frame;
    }
    *out_areas = osca->areas;
    return 0;
}

static int outstream_end_write_ca(struct SoundIoPrivate *si, struct SoundIoOutStreamPrivate *os) {
    struct SoundIoOutStreamCoreAudioIOS *osca = &os->backend_data.coreaudio_ios;
    osca->buffer_index += 1;
    osca->frames_left -= osca->write_frame_count;
    assert(osca->frames_left >= 0);
    return 0;
}

static int outstream_clear_buffer_ca(struct SoundIoPrivate *si, struct SoundIoOutStreamPrivate *os) {
    return SoundIoErrorIncompatibleBackend;
}

static int outstream_get_latency_ca(struct SoundIoPrivate *si, struct SoundIoOutStreamPrivate *os,
        double *out_latency)
{
    struct SoundIoOutStreamCoreAudioIOS *osca = &os->backend_data.coreaudio_ios;
    *out_latency = osca->hardware_latency;
    return 0;
}

static int outstream_set_volume_ca(struct SoundIoPrivate *si, struct SoundIoOutStreamPrivate *os, float volume) {
//    struct SoundIoOutStreamCoreAudioIOS *osca = &os->backend_data.coreaudio_ios;
    struct SoundIoOutStream *outstream = &os->pub;
    outstream->volume = volume;
    return 0;
}

//static OSStatus on_instream_device_overload(void *in_client_data)
//{
//    struct SoundIoInStreamPrivate *os = (struct SoundIoInStreamPrivate *)in_client_data;
//    struct SoundIoInStream *instream = &os->pub;
//    instream->overflow_callback(instream);
//    return noErr;
//}

static void instream_destroy_ca(struct SoundIoPrivate *si, struct SoundIoInStreamPrivate *is) {
    struct SoundIoInStreamCoreAudioIOS *isca = &is->backend_data.coreaudio_ios;
//    struct SoundIoInStream *instream = &is->pub;
//    struct SoundIoDevice *device = instream->device;
//    struct SoundIoDevicePrivate *dev = (struct SoundIoDevicePrivate *)device;
//    struct SoundIoDeviceCoreAudioIOS *dca = &dev->backend_data.coreaudio_ios;

    if (isca->instance) {
        AudioOutputUnitStop(isca->instance);
        AudioComponentInstanceDispose(isca->instance);
        isca->instance = NULL;
    }

    free(isca->buffer_list);
    isca->buffer_list = NULL;
}

static OSStatus read_callback_ca(void *userdata, AudioUnitRenderActionFlags *io_action_flags,
    const AudioTimeStamp *in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
    AudioBufferList *io_data)
{
    struct SoundIoInStreamPrivate *is = (struct SoundIoInStreamPrivate *) userdata;
    struct SoundIoInStream *instream = &is->pub;
    struct SoundIoInStreamCoreAudioIOS *isca = &is->backend_data.coreaudio_ios;

    for (int i = 0; i < isca->buffer_list->mNumberBuffers; i += 1) {
        isca->buffer_list->mBuffers[i].mData = NULL;
    }

    OSStatus os_err;
    if ((os_err = AudioUnitRender(isca->instance, io_action_flags, in_time_stamp,
        in_bus_number, in_number_frames, isca->buffer_list)))
    {
        instream->error_callback(instream, SoundIoErrorStreaming);
        return noErr;
    }

    if (isca->buffer_list->mNumberBuffers == 1) {
        AudioBuffer *audio_buffer = &isca->buffer_list->mBuffers[0];
        assert(audio_buffer->mNumberChannels == instream->layout.channel_count);
        assert(audio_buffer->mDataByteSize == in_number_frames * instream->bytes_per_frame);
        for (int ch = 0; ch < instream->layout.channel_count; ch += 1) {
            isca->areas[ch].ptr = ((char*)audio_buffer->mData) + (instream->bytes_per_sample * ch);
            isca->areas[ch].step = instream->bytes_per_frame;
        }
    } else {
        assert(isca->buffer_list->mNumberBuffers == instream->layout.channel_count);
        for (int ch = 0; ch < instream->layout.channel_count; ch += 1) {
            AudioBuffer *audio_buffer = &isca->buffer_list->mBuffers[ch];
            assert(audio_buffer->mDataByteSize == in_number_frames * instream->bytes_per_sample);
            isca->areas[ch].ptr = (char*)audio_buffer->mData;
            isca->areas[ch].step = instream->bytes_per_sample;
        }
    }

    isca->frames_left = in_number_frames;
    instream->read_callback(instream, isca->frames_left, isca->frames_left);

    return noErr;
}

static int instream_open_ca(struct SoundIoPrivate *si, struct SoundIoInStreamPrivate *is) {
    struct SoundIoInStreamCoreAudioIOS *isca = &is->backend_data.coreaudio_ios;
    struct SoundIoInStream *instream = &is->pub;
    struct SoundIoDevice *device = instream->device;
    struct SoundIoDevicePrivate *dev = (struct SoundIoDevicePrivate *)device;
    struct SoundIoDeviceCoreAudioIOS *dca = &dev->backend_data.coreaudio_ios;
    UInt32 io_size;
    OSStatus os_err;

    if (instream->software_latency == 0.0)
        instream->software_latency = device->software_latency_current;

    instream->software_latency = soundio_double_clamp(
            device->software_latency_min,
            instream->software_latency,
            device->software_latency_max);

    
    AudioStreamBasicDescription input_format = {0};
        io_size = sizeof(AudioStreamBasicDescription);
        if ((os_err = AudioUnitGetProperty(isca->instance,
            kAudioUnitProperty_StreamFormat,
            kAudioUnitScope_Input,
            INPUT_ELEMENT,
            &input_format, &io_size)))
        {
            instream_destroy_ca(si, is);
            return SoundIoErrorOpeningDevice;
        }

    UInt32 num_buffers = input_format.mChannelsPerFrame;
    io_size = offsetof(AudioBufferList, mBuffers[0]) + (sizeof(AudioBuffer) * num_buffers);

    isca->buffer_list = (AudioBufferList*)ALLOCATE_NONZERO(char, io_size);
    if (!isca->buffer_list) {
        instream_destroy_ca(si, is);
        return SoundIoErrorNoMem;
    }


    AudioComponentDescription desc = {0};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent component = AudioComponentFindNext(NULL, &desc);
    if (!component) {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    if ((os_err = AudioComponentInstanceNew(component, &isca->instance))) {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    if ((os_err = AudioUnitInitialize(isca->instance))) {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    UInt32 enable_io = 1;
    if ((os_err = AudioUnitSetProperty(isca->instance, kAudioOutputUnitProperty_EnableIO,
        kAudioUnitScope_Input, INPUT_ELEMENT, &enable_io, sizeof(UInt32))))
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    enable_io = 0;
    if ((os_err = AudioUnitSetProperty(isca->instance, kAudioOutputUnitProperty_EnableIO,
        kAudioUnitScope_Output, OUTPUT_ELEMENT, &enable_io, sizeof(UInt32))))
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    AudioStreamBasicDescription format = {0};
    format.mSampleRate = instream->sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mBytesPerPacket = instream->bytes_per_frame;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = instream->bytes_per_frame;
    format.mChannelsPerFrame = instream->layout.channel_count;

    int err;
    if ((err = set_ca_desc(instream->format, &format))) {
        instream_destroy_ca(si, is);
        return err;
    }

    if ((os_err = AudioUnitSetProperty(isca->instance, kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Output, INPUT_ELEMENT, &format, sizeof(AudioStreamBasicDescription))))
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    AURenderCallbackStruct input_callback = {read_callback_ca, is};
    if ((os_err = AudioUnitSetProperty(isca->instance, kAudioOutputUnitProperty_SetInputCallback,
        kAudioUnitScope_Output, INPUT_ELEMENT, &input_callback, sizeof(AURenderCallbackStruct))))
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    isca->hardware_latency = dca->latency_frames / (double)instream->sample_rate;

    return 0;
}

static int instream_pause_ca(struct SoundIoPrivate *si, struct SoundIoInStreamPrivate *is, bool pause) {
    struct SoundIoInStreamCoreAudioIOS *isca = &is->backend_data.coreaudio_ios;
    OSStatus os_err;
    if (pause) {
        if ((os_err = AudioOutputUnitStop(isca->instance))) {
            return SoundIoErrorStreaming;
        }
    } else {
        if ((os_err = AudioOutputUnitStart(isca->instance))) {
            return SoundIoErrorStreaming;
        }
    }

    return 0;
}

static int instream_start_ca(struct SoundIoPrivate *si, struct SoundIoInStreamPrivate *is) {
    return instream_pause_ca(si, is, false);
}

static int instream_begin_read_ca(struct SoundIoPrivate *si, struct SoundIoInStreamPrivate *is,
        struct SoundIoChannelArea **out_areas, int *frame_count)
{
    struct SoundIoInStreamCoreAudioIOS *isca = &is->backend_data.coreaudio_ios;

    if (*frame_count != isca->frames_left)
        return SoundIoErrorInvalid;

    *out_areas = isca->areas;

    return 0;
}

static int instream_end_read_ca(struct SoundIoPrivate *si, struct SoundIoInStreamPrivate *is) {
    struct SoundIoInStreamCoreAudioIOS *isca = &is->backend_data.coreaudio_ios;
    isca->frames_left = 0;
    return 0;
}

static int instream_get_latency_ca(struct SoundIoPrivate *si, struct SoundIoInStreamPrivate *is,
        double *out_latency)
{
    struct SoundIoInStreamCoreAudioIOS *isca = &is->backend_data.coreaudio_ios;
    *out_latency = isca->hardware_latency;
    return 0;
}

int soundio_coreaudio_init(struct SoundIoPrivate *si) {
    struct SoundIoCoreAudioIOS *sica = &si->backend_data.coreaudio_ios;
    int err;

    SOUNDIO_ATOMIC_STORE(sica->have_devices_flag, false);
    SOUNDIO_ATOMIC_STORE(sica->device_scan_queued, true);
    SOUNDIO_ATOMIC_STORE(sica->service_restarted, false);
    SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(sica->abort_flag);
    
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
    
    
    sica->mutex = soundio_os_mutex_create();
    if (!sica->mutex) {
        destroy_ca(si);
        return SoundIoErrorNoMem;
    }

    sica->cond = soundio_os_cond_create();
    if (!sica->cond) {
        destroy_ca(si);
        return SoundIoErrorNoMem;
    }

    sica->have_devices_cond = soundio_os_cond_create();
    if (!sica->have_devices_cond) {
        destroy_ca(si);
        return SoundIoErrorNoMem;
    }

    sica->scan_devices_cond = soundio_os_cond_create();
    if (!sica->scan_devices_cond) {
        destroy_ca(si);
        return SoundIoErrorNoMem;
    }

    if ((err = soundio_os_thread_create(device_thread_run, si, NULL, &sica->thread))) {
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
