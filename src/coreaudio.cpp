/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "coreaudio.h"
#include "soundio_private.h"

#include <assert.h>

static const int OUTPUT_ELEMENT = 0;
static const int INPUT_ELEMENT = 1;

#define ERROR_LOG(msg) \
printf("%s:%d: [ERROR] %s\n", __FILE__, __LINE__, msg)

static AudioObjectPropertyAddress device_listen_props[] = {
    {kAudioDevicePropertyDeviceHasChanged, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain},
    {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyStreamConfiguration, kAudioObjectPropertyScopeInput, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyStreamConfiguration, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyPreferredChannelLayout, kAudioObjectPropertyScopeInput, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyPreferredChannelLayout, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeInput, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyAvailableNominalSampleRates, kAudioObjectPropertyScopeInput, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyAvailableNominalSampleRates, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeInput, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeInput, kAudioObjectPropertyElementMain},
    {kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain},
};

static enum SoundIoDeviceAim aims[] =
{
    SoundIoDeviceAimInput,
    SoundIoDeviceAimOutput,
};

OSStatus CoreAudioCallback::devices_changed(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[])
{
    LOGI("on devices changed %d", in_object_id);
    std::shared_ptr<SoundIoPrivate> s = si.lock();
    if (s == nullptr)
    {
        return noErr;
    }

    soundio_force_device_scan(s);
    return noErr;
}

OSStatus CoreAudioCallback::service_restarted(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[])
{
    std::shared_ptr<SoundIoPrivate> s = si.lock();
    if (s == nullptr)
    {
        return noErr;
    }
    SoundIoCoreAudio& sica = s->backend_data->coreaudio;

    SOUNDIO_ATOMIC_STORE(sica.service_restarted, true);
    sica.scan_devices_cond->signal(nullptr);

    return noErr;
}

OSStatus CoreAudioCallback::outstream_device_overload(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[])
{
    std::shared_ptr<SoundIoOutStreamPrivate> os = out_stream.lock();
    if (os == nullptr)
    {
        return noErr;
    }
    os->underflow_callback(os);
    return noErr;
}

void CoreAudioCallback::unsubscribe_device_listeners() const
{
    std::shared_ptr<SoundIoPrivate> s = si.lock();
    if (s == nullptr)
    {
        return;
    }

    SoundIoCoreAudio& sica = s->backend_data->coreaudio;

    for (AudioDeviceID device_id: sica.registered_listeners)
    {
        for (auto& device_listen_prop: device_listen_props)
        {
            OSStatus os_err = AudioObjectRemovePropertyListener(device_id, &device_listen_prop, on_devices_changed, s->backend_data->coreaudio.callback.get());
            if (os_err != noErr)
            {
                ERROR_LOG(std::to_string(os_err).c_str());
            }
        }
    }
    sica.registered_listeners.clear();
}

OSStatus CoreAudioCallback::on_devices_changed(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[], void* in_client_data)
{
    CoreAudioCallback* callback = static_cast<CoreAudioCallback *>(in_client_data);
    return callback->devices_changed(in_object_id, in_number_addresses, in_addresses);
}

OSStatus CoreAudioCallback::on_service_restarted(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[], void* in_client_data)
{
    CoreAudioCallback* callback = static_cast<CoreAudioCallback *>(in_client_data);
    return callback->service_restarted(in_object_id, in_number_addresses, in_addresses);
}

OSStatus CoreAudioCallback::on_outstream_device_overload(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[],
                                                         void* in_client_data)
{
    CoreAudioCallback* callback = static_cast<CoreAudioCallback *>(in_client_data);
    return callback->outstream_device_overload(in_object_id, in_number_addresses, in_addresses);
}

OSStatus CoreAudioCallback::on_instream_device_overload(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[],
                                                        void* in_client_data)
{
    CoreAudioCallback* callback = static_cast<CoreAudioCallback *>(in_client_data);
    return callback->instream_device_overload(in_object_id, in_number_addresses, in_addresses);
}

OSStatus CoreAudioCallback::write_callback(void* userdata, AudioUnitRenderActionFlags* io_action_flags, const AudioTimeStamp* in_time_stamp, UInt32 in_bus_number,
                                           UInt32 in_number_frames, AudioBufferList* io_data)
{
    CoreAudioCallback* callback = static_cast<CoreAudioCallback *>(userdata);
    return callback->write_callback_ca(io_action_flags, in_time_stamp, in_bus_number, in_number_frames, io_data);
}

void CoreAudioCallback::unsubscribe_device_listeners(std::shared_ptr<SoundIoPrivate> si)
{
    si->backend_data->coreaudio.callback->unsubscribe_device_listeners();
}


// Possible errors:
//  * SoundIoErrorNoMem
//  * SoundIoErrorEncodingString
static std::wstring from_cf_string(CFStringRef string_ref)
{
    if (!string_ref)
    {
        return L"";
    }


    CFIndex length = CFStringGetLength(string_ref);
    if (length == 0)
    {
        return L"";
    }

    CFRange range = CFRangeMake(0, length);
    CFIndex usedBufLen = 0;
    CFStringEncoding encoding = CFByteOrderGetCurrent() == CFByteOrderLittleEndian ? kCFStringEncodingUTF32LE : kCFStringEncodingUTF32BE;

    CFStringGetBytes(string_ref, range, encoding, '?', false, nullptr, 0, &usedBufLen);

    if (usedBufLen == 0)
    {
        return L"";
    }

    std::wstring wstr(usedBufLen / sizeof(wchar_t), L'\0');
    CFStringGetBytes(string_ref, range, encoding, '?', false, reinterpret_cast<UInt8 *>(&wstr[0]), usedBufLen, nullptr);
    return wstr;
}

static int aim_to_scope(enum SoundIoDeviceAim aim)
{
    return (aim == SoundIoDeviceAimInput) ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;
}

