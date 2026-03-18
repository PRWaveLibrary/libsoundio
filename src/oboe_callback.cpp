//
// Created by Administrator on 2026/3/9.
//

#include "oboe_callback.h"

#include "soundio_private.h"
#include "oboe/AudioStreamBuilder.h"


oboe::DataCallbackResult oboe_callback::onAudioReady(oboe::AudioStream* audioStream, void* audioData, int32_t numFrames)
{
    std::shared_ptr<SoundIoOutStreamPrivate> os = outstream_private.lock();
    if (os == nullptr)
    {
        return oboe::DataCallbackResult::Stop;
    }

    struct SoundIoOutStreamOboe* osca = &os->backend_data.oboe;

    osca->request_num_frames = numFrames;
    osca->request_audio_data = static_cast<float *>(audioData);
    os->write_callback(os, osca->request_num_frames, osca->request_num_frames);

    return oboe::DataCallbackResult::Continue;
}

void oboe_stream_error_callback::onErrorAfterClose(oboe::AudioStream* audioStream, oboe::Result error)
{
    if (error != oboe::Result::ErrorDisconnected)
    {
        return;
    }

    std::shared_ptr<SoundIoPrivate> si = si_.lock();
    if (si == nullptr)
    {
        printf("os is nullptr!");
        return;
    }

    LOGI("device change.");
    soundio_force_device_scan(si);
}
