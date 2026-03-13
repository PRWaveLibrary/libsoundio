//
// Created by Administrator on 2026/3/9.
//

#ifndef AUDIORENDERER_OBOE_CALLBACK_H
#define AUDIORENDERER_OBOE_CALLBACK_H
#include <memory>
#include "oboe/AudioStreamCallback.h"

struct SoundIoOutStreamPrivate;


struct oboe_callback : oboe::AudioStreamDataCallback
{
    oboe_callback(std::shared_ptr<SoundIoOutStreamPrivate> out_stream_private)
    {
        outstream_private = out_stream_private;
    }

    virtual ~oboe_callback() = default;

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* audioStream, void* audioData, int32_t numFrames) override;

private:
    std::weak_ptr<SoundIoOutStreamPrivate> outstream_private;
};

struct SoundIoDeviceOboe
{
    uint32_t latency_frames;
};

#endif //AUDIORENDERER_OBOE_CALLBACK_H