static enum SoundIoChannelId from_channel_descr(const AudioChannelDescription* descr)
{
    switch (descr->mChannelLabel)
    {
        default:
            return SoundIoChannelIdInvalid;
        case kAudioChannelLabel_Left:
            return SoundIoChannelIdFrontLeft;
        case kAudioChannelLabel_Right:
            return SoundIoChannelIdFrontRight;
        case kAudioChannelLabel_Center:
            return SoundIoChannelIdFrontCenter;
        case kAudioChannelLabel_LFEScreen:
            return SoundIoChannelIdLfe;
        case kAudioChannelLabel_LeftSurround:
            return SoundIoChannelIdBackLeft;
        case kAudioChannelLabel_RightSurround:
            return SoundIoChannelIdBackRight;
        case kAudioChannelLabel_LeftCenter:
            return SoundIoChannelIdFrontLeftCenter;
        case kAudioChannelLabel_RightCenter:
            return SoundIoChannelIdFrontRightCenter;
        case kAudioChannelLabel_CenterSurround:
            return SoundIoChannelIdBackCenter;
        case kAudioChannelLabel_LeftSurroundDirect:
            return SoundIoChannelIdSideLeft;
        case kAudioChannelLabel_RightSurroundDirect:
            return SoundIoChannelIdSideRight;
        case kAudioChannelLabel_TopCenterSurround:
            return SoundIoChannelIdTopCenter;
        case kAudioChannelLabel_VerticalHeightLeft:
            return SoundIoChannelIdTopFrontLeft;
        case kAudioChannelLabel_VerticalHeightCenter:
            return SoundIoChannelIdTopFrontCenter;
        case kAudioChannelLabel_VerticalHeightRight:
            return SoundIoChannelIdTopFrontRight;
        case kAudioChannelLabel_TopBackLeft:
            return SoundIoChannelIdTopBackLeft;
        case kAudioChannelLabel_TopBackCenter:
            return SoundIoChannelIdTopBackCenter;
        case kAudioChannelLabel_TopBackRight:
            return SoundIoChannelIdTopBackRight;
        case kAudioChannelLabel_RearSurroundLeft:
            return SoundIoChannelIdBackLeft;
        case kAudioChannelLabel_RearSurroundRight:
            return SoundIoChannelIdBackRight;
        case kAudioChannelLabel_LeftWide:
            return SoundIoChannelIdFrontLeftWide;
        case kAudioChannelLabel_RightWide:
            return SoundIoChannelIdFrontRightWide;
        case kAudioChannelLabel_LFE2:
            return SoundIoChannelIdLfe2;
        case kAudioChannelLabel_LeftTotal:
            return SoundIoChannelIdFrontLeft;
        case kAudioChannelLabel_RightTotal:
            return SoundIoChannelIdFrontRight;
        case kAudioChannelLabel_HearingImpaired:
            return SoundIoChannelIdHearingImpaired;
        case kAudioChannelLabel_Narration:
            return SoundIoChannelIdNarration;
        case kAudioChannelLabel_Mono:
            return SoundIoChannelIdFrontCenter;
        case kAudioChannelLabel_DialogCentricMix:
            return SoundIoChannelIdDialogCentricMix;
        case kAudioChannelLabel_CenterSurroundDirect:
            return SoundIoChannelIdBackCenter;
        case kAudioChannelLabel_Haptic:
            return SoundIoChannelIdHaptic;

        case kAudioChannelLabel_Ambisonic_W:
            return SoundIoChannelIdAmbisonicW;
        case kAudioChannelLabel_Ambisonic_X:
            return SoundIoChannelIdAmbisonicX;
        case kAudioChannelLabel_Ambisonic_Y:
            return SoundIoChannelIdAmbisonicY;
        case kAudioChannelLabel_Ambisonic_Z:
            return SoundIoChannelIdAmbisonicZ;

        case kAudioChannelLabel_MS_Mid:
            return SoundIoChannelIdMsMid;
        case kAudioChannelLabel_MS_Side:
            return SoundIoChannelIdMsSide;

        case kAudioChannelLabel_XY_X:
            return SoundIoChannelIdXyX;
        case kAudioChannelLabel_XY_Y:
            return SoundIoChannelIdXyY;

        case kAudioChannelLabel_HeadphonesLeft:
            return SoundIoChannelIdHeadphonesLeft;
        case kAudioChannelLabel_HeadphonesRight:
            return SoundIoChannelIdHeadphonesRight;
        case kAudioChannelLabel_ClickTrack:
            return SoundIoChannelIdClickTrack;
        case kAudioChannelLabel_ForeignLanguage:
            return SoundIoChannelIdForeignLanguage;

        case kAudioChannelLabel_Discrete:
            return SoundIoChannelIdAux;

        case kAudioChannelLabel_Discrete_0:
            return SoundIoChannelIdAux0;
        case kAudioChannelLabel_Discrete_1:
            return SoundIoChannelIdAux1;
        case kAudioChannelLabel_Discrete_2:
            return SoundIoChannelIdAux2;
        case kAudioChannelLabel_Discrete_3:
            return SoundIoChannelIdAux3;
        case kAudioChannelLabel_Discrete_4:
            return SoundIoChannelIdAux4;
        case kAudioChannelLabel_Discrete_5:
            return SoundIoChannelIdAux5;
        case kAudioChannelLabel_Discrete_6:
            return SoundIoChannelIdAux6;
        case kAudioChannelLabel_Discrete_7:
            return SoundIoChannelIdAux7;
        case kAudioChannelLabel_Discrete_8:
            return SoundIoChannelIdAux8;
        case kAudioChannelLabel_Discrete_9:
            return SoundIoChannelIdAux9;
        case kAudioChannelLabel_Discrete_10:
            return SoundIoChannelIdAux10;
        case kAudioChannelLabel_Discrete_11:
            return SoundIoChannelIdAux11;
        case kAudioChannelLabel_Discrete_12:
            return SoundIoChannelIdAux12;
        case kAudioChannelLabel_Discrete_13:
            return SoundIoChannelIdAux13;
        case kAudioChannelLabel_Discrete_14:
            return SoundIoChannelIdAux14;
        case kAudioChannelLabel_Discrete_15:
            return SoundIoChannelIdAux15;
    }
}

// See https://developer.apple.com/library/mac/documentation/MusicAudio/Reference/CoreAudioDataTypesRef/#//apple_ref/doc/constant_group/Audio_Channel_Layout_Tags
// Possible Errors:
// * SoundIoErrorIncompatibleDevice
// This does not handle all the possible layout enum values and it does not
// handle channel bitmaps.
static int from_coreaudio_layout(const AudioChannelLayout* ca_layout, struct SoundIoChannelLayout* layout)
{
    switch (ca_layout->mChannelLayoutTag)
    {
        case kAudioChannelLayoutTag_UseChannelDescriptions:
        {
            layout->channel_count = soundio_int_min(SOUNDIO_MAX_CHANNELS, static_cast<int32_t>(ca_layout->mNumberChannelDescriptions));
            for (int i = 0; i < layout->channel_count; i += 1)
            {
                layout->channels[i] = from_channel_descr(&ca_layout->mChannelDescriptions[i]);
            }
            break;
        }
        case kAudioChannelLayoutTag_UseChannelBitmap:
        {
            return SoundIoErrorIncompatibleDevice;
        }
        case kAudioChannelLayoutTag_Mono:
        {
            layout->channel_count = 1;
            layout->channels[0] = SoundIoChannelIdFrontCenter;
            break;
        }
        case kAudioChannelLayoutTag_Stereo:
        case kAudioChannelLayoutTag_StereoHeadphones:
        case kAudioChannelLayoutTag_MatrixStereo:
        case kAudioChannelLayoutTag_Binaural:
        {
            layout->channel_count = 2;
            layout->channels[0] = SoundIoChannelIdFrontLeft;
            layout->channels[1] = SoundIoChannelIdFrontRight;
            break;
        }
        case kAudioChannelLayoutTag_XY:
        {
            layout->channel_count = 2;
            layout->channels[0] = SoundIoChannelIdXyX;
            layout->channels[1] = SoundIoChannelIdXyY;
            break;
        }
        case kAudioChannelLayoutTag_MidSide:
        {
            layout->channel_count = 2;
            layout->channels[0] = SoundIoChannelIdMsMid;
            layout->channels[1] = SoundIoChannelIdMsSide;
            break;
        }
        case kAudioChannelLayoutTag_Ambisonic_B_Format:
        {
            layout->channel_count = 4;
            layout->channels[0] = SoundIoChannelIdAmbisonicW;
            layout->channels[1] = SoundIoChannelIdAmbisonicX;
            layout->channels[2] = SoundIoChannelIdAmbisonicY;
            layout->channels[3] = SoundIoChannelIdAmbisonicZ;
            break;
        }
        case kAudioChannelLayoutTag_Quadraphonic:
        {
            layout->channel_count = 4;
            layout->channels[0] = SoundIoChannelIdFrontLeft;
            layout->channels[1] = SoundIoChannelIdFrontRight;
            layout->channels[2] = SoundIoChannelIdBackLeft;
            layout->channels[3] = SoundIoChannelIdBackRight;
            break;
        }
        case kAudioChannelLayoutTag_Pentagonal:
        {
            layout->channel_count = 5;
            layout->channels[0] = SoundIoChannelIdSideLeft;
            layout->channels[1] = SoundIoChannelIdSideRight;
            layout->channels[2] = SoundIoChannelIdBackLeft;
            layout->channels[3] = SoundIoChannelIdBackRight;
            layout->channels[4] = SoundIoChannelIdFrontCenter;
            break;
        }
        case kAudioChannelLayoutTag_Hexagonal:
        {
            layout->channel_count = 6;
            layout->channels[0] = SoundIoChannelIdFrontLeft;
            layout->channels[1] = SoundIoChannelIdFrontRight;
            layout->channels[2] = SoundIoChannelIdBackLeft;
            layout->channels[3] = SoundIoChannelIdBackRight;
            layout->channels[4] = SoundIoChannelIdFrontCenter;
            layout->channels[5] = SoundIoChannelIdBackCenter;
            break;
        }
        case kAudioChannelLayoutTag_Octagonal:
        {
            layout->channel_count = 8;
            layout->channels[0] = SoundIoChannelIdFrontLeft;
            layout->channels[1] = SoundIoChannelIdFrontRight;
            layout->channels[2] = SoundIoChannelIdBackLeft;
            layout->channels[3] = SoundIoChannelIdBackRight;
            layout->channels[4] = SoundIoChannelIdFrontCenter;
            layout->channels[5] = SoundIoChannelIdBackCenter;
            layout->channels[6] = SoundIoChannelIdSideLeft;
            layout->channels[7] = SoundIoChannelIdSideRight;
            break;
        }
        case kAudioChannelLayoutTag_Cube:
        {
            layout->channel_count = 8;
            layout->channels[0] = SoundIoChannelIdFrontLeft;
            layout->channels[1] = SoundIoChannelIdFrontRight;
            layout->channels[2] = SoundIoChannelIdBackLeft;
            layout->channels[3] = SoundIoChannelIdBackRight;
            layout->channels[4] = SoundIoChannelIdTopFrontLeft;
            layout->channels[5] = SoundIoChannelIdTopFrontRight;
            layout->channels[6] = SoundIoChannelIdTopBackLeft;
            layout->channels[7] = SoundIoChannelIdTopBackRight;
            break;
        }
        default:
        {
            return SoundIoErrorIncompatibleDevice;
        }
    }
    soundio_channel_layout_detect_builtin(layout);
    return 0;
}

static bool all_channels_invalid(const struct SoundIoChannelLayout* layout)
{
    for (int i = 0; i < layout->channel_count; i += 1)
    {
        if (layout->channels[i] != SoundIoChannelIdInvalid)
            return false;
    }
    return true;
}

struct RefreshDevices
{
    std::shared_ptr<SoundIoPrivate> si;
    std::unique_ptr<SoundIoDevicesInfo> devices_info;
    UInt32 devices_size;
    std::vector<AudioObjectID> device_ids;
    std::wstring device_name;
    std::shared_ptr<SoundIoDevice> device;
    std::wstring device_uid;
    bool ok;
};

static void deinit_refresh_devices(struct RefreshDevices* rd)
{
    if (!rd->ok)
    {
        CoreAudioCallback::unsubscribe_device_listeners(rd->si);
    }
    rd->devices_info = nullptr;
    rd->device_name.clear();
    rd->device = nullptr;
    rd->device_uid.clear();
}

static int refresh_devices(std::shared_ptr<SoundIoPrivate> si)
{
    std::shared_ptr<SoundIo> soundio = si;
    struct SoundIoCoreAudio& sica = si->backend_data->coreaudio;

    UInt32 io_size = 0;
    OSStatus os_err = 0;
    int err = 0;

    CoreAudioCallback::unsubscribe_device_listeners(si);

    RefreshDevices rd = {};
    rd.si = si;

    rd.devices_info = std::make_unique<SoundIoDevicesInfo>();

    AudioObjectPropertyAddress prop_address = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    os_err = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &prop_address, 0, nullptr, &io_size);
    if (os_err != noErr)
    {
        deinit_refresh_devices(&rd);
        return SoundIoErrorOpeningDevice;
    }

    AudioObjectID default_input_id;
    AudioObjectID default_output_id;

    UInt32 device_count = io_size / sizeof(AudioObjectID);

    rd.device_ids.clear();
    // get default device
    if (device_count > 0)
    {
        rd.devices_size = io_size;
        auto* system_devices = new AudioObjectID[device_count];
        os_err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop_address, 0, nullptr, &io_size, system_devices);
        if (os_err != noErr)
        {
            delete[] system_devices;
            system_devices = nullptr;
            deinit_refresh_devices(&rd);
            return SoundIoErrorOpeningDevice;
        }

        for (UInt32 i = 0; i < device_count; i += 1)
        {
            rd.device_ids.push_back(system_devices[i]);
        }


        delete[] system_devices;
        system_devices = nullptr;

        io_size = sizeof(AudioObjectID);
        prop_address.mSelector = kAudioHardwarePropertyDefaultInputDevice;
        os_err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop_address, 0, nullptr, &io_size, &default_input_id);
        if (os_err != noErr)
        {
            deinit_refresh_devices(&rd);
            return SoundIoErrorOpeningDevice;
        }

        io_size = sizeof(AudioObjectID);
        prop_address.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
        os_err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &prop_address, 0, nullptr, &io_size, &default_output_id);
        if (os_err != noErr)
        {
            deinit_refresh_devices(&rd);
            return SoundIoErrorOpeningDevice;
        }
    }

    for (int device_i = 0; device_i < device_count; ++device_i)
    {
        AudioObjectID device_id = rd.device_ids[device_i];

        for (int i = 0; i < std::size(device_listen_props); i += 1)
        {
            os_err = AudioObjectAddPropertyListener(device_id, &device_listen_props[i], CoreAudioCallback::on_devices_changed, si->backend_data->coreaudio.callback.get());
            if (os_err != noErr)
            {
                deinit_refresh_devices(&rd);
                return SoundIoErrorOpeningDevice;
            }
        }

        sica.registered_listeners.push_back(device_id);
        prop_address.mSelector = kAudioObjectPropertyName;
        prop_address.mScope = kAudioObjectPropertyScopeGlobal;
        prop_address.mElement = kAudioObjectPropertyElementMain;
        io_size = sizeof(CFStringRef);
        CFStringRef buf;

        os_err = AudioObjectGetPropertyData(device_id, &prop_address, 0, nullptr, &io_size, &buf);
        if (os_err != noErr)
        {
            deinit_refresh_devices(&rd);
            return SoundIoErrorOpeningDevice;
        }

        rd.device_name = from_cf_string(buf);
        CFRelease(buf);
        buf = nullptr;

        prop_address.mSelector = kAudioDevicePropertyDeviceUID;
        prop_address.mScope = kAudioObjectPropertyScopeGlobal;
        prop_address.mElement = kAudioObjectPropertyElementMain;
        io_size = sizeof(CFStringRef);
        os_err = AudioObjectGetPropertyData(device_id, &prop_address, 0, nullptr, &io_size, &buf);
        if (os_err != noErr)
        {
            deinit_refresh_devices(&rd);
            return SoundIoErrorOpeningDevice;
        }

        rd.device_uid = from_cf_string(buf);
        CFRelease(buf);
        buf = nullptr;

        for (size_t aim_i = 0; aim_i < std::size(aims); aim_i += 1)
        {
            SoundIoDeviceAim aim = aims[aim_i];

            io_size = 0;
            prop_address.mSelector = kAudioDevicePropertyStreamConfiguration;
            prop_address.mScope = aim_to_scope(aim);
            prop_address.mElement = kAudioObjectPropertyElementMain;
            os_err = AudioObjectGetPropertyDataSize(device_id, &prop_address, 0, nullptr, &io_size);
            if (os_err != noErr)
            {
                deinit_refresh_devices(&rd);
                return SoundIoErrorOpeningDevice;
            }

            std::unique_ptr<AudioBufferList, decltype(&std::free)> buffer_list = std::unique_ptr<AudioBufferList, decltype(&std::free)>(
                static_cast<AudioBufferList *>(malloc(io_size)), std::free);
            os_err = AudioObjectGetPropertyData(device_id, &prop_address, 0, nullptr, &io_size, buffer_list.get());
            if (os_err != noErr)
            {
                buffer_list = nullptr;
                deinit_refresh_devices(&rd);
                return SoundIoErrorOpeningDevice;
            }

            int32_t channel_count = 0;
            for (int i = 0; i < buffer_list->mNumberBuffers; i++)
            {
                channel_count += static_cast<int32_t>(buffer_list->mBuffers[i].mNumberChannels);
            }

            buffer_list = nullptr;

            if (channel_count <= 0)
            {
                continue;
            }

            std::shared_ptr<SoundIoDevicePrivate> dev = std::make_shared<SoundIoDevicePrivate>();
            std::shared_ptr<SoundIoDeviceCoreAudio> dca = dev->backend_data.coreaudio;

            dca->device_id = device_id;
            assert(!rd.device);
            rd.device = dev;
            rd.device->soundio = soundio;
            rd.device->is_raw = false;
            rd.device->aim = aim;
            rd.device->id = rd.device_uid;
            rd.device->name = rd.device_name;

            prop_address.mSelector = kAudioDevicePropertyPreferredChannelLayout;
            prop_address.mScope = aim_to_scope(aim);
            prop_address.mElement = kAudioObjectPropertyElementMain;

            os_err = AudioObjectGetPropertyDataSize(device_id, &prop_address, 0, nullptr, &io_size);

            if (os_err == noErr)
            {
                std::unique_ptr<AudioChannelLayout, decltype(&std::free)> layout_buf(static_cast<AudioChannelLayout *>(std::malloc(io_size)), std::free);

                if (!layout_buf)
                {
                    deinit_refresh_devices(&rd);
                    return SoundIoErrorNoMem;
                }
                os_err = AudioObjectGetPropertyData(device_id, &prop_address, 0, nullptr, &io_size, layout_buf.get());
                if (os_err != noErr)
                {
                    deinit_refresh_devices(&rd);
                    return SoundIoErrorOpeningDevice;
                }
                err = from_coreaudio_layout(layout_buf.get(), &rd.device->current_layout);
                if (err)
                {
                    rd.device->current_layout.channel_count = channel_count;
                }
            }


            if (all_channels_invalid(&rd.device->current_layout))
            {
                const SoundIoChannelLayout* guessed_layout = soundio_channel_layout_get_default(channel_count);
                if (guessed_layout)
                {
                    rd.device->current_layout = *guessed_layout;
                }
            }

            rd.device->layouts.push_back(rd.device->current_layout);

            rd.device->formats.push_back(SoundIoFormatS16LE);
            rd.device->formats.push_back(SoundIoFormatS32LE);
            rd.device->formats.push_back(SoundIoFormatFloat32LE);
            rd.device->formats.push_back(SoundIoFormatFloat64LE);

            prop_address.mSelector = kAudioDevicePropertyNominalSampleRate;
            prop_address.mScope = aim_to_scope(aim);
            prop_address.mElement = kAudioObjectPropertyElementMain;
            io_size = sizeof(double);
            double value;
            os_err = AudioObjectGetPropertyData(device_id, &prop_address, 0, nullptr, &io_size, &value);

            if (os_err != noErr)
            {
                deinit_refresh_devices(&rd);
                return SoundIoErrorOpeningDevice;
            }

            double floored_value = floor(value);
            if (value != floored_value)
            {
                deinit_refresh_devices(&rd);
                return SoundIoErrorIncompatibleDevice;
            }

            rd.device->sample_rate_current = static_cast<int>(floored_value);

            // If you try to open an input stream with anything but the current
            // nominal sample rate, AudioUnitRender returns an error.
            if (aim == SoundIoDeviceAimInput)
            {
                SoundIoSampleRateRange range = SoundIoSampleRateRange{rd.device->sample_rate_current, rd.device->sample_rate_current};
                rd.device->sample_rates.push_back(range);
            }
            else
            {
                prop_address.mSelector = kAudioDevicePropertyAvailableNominalSampleRates;
                prop_address.mScope = aim_to_scope(aim);
                prop_address.mElement = kAudioObjectPropertyElementMain;
                os_err = AudioObjectGetPropertyDataSize(device_id, &prop_address, 0, nullptr, &io_size);

                if (os_err != noErr)
                {
                    deinit_refresh_devices(&rd);
                    return SoundIoErrorOpeningDevice;
                }

                UInt32 avr_array_len = io_size / sizeof(AudioValueRange);
                std::unique_ptr<AudioValueRange[], decltype(&std::free)> avr_buf = std::unique_ptr<AudioValueRange[], decltype(&std::free)>(
                    static_cast<AudioValueRange *>(std::malloc(io_size)), std::free);

                if (!avr_buf)
                {
                    deinit_refresh_devices(&rd);
                    return SoundIoErrorNoMem;
                }

                if ((os_err = AudioObjectGetPropertyData(device_id, &prop_address, 0, nullptr, &io_size, avr_buf.get())))
                {
                    deinit_refresh_devices(&rd);
                    return SoundIoErrorOpeningDevice;
                }

                for (int i = 0; i < avr_array_len; i += 1)
                {
                    rd.device->sample_rates.push_back(SoundIoSampleRateRange{ceil_dbl_to_int(avr_buf[i].mMinimum), static_cast<int>(avr_buf[i].mMaximum)});
                }
            }

            prop_address.mSelector = kAudioDevicePropertyBufferFrameSize;
            prop_address.mScope = aim_to_scope(aim);
            prop_address.mElement = kAudioObjectPropertyElementMain;
            io_size = sizeof(UInt32);
            UInt32 buffer_frame_size;
            if ((os_err = AudioObjectGetPropertyData(device_id, &prop_address, 0, nullptr, &io_size, &buffer_frame_size)))
            {
                deinit_refresh_devices(&rd);
                return SoundIoErrorOpeningDevice;
            }

            rd.device->software_latency_current = static_cast<double>(buffer_frame_size) / rd.device->sample_rate_current;

            prop_address.mSelector = kAudioDevicePropertyBufferFrameSizeRange;
            prop_address.mScope = aim_to_scope(aim);
            prop_address.mElement = kAudioObjectPropertyElementMain;

            io_size = sizeof(AudioValueRange);
            AudioValueRange avr;
            os_err = AudioObjectGetPropertyData(device_id, &prop_address, 0, nullptr, &io_size, &avr);
            if (os_err != noErr)
            {
                deinit_refresh_devices(&rd);
                return SoundIoErrorOpeningDevice;
            }

            rd.device->software_latency_min = avr.mMinimum / rd.device->sample_rate_current;
            rd.device->software_latency_max = avr.mMaximum / rd.device->sample_rate_current;

            prop_address.mSelector = kAudioDevicePropertyLatency;
            prop_address.mScope = aim_to_scope(aim);
            prop_address.mElement = kAudioObjectPropertyElementMain;
            io_size = sizeof(UInt32);
            if ((os_err = AudioObjectGetPropertyData(device_id, &prop_address, 0, nullptr, &io_size, &dca->latency_frames)))
            {
                deinit_refresh_devices(&rd);
                return SoundIoErrorOpeningDevice;
            }

            std::vector<std::shared_ptr<SoundIoDevice>>* device_list;
            if (rd.device->aim == SoundIoDeviceAimOutput)
            {
                device_list = &rd.devices_info->output_devices;
                if (device_id == default_output_id)
                {
                    rd.devices_info->default_output_index = static_cast<int>(device_list->size());
                }
            }
            else
            {
                assert(rd.device->aim == SoundIoDeviceAimInput);
                device_list = &rd.devices_info->input_devices;
                if (device_id == default_input_id)
                {
                    rd.devices_info->default_input_index = static_cast<int>(device_list->size());
                }
            }

            device_list->push_back(rd.device);
            rd.device = nullptr;
        }
    }

    rd.ok = true;

    std::unique_lock lock(sica.mutex->get());

    sica.ready_devices_info = std::move(rd.devices_info);
    SOUNDIO_ATOMIC_STORE(sica.have_devices_flag, true);
    sica.cond->signal(&lock);
    si->on_events_signal(si);

    deinit_refresh_devices(&rd);

    return 0;
}

static void shutdown_backend(std::shared_ptr<SoundIoPrivate> si, int err)
{
    SoundIoCoreAudio& sica = si->backend_data->coreaudio;
    std::unique_lock lock(sica.mutex->get());

    sica.shutdown_err = err;
    sica.cond->signal(&lock);
    si->on_events_signal(si);
}

static void my_flush_events(std::shared_ptr<SoundIoPrivate>& si, bool wait)
{
    SoundIoCoreAudio& sica = si->backend_data->coreaudio;

    bool change = false;
    bool cb_shutdown = false;

    std::unique_lock lock(sica.mutex->get());

    // block until have devices
    while (wait || (!SOUNDIO_ATOMIC_LOAD(sica.have_devices_flag) && !sica.shutdown_err))
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
    SoundIoCoreAudio& sica = si->backend_data->coreaudio;
    std::unique_lock lock(sica.mutex->get());
    sica.cond->signal(&lock);
}

static void force_device_scan_ca(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoCoreAudio& sica = si->backend_data->coreaudio;
    SOUNDIO_ATOMIC_STORE(sica.device_scan_queued, true);
    SOUNDIO_ATOMIC_STORE(sica.have_devices_flag, false);

    std::unique_lock scan_lock(sica.scan_devices_mutex->get());
    sica.scan_devices_cond->signal(&scan_lock);
}

static void device_thread_run(std::shared_ptr<void> arg)
{
    std::shared_ptr<SoundIoPrivate> si = std::static_pointer_cast<SoundIoPrivate>(arg);
    SoundIoCoreAudio& sica = si->backend_data->coreaudio;

    std::unique_lock lock(sica.scan_devices_mutex->get());

    while (!SOUNDIO_ATOMIC_LOAD(sica.abort_flag))
    {
        if (SOUNDIO_ATOMIC_LOAD(sica.service_restarted))
        {
            shutdown_backend(si, SoundIoErrorBackendDisconnected);
            break;
        }
        if (SOUNDIO_ATOMIC_EXCHANGE(sica.device_scan_queued, false))
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

static void outstream_destroy_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    LOGI("destroy core audio outstream");
    si->backend_data->coreaudio.callback->out_stream.reset();
    SoundIoOutStreamCoreAudio& osca = os->backend_data.coreaudio;
    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(os->device);
    auto dca = dev->backend_data.coreaudio;

    AudioObjectPropertyAddress prop_address = {kAudioDeviceProcessorOverload, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectRemovePropertyListener(dca->device_id, &prop_address, CoreAudioCallback::on_outstream_device_overload, si->backend_data->coreaudio.callback.get());

    if (osca.instance)
    {
        AudioOutputUnitStop(osca.instance);
        AudioComponentInstanceDispose(osca.instance);
        osca.instance = nullptr;
    }
}

OSStatus CoreAudioCallback::write_callback_ca(AudioUnitRenderActionFlags* io_action_flags, const AudioTimeStamp* in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
                                              AudioBufferList* io_data)
{
    std::shared_ptr<SoundIoOutStreamPrivate> os = out_stream.lock();
    if (!os)
    {
        ERROR_LOG("os is nullptr!\n");
        return noErr;
    }
    SoundIoOutStreamCoreAudio& osca = os->backend_data.coreaudio;

    osca.io_data = io_data;
    osca.buffer_index = 0;
    osca.frames_left = static_cast<int>(in_number_frames);
    os->write_callback(os, osca.frames_left, osca.frames_left);
    osca.io_data = nullptr;

    return noErr;
}

static int set_ca_desc(enum SoundIoFormat fmt, AudioStreamBasicDescription* desc)
{
    switch (fmt)
    {
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
    return SoundIoErrorNone;
}

static int outstream_open_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    LOGI("open core audio outstream");
    SoundIoOutStreamCoreAudio& osca = os->backend_data.coreaudio;
    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(os->device);
    std::shared_ptr<SoundIoDeviceCoreAudio> dca = dev->backend_data.coreaudio;
    si->backend_data->coreaudio.callback->out_stream = os;

    if (os->software_latency == 0.0)
    {
        os->software_latency = dev->software_latency_current;
    }

    os->software_latency = soundio_double_clamp(dev->software_latency_min, os->software_latency, dev->software_latency_max);

    AudioComponentDescription desc = {};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_HALOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    // 查找组件
    AudioComponent component = AudioComponentFindNext(nullptr, &desc);
    if (!component)
    {
        LOGE("AudioComponent not found.");
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    // 实例音频单元
    OSStatus os_err = AudioComponentInstanceNew(component, &osca.instance);
    if (os_err != noErr)
    {
        LOGE("AudioComponentInstanceNew failed.");
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    os_err = AudioUnitInitialize(osca.instance);
    if (os_err != noErr)
    {
        LOGE("AudioUnitInitialize failed.");
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    AudioStreamBasicDescription format = {};
    format.mSampleRate = os->sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    int err = set_ca_desc(os->format, &format);
    if (err)
    {
        LOGE("set_ca_desc failed.");
        outstream_destroy_ca(si, os);
        return err;
    }
    format.mBytesPerPacket = os->bytes_per_frame;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = os->bytes_per_frame;
    format.mChannelsPerFrame = os->layout.channel_count;
    os_err = AudioUnitSetProperty(osca.instance, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Input, OUTPUT_ELEMENT, &dca->device_id, sizeof(AudioDeviceID));
    if (os_err != noErr)
    {
        LOGE("Binding outstream id %d failed.", dca->device_id);
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }
    os_err = AudioUnitSetProperty(osca.instance, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, OUTPUT_ELEMENT, &format, sizeof(AudioStreamBasicDescription));
    if (os_err != noErr)
    {
        LOGE("AudioUnitSetProperty format failed.");
        outstream_destroy_ca(si, os);
        return SoundIoErrorIncompatibleDevice;
    }

    AURenderCallbackStruct render_callback = {CoreAudioCallback::write_callback, si->backend_data->coreaudio.callback.get()};
    os_err = AudioUnitSetProperty(osca.instance, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, OUTPUT_ELEMENT, &render_callback, sizeof(AURenderCallbackStruct));
    if (os_err != noErr)
    {
        LOGE("Binding render_callback failed.");
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    AudioObjectPropertyAddress prop_address = {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeInput, OUTPUT_ELEMENT};
    UInt32 buffer_frame_size = os->software_latency * os->sample_rate;
    os_err = AudioObjectSetPropertyData(dca->device_id, &prop_address, 0, nullptr, sizeof(UInt32), &buffer_frame_size);
    if (os_err != noErr)
    {
        LOGE("Set buffer frame size %d failed.", buffer_frame_size);
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    prop_address.mSelector = kAudioDeviceProcessorOverload;
    prop_address.mScope = kAudioObjectPropertyScopeGlobal;
    prop_address.mElement = OUTPUT_ELEMENT;
    os_err = AudioObjectAddPropertyListener(dca->device_id, &prop_address, CoreAudioCallback::on_outstream_device_overload, si->backend_data->coreaudio.callback.get());
    if (os_err != noErr)
    {
        LOGE("Binding callback overload failed.");
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }
    os_err = AudioUnitGetParameter(osca.instance, kHALOutputParam_Volume, kAudioUnitScope_Global, 0, &os->volume);
    if (os_err != noErr)
    {
        LOGE("Set volume failed.");
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    osca.hardware_latency = dca->latency_frames / static_cast<double>(os->sample_rate);
    return 0;
}

static int outstream_pause_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, bool pause)
{
    SoundIoOutStreamCoreAudio& osca = os->backend_data.coreaudio;
    OSStatus os_err;
    if (pause)
    {
        os_err = AudioOutputUnitStop(osca.instance);
        if (os_err != noErr)
        {
            return SoundIoErrorStreaming;
        }
    }
    else
    {
        os_err = AudioOutputUnitStart(osca.instance);
        if (os_err != noErr)
        {
            return SoundIoErrorStreaming;
        }
    }

    return 0;
}

static int outstream_start_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    return outstream_pause_ca(si, os, false);
}

static int outstream_begin_write_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, struct SoundIoChannelArea** out_areas, int* frame_count)
{
    SoundIoOutStreamCoreAudio& osca = os->backend_data.coreaudio;
    if (osca.buffer_index >= osca.io_data->mNumberBuffers)
    {
        return SoundIoErrorInvalid;
    }

    if (*frame_count != osca.frames_left)
    {
        return SoundIoErrorInvalid;
    }

    AudioBuffer* audio_buffer = &osca.io_data->mBuffers[osca.buffer_index];
    assert(audio_buffer->mNumberChannels == os->layout.channel_count);
    osca.write_frame_count = audio_buffer->mDataByteSize / os->bytes_per_frame;
    *frame_count = osca.write_frame_count;
    assert((audio_buffer->mDataByteSize % os->bytes_per_frame) == 0);
    for (int ch = 0; ch < os->layout.channel_count; ch += 1)
    {
        osca.areas[ch].ptr = static_cast<char *>(audio_buffer->mData) + os->bytes_per_sample * ch;
        osca.areas[ch].step = os->bytes_per_frame;
    }
    *out_areas = osca.areas;
    return 0;
}

static int outstream_end_write_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    SoundIoOutStreamCoreAudio& osca = os->backend_data.coreaudio;
    osca.buffer_index += 1;
    osca.frames_left -= osca.write_frame_count;
    assert(osca.frames_left >= 0);
    return 0;
}

static int outstream_clear_buffer_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    return SoundIoErrorIncompatibleBackend;
}

static int outstream_get_latency_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os,
                                    double* out_latency)
{
    SoundIoOutStreamCoreAudio& osca = os->backend_data.coreaudio;
    *out_latency = osca.hardware_latency;
    return 0;
}

static int outstream_set_volume_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, float volume)
{
    SoundIoOutStreamCoreAudio& osca = os->backend_data.coreaudio;
    OSStatus os_err = AudioUnitSetParameter(osca.instance, kHALOutputParam_Volume, kAudioUnitScope_Global, 0, volume, 0);
    if (os_err != noErr)
    {
        return SoundIoErrorIncompatibleDevice;
    }
    os->volume = volume;
    return 0;
}

OSStatus CoreAudioCallback::instream_device_overload(AudioObjectID in_object_id, UInt32 in_number_addresses, const AudioObjectPropertyAddress in_addresses[]) const
{
    std::shared_ptr<SoundIoInStreamPrivate> is = in_stream.lock();
    if (is == nullptr)
    {
        return noErr;
    }

    is->overflow_callback(is);
    return noErr;
}


static void instream_destroy_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    si->backend_data->coreaudio.callback->in_stream.reset();
    struct SoundIoInStreamCoreAudio& isca = is->backend_data.coreaudio;
    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(is->device);
    std::shared_ptr<SoundIoDeviceCoreAudio> dca = dev->backend_data.coreaudio;

    AudioObjectPropertyAddress prop_address = {kAudioDeviceProcessorOverload, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectRemovePropertyListener(dca->device_id, &prop_address, CoreAudioCallback::on_instream_device_overload, si->backend_data->coreaudio.callback.get());

    if (isca.instance)
    {
        AudioOutputUnitStop(isca.instance);
        AudioComponentInstanceDispose(isca.instance);
        isca.instance = nullptr;
    }

    isca.buffer_list = nullptr;
}

OSStatus CoreAudioCallback::read_callback(void* userdata, AudioUnitRenderActionFlags* io_action_flags, const AudioTimeStamp* in_time_stamp, UInt32 in_bus_number,
                                          UInt32 in_number_frames,
                                          AudioBufferList* io_data)
{
    CoreAudioCallback* callback = static_cast<CoreAudioCallback *>(userdata);
    return callback->read_callback_ca(io_action_flags, in_time_stamp, in_bus_number, in_number_frames, io_data);
}

OSStatus CoreAudioCallback::read_callback_ca(AudioUnitRenderActionFlags* io_action_flags, const AudioTimeStamp* in_time_stamp, UInt32 in_bus_number, UInt32 in_number_frames,
                                             AudioBufferList* io_data)
{
    auto is = in_stream.lock();
    if (is == nullptr)
    {
        return noErr;
    }

    SoundIoInStreamCoreAudio& isca = is->backend_data.coreaudio;

    for (int i = 0; i < isca.buffer_list->mNumberBuffers; i++)
    {
        isca.buffer_list->mBuffers[i].mData = nullptr;
    }

    OSStatus os_err = AudioUnitRender(isca.instance, io_action_flags, in_time_stamp, in_bus_number, in_number_frames, isca.buffer_list.get());
    if (os_err != noErr)
    {
        is->error_callback(is, SoundIoErrorStreaming);
        return noErr;
    }

    if (isca.buffer_list->mNumberBuffers == 1)
    {
        AudioBuffer* audio_buffer = &isca.buffer_list->mBuffers[0];
        assert(audio_buffer->mNumberChannels == is->layout.channel_count);
        assert(audio_buffer->mDataByteSize == in_number_frames * is->bytes_per_frame);
        for (int ch = 0; ch < is->layout.channel_count; ch += 1)
        {
            isca.areas[ch].ptr = static_cast<char *>(audio_buffer->mData) + (is->bytes_per_sample * ch);
            isca.areas[ch].step = is->bytes_per_frame;
        }
    }
    else
    {
        assert(isca.buffer_list->mNumberBuffers == is->layout.channel_count);
        for (int ch = 0; ch < is->layout.channel_count; ch += 1)
        {
            AudioBuffer* audio_buffer = &isca.buffer_list->mBuffers[ch];
            assert(audio_buffer->mDataByteSize == in_number_frames * is->bytes_per_sample);
            isca.areas[ch].ptr = static_cast<char *>(audio_buffer->mData);
            isca.areas[ch].step = is->bytes_per_sample;
        }
    }

    isca.frames_left = static_cast<int>(in_number_frames);
    is->read_callback(is, isca.frames_left, isca.frames_left);

    return noErr;
}

static int instream_open_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    si->backend_data->coreaudio.callback->in_stream = is;
    SoundIoInStreamCoreAudio& isca = is->backend_data.coreaudio;
    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(is->device);
    std::shared_ptr<SoundIoDeviceCoreAudio> dca = dev->backend_data.coreaudio;
    UInt32 io_size;
    OSStatus os_err;

    if (is->software_latency == 0.0)
    {
        is->software_latency = dev->software_latency_current;
    }

    is->software_latency = soundio_double_clamp(dev->software_latency_min, is->software_latency, dev->software_latency_max);


    AudioObjectPropertyAddress prop_address;
    prop_address.mSelector = kAudioDevicePropertyStreamConfiguration;
    prop_address.mScope = kAudioObjectPropertyScopeInput;
    prop_address.mElement = kAudioObjectPropertyElementMain;
    io_size = 0;
    if ((os_err = AudioObjectGetPropertyDataSize(dca->device_id, &prop_address, 0, nullptr, &io_size)))
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    isca.buffer_list = std::unique_ptr<AudioBufferList, decltype(&std::free)>(static_cast<AudioBufferList *>(malloc(io_size)), &std::free);
    if (!isca.buffer_list)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorNoMem;
    }

    if ((os_err = AudioObjectGetPropertyData(dca->device_id, &prop_address, 0, nullptr, &io_size, isca.buffer_list.get())))
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }


    AudioComponentDescription desc = {0};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_HALOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent component = AudioComponentFindNext(nullptr, &desc);
    if (!component)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    os_err = AudioComponentInstanceNew(component, &isca.instance);
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    os_err = AudioUnitInitialize(isca.instance);
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    UInt32 enable_io = 1;
    os_err = AudioUnitSetProperty(isca.instance, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, INPUT_ELEMENT, &enable_io, sizeof(UInt32));
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    enable_io = 0;
    os_err = AudioUnitSetProperty(isca.instance, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, OUTPUT_ELEMENT, &enable_io, sizeof(UInt32));
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }
    os_err = AudioUnitSetProperty(isca.instance, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Output, INPUT_ELEMENT, &dca->device_id, sizeof(AudioDeviceID));
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
    if (err != SoundIoErrorNone)
    {
        instream_destroy_ca(si, is);
        return err;
    }
    os_err = AudioUnitSetProperty(isca.instance, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, INPUT_ELEMENT, &format, sizeof(AudioStreamBasicDescription));
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    AURenderCallbackStruct input_callback = {CoreAudioCallback::read_callback, si->backend_data->coreaudio.callback.get()};
    os_err = AudioUnitSetProperty(isca.instance, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Output, INPUT_ELEMENT, &input_callback, sizeof(AURenderCallbackStruct));
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }


    prop_address.mSelector = kAudioDevicePropertyBufferFrameSize;
    prop_address.mScope = kAudioObjectPropertyScopeOutput;
    prop_address.mElement = INPUT_ELEMENT;
    UInt32 buffer_frame_size = static_cast<UInt32>(is->software_latency) * is->sample_rate;
    os_err = AudioObjectSetPropertyData(dca->device_id, &prop_address, 0, nullptr, sizeof(UInt32), &buffer_frame_size);
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    prop_address.mSelector = kAudioDeviceProcessorOverload;
    prop_address.mScope = kAudioObjectPropertyScopeGlobal;
    os_err = AudioObjectAddPropertyListener(dca->device_id, &prop_address, CoreAudioCallback::on_instream_device_overload, si->backend_data->coreaudio.callback.get());
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    isca.hardware_latency = dca->latency_frames / static_cast<double>(is->sample_rate);

    return 0;
}

static int instream_pause_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is, bool pause)
{
    SoundIoInStreamCoreAudio& isca = is->backend_data.coreaudio;
    OSStatus os_err;
    if (pause)
    {
        os_err = AudioOutputUnitStop(isca.instance);
        if (os_err != noErr)
        {
            return SoundIoErrorStreaming;
        }
    }
    else
    {
        os_err = AudioOutputUnitStart(isca.instance);
        if (os_err != noErr)
        {
            return SoundIoErrorStreaming;
        }
    }

    return 0;
}

static int instream_start_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    return instream_pause_ca(si, is, false);
}

static int instream_begin_read_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is, struct SoundIoChannelArea** out_areas, int* frame_count)
{
    SoundIoInStreamCoreAudio& isca = is->backend_data.coreaudio;

    if (*frame_count != isca.frames_left)
    {
        return SoundIoErrorInvalid;
    }

    *out_areas = isca.areas;
    return 0;
}

static int instream_end_read_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    struct SoundIoInStreamCoreAudio* isca = &is->backend_data.coreaudio;
    isca->frames_left = 0;
    return 0;
}

static int instream_get_latency_ca(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is, double* out_latency)
{
    SoundIoInStreamCoreAudio& isca = is->backend_data.coreaudio;
    *out_latency = isca.hardware_latency;
    return 0;
}


static void destroy_core_audio(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoCoreAudio& sica = si->backend_data->coreaudio;

    soundio_outstream_destroy(si->outstream);

    AudioObjectPropertyAddress prop_address = {kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &prop_address, CoreAudioCallback::on_devices_changed, si->backend_data->coreaudio.callback.get());

    prop_address.mSelector = kAudioHardwarePropertyDevices;
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &prop_address, CoreAudioCallback::on_devices_changed, si->backend_data->coreaudio.callback.get());

    prop_address.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &prop_address, CoreAudioCallback::on_devices_changed, si->backend_data->coreaudio.callback.get());


    prop_address.mSelector = kAudioHardwarePropertyServiceRestarted;
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &prop_address, CoreAudioCallback::on_service_restarted, si->backend_data->coreaudio.callback.get());

    CoreAudioCallback::unsubscribe_device_listeners(si);

    if (sica.thread)
    {
        std::unique_lock lock(sica.scan_devices_mutex->get());
        SOUNDIO_ATOMIC_STORE(sica.abort_flag, true);
        sica.scan_devices_cond->signal(&lock);
        lock.unlock();
        sica.thread = nullptr;
    }

    sica.mutex = nullptr;
    sica.cond = nullptr;

    sica.scan_devices_mutex = nullptr;
    sica.scan_devices_cond = nullptr;

    sica.ready_devices_info = nullptr;
}

int soundio_coreaudio_init(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoCoreAudio& sica = si->backend_data->coreaudio;
    sica.callback->si = si;
    int err;

    SOUNDIO_ATOMIC_STORE(sica.have_devices_flag, false);
    SOUNDIO_ATOMIC_STORE(sica.device_scan_queued, true);
    SOUNDIO_ATOMIC_STORE(sica.service_restarted, false);
    SOUNDIO_ATOMIC_STORE(sica.abort_flag, false);

    sica.mutex = soundio_os_mutex_create();
    if (!sica.mutex)
    {
        destroy_core_audio(si);
        return SoundIoErrorNoMem;
    }

    sica.cond = soundio_os_cond_create();
    if (!sica.cond)
    {
        destroy_core_audio(si);
        return SoundIoErrorNoMem;
    }

    sica.scan_devices_mutex = soundio_os_mutex_create();
    if (!sica.scan_devices_mutex)
    {
        destroy_core_audio(si);
        return SoundIoErrorNoMem;
    }

    sica.scan_devices_cond = soundio_os_cond_create();
    if (!sica.scan_devices_cond)
    {
        destroy_core_audio(si);
        return SoundIoErrorNoMem;
    }

    AudioObjectPropertyAddress prop_address = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    err = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &prop_address, CoreAudioCallback::on_devices_changed, si->backend_data->coreaudio.callback.get());
    if (err != noErr)
    {
        destroy_core_audio(si);
        return SoundIoErrorSystemResources;
    }

    prop_address.mSelector = kAudioHardwarePropertyDefaultInputDevice;
    err = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &prop_address, CoreAudioCallback::on_devices_changed, si->backend_data->coreaudio.callback.get());
    if (err != noErr)
    {
        destroy_core_audio(si);
        return SoundIoErrorSystemResources;
    }

    prop_address.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    err = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &prop_address, CoreAudioCallback::on_devices_changed, si->backend_data->coreaudio.callback.get());
    if (err != noErr)
    {
        destroy_core_audio(si);
        return SoundIoErrorSystemResources;
    }

    prop_address.mSelector = kAudioHardwarePropertyServiceRestarted;
    err = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &prop_address, CoreAudioCallback::on_service_restarted, si->backend_data->coreaudio.callback.get());
    if (err != noErr)
    {
        destroy_core_audio(si);
        return SoundIoErrorSystemResources;
    }

    sica.thread = SoundIoOsThread::create(device_thread_run, si);

    si->destroy = destroy_core_audio;
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
