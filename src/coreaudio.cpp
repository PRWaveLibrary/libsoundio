/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "coreaudio.h"
#include "soundio_private.h"

#include <cassert>
#include <ranges>

#include "util.h"
#include "util/logger.hpp"
#include "util/malloc_util.h"

static constexpr int OUTPUT_ELEMENT = 0;
static constexpr int INPUT_ELEMENT = 1;

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

/**
 * @brief 处理设备层级变动的 CoreAudio 底层通知 hook
 *
 * refCon 是 SoundIoPrivate 裸指针，需要 shared_ptr 时代替：weak_from_this().lock()
 * （SoundIo 已继承 enable_shared_from_this）。
 */
OSStatus CoreAudioCallback::on_devices_changed(const AudioObjectID in_object_id, const std::uint32_t in_number_addresses, const AudioObjectPropertyAddress in_addresses[], void* in_client_data) // NOLINT
{
    LOGI(L"on devices changed %u", in_object_id);
    auto* s_raw = static_cast<SoundIoPrivate*>(in_client_data);
    if (s_raw == nullptr)
    {
        return noErr;
    }

    const auto s = std::dynamic_pointer_cast<SoundIoPrivate>(s_raw->weak_from_this().lock());
    if (s == nullptr || s->force_device_scan == nullptr)
    {
        return noErr;
    }

    s->force_device_scan(s);
    return noErr;
}

/**
 * @brief 响应音频系统服务重启的回调
 *
 * 通常在系统层级 daemon crash 并自动拉起时被调用。
 */
OSStatus CoreAudioCallback::on_service_restarted(const AudioObjectID in_object_id, const std::uint32_t in_number_addresses, const AudioObjectPropertyAddress in_addresses[], void* in_client_data) // NOLINT
{
    auto* s = static_cast<SoundIoPrivate*>(in_client_data);
    if (s == nullptr)
    {
        return noErr;
    }

    auto& sica = s->backend_data->coreaudio;
    sica.service_restarted.store(true);
    sica.scan_devices_cond->signal(nullptr);

    return noErr;
}

/**
 * @brief 注销先前订阅的设备级别的 system properties listeners
 */
void CoreAudioCallback::unsubscribe_device_listeners(const std::shared_ptr<SoundIoPrivate>& si)
{
    auto& sica = si->backend_data->coreaudio;

    for (AudioDeviceID device_id: sica.registered_listeners)
    {
        for (auto& device_listen_prop: device_listen_props)
        {
            const OSStatus os_err = AudioObjectRemovePropertyListener(device_id, &device_listen_prop, on_devices_changed, si.get());
            if (os_err != noErr)
            {
                LOGE(L"unsubscribe_device_listeners err: %d", os_err);
            }
        }
    }
    sica.registered_listeners.clear();
}

/**
 * @brief 处理由于 hardware 负担过重引起的 stream 超载信息回调
 */
OSStatus CoreAudioCallback::on_outstream_device_overload(const AudioObjectID in_object_id, const std::uint32_t in_number_addresses, const AudioObjectPropertyAddress in_addresses[], void* in_client_data) // NOLINT
{
    auto* os = static_cast<SoundIoOutStreamPrivate*>(in_client_data);
    if (os == nullptr)
    {
        return noErr;
    }
    const auto os_sp = os->weak_from_this().lock();
    if (!os_sp)
    {
        return noErr;
    }
    os_sp->underflow_callback(os_sp);
    return noErr;
}

/**
 * @brief 处理由于 hardware 负担过重引起的 instream 超载信息回调
 */
OSStatus CoreAudioCallback::on_instream_device_overload(const AudioObjectID in_object_id, const std::uint32_t in_number_addresses, const AudioObjectPropertyAddress in_addresses[], void* in_client_data) // NOLINT
{
    auto* is = static_cast<SoundIoInStreamPrivate*>(in_client_data);
    if (is == nullptr)
    {
        return noErr;
    }
    const auto is_sp = is->weak_from_this().lock();
    if (!is_sp)
    {
        return noErr;
    }
    is_sp->overflow_callback(is_sp);
    return noErr;
}

/**
 * @brief CoreAudio AudioUnit 的渲染钩子：拿回 stream 的 shared_ptr，把框架层缓冲映射交由 user callback
 */
OSStatus CoreAudioCallback::write_callback(void* userdata, AudioUnitRenderActionFlags* io_action_flags, const AudioTimeStamp* in_time_stamp, const std::uint32_t in_bus_number, const std::uint32_t in_number_frames, AudioBufferList* io_data) // NOLINT
{
    auto* os = static_cast<SoundIoOutStreamPrivate*>(userdata);
    if (os == nullptr)
    {
        LOGE(L"os is nullptr!");
        return noErr;
    }

    // 裸指针 → shared_ptr 只能经由 enable_shared_from_this 的控制块；
    // weak_from_this().lock() 在对象已进入销毁流程时返回空，不会抛异常
    const auto os_sp = os->weak_from_this().lock();
    if (!os_sp)
    {
        return noErr;
    }

    auto& osca = os->backend_data.coreaudio;

    osca.io_data = io_data;
    osca.buffer_index = 0;
    osca.frames_left = static_cast<int>(in_number_frames);
    os_sp->write_callback(os_sp, osca.frames_left, osca.frames_left);
    osca.io_data = nullptr;

    return noErr;
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
    CFStringGetBytes(string_ref, range, encoding, '?', false, reinterpret_cast<UInt8*>(&wstr[0]), usedBufLen, nullptr);
    return wstr;
}

/**
 * @brief 将通用 SoundIoDeviceAim 输入输出类型转换为苹果原生定义的 Scope
 */
static int aim_to_scope(enum SoundIoDeviceAim aim)
{
    return (aim == SoundIoDeviceAimInput) ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;
}

/**
 * @brief 从指定的 AudioChannelDescription 找出其对应于 libsoundio 通用的 channel tag
 */
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

/**
 * @brief 将苹果特有的 AudioChannelLayout 枚举转换为通用的 SoundIoChannelLayout 配置数据对象
 * 
 * 注意该转换并未覆盖所有布局组合或者是 channel bitmaps。
 */
// See https://developer.apple.com/library/mac/documentation/MusicAudio/Reference/CoreAudioDataTypesRef/#//apple_ref/doc/constant_group/Audio_Channel_Layout_Tags
// Possible Errors:
// * SoundIoErrorIncompatibleDevice
// This does not handle all the possible layout enum values, and it does not
// handle channel bitmaps.
static int from_coreaudio_layout(const AudioChannelLayout* ca_layout, struct SoundIoChannelLayout* layout)
{
    switch (ca_layout->mChannelLayoutTag)
    {
        case kAudioChannelLayoutTag_UseChannelDescriptions:
        {
            layout->channel_count = std::min(SOUNDIO_MAX_CHANNELS, static_cast<int32_t>(ca_layout->mNumberChannelDescriptions));
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

/**
 * @brief 检查 channel layout 是不是包含全无效标示
 */
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
    std::uint32_t devices_size;
    std::vector<AudioObjectID> device_ids;
    std::wstring device_name;
    std::wstring container_id;
    std::shared_ptr<SoundIoDevice> device;
    std::wstring device_uid;
    bool ok;
};

/**
 * @brief 释放并销毁 RefreshDevices 中包裹的资源
 */
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


/**
 * @brief 全局刷新设备状态，与 CoreAudio framework 进行同步和轮询
 */
static int refresh_devices(const std::shared_ptr<SoundIoPrivate>& si)
{
    auto& sica = si->backend_data->coreaudio;

    std::uint32_t io_size = 0;
    OSStatus os_err = 0;
    int err = 0;

    CoreAudioCallback::unsubscribe_device_listeners(si);

    RefreshDevices rd = {};
    rd.si = si;

    rd.devices_info = std::make_unique<SoundIoDevicesInfo>();

    AudioObjectPropertyAddress devicesProp = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    os_err = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &devicesProp, 0, nullptr, &io_size);
    if (os_err != noErr)
    {
        deinit_refresh_devices(&rd);
        return SoundIoErrorOpeningDevice;
    }

    AudioObjectID default_input_id;
    AudioObjectID default_output_id;

    std::uint32_t device_count = io_size / sizeof(AudioObjectID);

    rd.device_ids.clear();
    // get default device
    if (device_count > 0)
    {
        rd.devices_size = io_size;
        auto system_devices = std::make_unique<AudioObjectID[]>(device_count);
        os_err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &devicesProp, 0, nullptr, &io_size, system_devices.get());
        if (os_err != noErr)
        {
            deinit_refresh_devices(&rd);
            return SoundIoErrorOpeningDevice;
        }

        for (std::uint32_t i = 0; i < device_count; ++i)
        {
            rd.device_ids.push_back(system_devices[i]);
        }

        io_size = sizeof(AudioObjectID);

        AudioObjectPropertyAddress defaultInputProp = {kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        os_err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultInputProp, 0, nullptr, &io_size, &default_input_id);
        if (os_err != noErr)
        {
            deinit_refresh_devices(&rd);
            return SoundIoErrorOpeningDevice;
        }

        io_size = sizeof(AudioObjectID);
        AudioObjectPropertyAddress defaultOutputProp = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        os_err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultOutputProp, 0, nullptr, &io_size, &default_output_id);

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
            os_err = AudioObjectAddPropertyListener(device_id, &device_listen_props[i], CoreAudioCallback::on_devices_changed, si.get());
            if (os_err != noErr)
            {
                deinit_refresh_devices(&rd);
                return SoundIoErrorOpeningDevice;
            }
        }

        sica.registered_listeners.push_back(device_id);
        io_size = sizeof(CFStringRef);
        CFStringRef buf;
        AudioObjectPropertyAddress getNameProp = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};

        os_err = AudioObjectGetPropertyData(device_id, &getNameProp, 0, nullptr, &io_size, &buf);
        if (os_err != noErr)
        {
            deinit_refresh_devices(&rd);
            return SoundIoErrorOpeningDevice;
        }

        rd.device_name = from_cf_string(buf);
        CFRelease(buf);
        buf = nullptr;

        io_size = sizeof(CFStringRef);
        AudioObjectPropertyAddress getUidProp = {kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        os_err = AudioObjectGetPropertyData(device_id, &getUidProp, 0, nullptr, &io_size, &buf);
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
            AudioObjectPropertyAddress configProp = {kAudioDevicePropertyStreamConfiguration, static_cast<AudioObjectPropertyScope>(aim_to_scope(aim)), kAudioObjectPropertyElementMain};
            os_err = AudioObjectGetPropertyDataSize(device_id, &configProp, 0, nullptr, &io_size);
            if (os_err != noErr)
            {
                deinit_refresh_devices(&rd);
                return SoundIoErrorOpeningDevice;
            }

            auto buffer_list = std::unique_ptr<AudioBufferList, decltype(&std::free)>(static_cast<AudioBufferList*>(soundio_malloc(io_size)), std::free);
            os_err = AudioObjectGetPropertyData(device_id, &configProp, 0, nullptr, &io_size, buffer_list.get());
            if (os_err != noErr)
            {
                deinit_refresh_devices(&rd);
                return SoundIoErrorOpeningDevice;
            }

            int32_t channel_count = 0;
            for (int i = 0; i < buffer_list->mNumberBuffers; i++)
            {
                channel_count += static_cast<int32_t>(buffer_list->mBuffers[i].mNumberChannels);
            }

            if (channel_count <= 0)
            {
                continue;
            }

            const auto dev = std::make_shared<SoundIoDevicePrivate>();
            const auto dca = dev->backend_data.coreaudio;

            dca->device_id = device_id;
            assert(!rd.device);
            rd.device = dev;
            rd.device->soundio = si;
            rd.device->is_raw = false;
            rd.device->aim = aim;
            rd.device->id = rd.device_uid;
            rd.device->name = rd.device_name;
            rd.device->container_id = rd.device_uid;

            AudioObjectPropertyAddress channel_prop = {kAudioDevicePropertyPreferredChannelLayout, static_cast<AudioObjectPropertyScope>(aim_to_scope(aim)), kAudioObjectPropertyElementMain};
            os_err = AudioObjectGetPropertyDataSize(device_id, &channel_prop, 0, nullptr, &io_size);

            if (os_err == noErr)
            {
                auto layout_buf = std::unique_ptr<AudioChannelLayout, decltype(&std::free)>(static_cast<AudioChannelLayout*>(soundio_malloc(io_size)), std::free);

                if (!layout_buf)
                {
                    deinit_refresh_devices(&rd);
                    return SoundIoErrorNoMem;
                }
                os_err = AudioObjectGetPropertyData(device_id, &channel_prop, 0, nullptr, &io_size, layout_buf.get());
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
                const auto* guessed_layout = soundio_channel_layout_get_default(channel_count);
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


            AudioObjectPropertyAddress sample_prop = {kAudioDevicePropertyNominalSampleRate, static_cast<AudioObjectPropertyScope>(aim_to_scope(aim)), kAudioObjectPropertyElementMain};
            io_size = sizeof(double);
            double value;
            os_err = AudioObjectGetPropertyData(device_id, &sample_prop, 0, nullptr, &io_size, &value);

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
                auto range = SoundIoSampleRateRange{rd.device->sample_rate_current, rd.device->sample_rate_current};
                rd.device->sample_rates.push_back(range);
            }
            else
            {
                AudioObjectPropertyAddress sampleRatesProp = {kAudioDevicePropertyAvailableNominalSampleRates, static_cast<AudioObjectPropertyScope>(aim_to_scope(aim)), kAudioObjectPropertyElementMain};
                os_err = AudioObjectGetPropertyDataSize(device_id, &sampleRatesProp, 0, nullptr, &io_size);

                if (os_err != noErr)
                {
                    deinit_refresh_devices(&rd);
                    return SoundIoErrorOpeningDevice;
                }

                std::uint32_t avr_array_len = io_size / sizeof(AudioValueRange);
                auto avr_buf = std::make_unique<AudioValueRange[]>(avr_array_len);

                if (!avr_buf)
                {
                    deinit_refresh_devices(&rd);
                    return SoundIoErrorNoMem;
                }
                os_err = AudioObjectGetPropertyData(device_id, &sampleRatesProp, 0, nullptr, &io_size, avr_buf.get());
                if (os_err != noErr)
                {
                    deinit_refresh_devices(&rd);
                    return SoundIoErrorOpeningDevice;
                }

                for (int i = 0; i < avr_array_len; i += 1)
                {
                    rd.device->sample_rates.push_back(SoundIoSampleRateRange{ceil_dbl_to_int(avr_buf[i].mMinimum), static_cast<int>(avr_buf[i].mMaximum)});
                }
            }

            AudioObjectPropertyAddress frameSizeProp = {kAudioDevicePropertyBufferFrameSize, static_cast<AudioObjectPropertyScope>(aim_to_scope(aim)), kAudioObjectPropertyElementMain};
            io_size = sizeof(std::uint32_t);
            std::uint32_t buffer_frame_size;
            os_err = AudioObjectGetPropertyData(device_id, &frameSizeProp, 0, nullptr, &io_size, &buffer_frame_size);
            if (os_err != noErr)
            {
                deinit_refresh_devices(&rd);
                return SoundIoErrorOpeningDevice;
            }

            rd.device->software_latency_current = static_cast<double>(buffer_frame_size) / rd.device->sample_rate_current;

            AudioObjectPropertyAddress frameSizeRangeProp = {kAudioDevicePropertyBufferFrameSizeRange, static_cast<AudioObjectPropertyScope>(aim_to_scope(aim)), kAudioObjectPropertyElementMain};
            io_size = sizeof(AudioValueRange);
            AudioValueRange avr;
            os_err = AudioObjectGetPropertyData(device_id, &frameSizeRangeProp, 0, nullptr, &io_size, &avr);
            if (os_err != noErr)
            {
                deinit_refresh_devices(&rd);
                return SoundIoErrorOpeningDevice;
            }

            rd.device->software_latency_min = avr.mMinimum / rd.device->sample_rate_current;
            rd.device->software_latency_max = avr.mMaximum / rd.device->sample_rate_current;

            AudioObjectPropertyAddress latencyProp = {kAudioDevicePropertyLatency, static_cast<AudioObjectPropertyScope>(aim_to_scope(aim)), kAudioObjectPropertyElementMain};
            io_size = sizeof(std::uint32_t);
            os_err = AudioObjectGetPropertyData(device_id, &latencyProp, 0, nullptr, &io_size, &dca->latency_frames);
            if (os_err != noErr)
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
                    rd.device->is_default = true;
                }
            }
            else
            {
                assert(rd.device->aim == SoundIoDeviceAimInput);
                device_list = &rd.devices_info->input_devices;
                if (device_id == default_input_id)
                {
                    rd.devices_info->default_input_index = static_cast<int>(device_list->size());
                    rd.device->is_default = true;
                }
            }

            device_list->push_back(rd.device);
            rd.device = nullptr;
        }
    }

    rd.ok = true;


    {
        std::unique_lock lock(sica.mutex->get());
        sica.ready_devices_info = std::move(rd.devices_info);
        sica.cond->signal(&lock);
        sica.have_devices_flag.store(true, std::memory_order_release);
        si->on_events_signal(si);
    }

    deinit_refresh_devices(&rd);

    return 0;
}

/**
 * @brief 响应故障进而卸下后端设备并通告主程序断开消息
 */
static void shutdown_backend(const std::shared_ptr<SoundIoPrivate>& si, int err)
{
    auto& sica = si->backend_data->coreaudio;
    std::unique_lock lock(sica.mutex->get());

    sica.shutdown_err.store(err, std::memory_order_release);
    sica.cond->signal(&lock);
    si->on_events_signal(si);
}

/**
 * @brief 拉取在 device 操作上抛出的 event 队列，处理 disconnect 及 devices_change signals
 */
static void my_flush_events(const std::shared_ptr<SoundIoPrivate>& si, bool wait)
{
    auto& sica = si->backend_data->coreaudio;

    bool change = false;
    bool cb_shutdown = false;

    {
        std::unique_lock lock(sica.mutex->get());
        if (wait)
        {
            sica.cond->wait(&lock, [&]
            {
                return sica.have_devices_flag.load(std::memory_order_acquire) || sica.shutdown_err.load(std::memory_order_acquire);
            });
        }

        if (sica.shutdown_err.load(std::memory_order_acquire) && !sica.emitted_shutdown_cb.load(std::memory_order_acquire))
        {
            sica.emitted_shutdown_cb.store(true, std::memory_order_release);
            cb_shutdown = true;
        }
        else if (sica.ready_devices_info)
        {
            si->safe_devices_info = std::move(sica.ready_devices_info);
            change = true;
        }
    }

    if (cb_shutdown)
    {
        si->on_backend_disconnect(si, sica.shutdown_err.load(std::memory_order_acquire));
    }
    else if (change)
    {
        si->on_devices_change(si);
    }
}

/**
 * @brief 执行非阻塞刷新 CA events 接口
 */
static void flush_events_ca(const std::shared_ptr<SoundIoPrivate>& si)
{
    my_flush_events(si, false);
}

/**
 * @brief 执行强阻塞挂起用于截获 CA events
 */
static void wait_events_ca(const std::shared_ptr<SoundIoPrivate>& si)
{
    my_flush_events(si, true);
}

/**
 * @brief 主动给 thread 派发结束阻塞挂起的 cond signal 
 */
static void wakeup_ca(const std::shared_ptr<SoundIoPrivate>& si)
{
    auto& sica = si->backend_data->coreaudio;
    std::unique_lock lock(sica.mutex->get());
    sica.cond->signal(&lock);
}

/**
 * @brief 强制触发 background 开始检索和处理设备的列表改动
 */
static void force_device_scan_ca(const std::shared_ptr<SoundIoPrivate>& si)
{
    auto& sica = si->backend_data->coreaudio;
    sica.device_scan_queued.store(true, std::memory_order_release);
    sica.have_devices_flag.store(false, std::memory_order_release);

    std::unique_lock scan_lock(sica.scan_devices_mutex->get());
    sica.scan_devices_cond->signal(&scan_lock);
}

/**
 * @brief CoreAudio devices 检测 background thread 执行框架
 */
static void device_thread_run(const std::stop_token& token, const std::shared_ptr<void>& arg)
{
    const auto si = std::static_pointer_cast<SoundIoPrivate>(arg);
    auto& sica = si->backend_data->coreaudio;
    int err = SoundIoErrorNone;

    while (!token.stop_requested())
    {
        {
            std::unique_lock lock(sica.scan_devices_mutex->get());
            sica.scan_devices_cond->wait(&lock, [&]
            {
                return token.stop_requested() || sica.service_restarted.load(std::memory_order_acquire) || sica.device_scan_queued.load(std::memory_order_acquire);
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
 * @brief 彻底撤解 output unit 和解除其对外接口封装对象的依赖
 */
static void outstream_destroy_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os)
{
    LOGI(L"destroy core audio outstream");
    auto& osca = os->backend_data.coreaudio;
    const auto dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(os->device);
    const auto dca = dev->backend_data.coreaudio;

    auto prop_address = AudioObjectPropertyAddress{kAudioDeviceProcessorOverload, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectRemovePropertyListener(dca->device_id, &prop_address, CoreAudioCallback::on_outstream_device_overload, os.get());

    if (osca.instance)
    {
        AudioOutputUnitStop(osca.instance);
        AudioComponentInstanceDispose(osca.instance);
        osca.instance = nullptr;
    }
}

/**
 * @brief 基于指定的 format 初始化目标 ASBD (AudioStreamBasicDescription) 数据位宽或标示符
 */
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

/**
 * @brief 在 macOS 下以指定的 layout 和 format 激活并初始化 CoreAudio outstream
 * 
 * 搜索 AudioDevice 并设置 format 关联到指定的 AudioUnitOutput 单元。
 * 
 * @param si CoreAudio system 指针
 * @param os 输出流上下文封装
 * @return int 成功则返回 0
 */
static int outstream_open_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os)
{
    LOGI(L"open core audio outstream");
    auto& osca = os->backend_data.coreaudio;
    const auto dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(os->device);
    const auto dca = dev->backend_data.coreaudio;

    if (os->software_latency == 0.0)
    {
        os->software_latency = dev->software_latency_current;
    }

    os->software_latency = std::clamp(os->software_latency, dev->software_latency_min, dev->software_latency_max);

    auto desc = AudioComponentDescription{};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_HALOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    // 查找组件
    const auto component = AudioComponentFindNext(nullptr, &desc);
    if (!component)
    {
        LOGE(L"AudioComponent not found.");
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    // 实例音频单元
    auto os_err = AudioComponentInstanceNew(component, &osca.instance);
    if (os_err != noErr)
    {
        LOGE(L"AudioComponentInstanceNew failed.");
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    os_err = AudioUnitInitialize(osca.instance);
    if (os_err != noErr)
    {
        LOGE(L"AudioUnitInitialize failed.");
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    auto format = AudioStreamBasicDescription{};
    format.mSampleRate = os->sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    const int err = set_ca_desc(os->format, &format);
    if (err)
    {
        LOGE(L"set_ca_desc failed.");
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
        LOGE(L"Binding outstream id %d failed.", dca->device_id);
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }
    os_err = AudioUnitSetProperty(osca.instance, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, OUTPUT_ELEMENT, &format, sizeof(AudioStreamBasicDescription));
    if (os_err != noErr)
    {
        LOGE(L"AudioUnitSetProperty format failed.");
        outstream_destroy_ca(si, os);
        return SoundIoErrorIncompatibleDevice;
    }

    const AURenderCallbackStruct render_callback = {CoreAudioCallback::write_callback, os.get()};
    os_err = AudioUnitSetProperty(osca.instance, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, OUTPUT_ELEMENT, &render_callback, sizeof(AURenderCallbackStruct));
    if (os_err != noErr)
    {
        LOGE(L"Binding render_callback failed.");
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    auto prop_address = AudioObjectPropertyAddress{kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeInput, OUTPUT_ELEMENT};
    const auto buffer_frame_size = static_cast<std::uint32_t>(os->software_latency * os->sample_rate);
    os_err = AudioObjectSetPropertyData(dca->device_id, &prop_address, 0, nullptr, sizeof(std::uint32_t), &buffer_frame_size);
    if (os_err != noErr)
    {
        LOGE(L"Set buffer frame size %d failed.", buffer_frame_size);
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    prop_address.mSelector = kAudioDeviceProcessorOverload;
    prop_address.mScope = kAudioObjectPropertyScopeGlobal;
    prop_address.mElement = OUTPUT_ELEMENT;
    os_err = AudioObjectAddPropertyListener(dca->device_id, &prop_address, CoreAudioCallback::on_outstream_device_overload, os.get());
    if (os_err != noErr)
    {
        LOGE(L"Binding callback overload failed.");
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }
    auto volume = 0.0f;
    os_err = AudioUnitGetParameter(osca.instance, kHALOutputParam_Volume, kAudioUnitScope_Global, 0, &volume);
    if (os_err != noErr)
    {
        LOGE(L"Set volume failed.");
        outstream_destroy_ca(si, os);
        return SoundIoErrorOpeningDevice;
    }

    os->volume.store(volume, std::memory_order_release);
    osca.hardware_latency = dca->latency_frames / static_cast<double>(os->sample_rate);
    os->paused = false;
    return 0;
}

/**
 * @brief 通过 AudioOutputUnitStop 达到停止 output 目标数据的处理，反之则开始
 */
static int outstream_pause_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os, bool pause)
{
    os->paused = pause;
    const auto& osca = os->backend_data.coreaudio;
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

/**
 * @brief 触发 stream 首播，包装了底层 pause 的逻辑控制
 */
static int outstream_start_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os)
{
    return outstream_pause_ca(si, os, false);
}

/**
 * @brief 在回调阶段通过将内存 layout 推给 out_areas 给应用进行后续填入准备
 */
static int outstream_begin_write_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os, SoundIoChannelArea** out_areas,
                                    int* out_frame_count)
{
    auto& osca = os->backend_data.coreaudio;
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
    osca.write_frame_count = static_cast<std::int32_t>(audio_buffer->mDataByteSize) / os->bytes_per_frame;
    *out_frame_count = osca.write_frame_count;
    assert((audio_buffer->mDataByteSize % os->bytes_per_frame) == 0);
    for (int ch = 0; ch < os->layout.channel_count; ch += 1)
    {
        osca.areas[ch].ptr = static_cast<char*>(audio_buffer->mData) + os->bytes_per_sample * ch;
        osca.areas[ch].step = os->bytes_per_frame;
    }
    *out_areas = osca.areas;
    return 0;
}

/**
 * @brief 数据填充完成时确认并提交缓冲段游标
 */
static int outstream_end_write_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os)
{
    auto& osca = os->backend_data.coreaudio;
    osca.buffer_index += 1;
    osca.frames_left -= osca.write_frame_count;
    assert(osca.frames_left >= 0);
    return 0;
}

/**
 * @brief Backend 阻挡对清空缓存 buffer 的请求
 */
static int outstream_clear_buffer_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os)
{
    return SoundIoErrorIncompatibleBackend;
}

/**
 * @brief 从系统取 latency，包括 hardware latency 和 system 提供的值
 */
static int outstream_get_latency_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os,
                                    double* out_latency)
{
    const auto& osca = os->backend_data.coreaudio;
    *out_latency = osca.hardware_latency;
    return 0;
}

/**
 * @brief 映射音量更改给 CoreAudio device 的 master/channel scope
 */
static int outstream_set_volume_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os, float volume)
{
    const auto& osca = os->backend_data.coreaudio;
    const OSStatus os_err = AudioUnitSetParameter(osca.instance, kHALOutputParam_Volume, kAudioUnitScope_Global, 0, volume, 0);
    if (os_err != noErr)
    {
        return SoundIoErrorIncompatibleDevice;
    }
    os->volume = volume;
    return 0;
}

/**
 * @brief 对 HAL 层销毁 instream 并终止系统级 AU 单元服务
 */
static void instream_destroy_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is)
{
    auto& isca = is->backend_data.coreaudio;
    const auto dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(is->device);
    const auto dca = dev->backend_data.coreaudio;

    auto prop_address = AudioObjectPropertyAddress{kAudioDeviceProcessorOverload, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectRemovePropertyListener(dca->device_id, &prop_address, CoreAudioCallback::on_instream_device_overload, is.get());

    if (isca.instance)
    {
        AudioOutputUnitStop(isca.instance);
        AudioComponentInstanceDispose(isca.instance);
        isca.instance = nullptr;
    }

    isca.buffer_list = nullptr;
}

/**
 * @brief Instream (捕捉/录音) 期间被系统填充数据触发的 handler entry：拿回 stream 的 shared_ptr 并把数据交给 user callback
 */
OSStatus CoreAudioCallback::read_callback(void* userdata, AudioUnitRenderActionFlags* io_action_flags, //NOLINT
                                          const AudioTimeStamp* in_time_stamp,
                                          const std::uint32_t in_bus_number,
                                          const std::uint32_t in_number_frames,
                                          AudioBufferList* io_data)
{
    auto* is = static_cast<SoundIoInStreamPrivate*>(userdata);
    if (is == nullptr)
    {
        LOGE(L"is is nullptr!");
        return noErr;
    }

    // 裸指针 → shared_ptr 只能经由 enable_shared_from_this 的控制块
    const auto is_sp = is->weak_from_this().lock();
    if (!is_sp)
    {
        return noErr;
    }

    auto& isca = is->backend_data.coreaudio;

    for (int i = 0; i < isca.buffer_list->mNumberBuffers; i++)
    {
        isca.buffer_list->mBuffers[i].mData = nullptr;
    }

    const OSStatus os_err = AudioUnitRender(isca.instance, io_action_flags, in_time_stamp, in_bus_number, in_number_frames, isca.buffer_list.get());
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
        for (int ch = 0; ch < is->layout.channel_count; ch += 1)
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
 * @brief 解析 macOS DefaultInputDevice 并建立 Input Unit AudioComponent
 */
static int instream_open_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is)
{
    auto& isca = is->backend_data.coreaudio;
    const auto dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(is->device);
    const auto dca = dev->backend_data.coreaudio;
    std::uint32_t io_size;
    OSStatus os_err;

    if (is->software_latency == 0.0)
    {
        is->software_latency = dev->software_latency_current;
    }

    is->software_latency = std::clamp(is->software_latency, dev->software_latency_min, dev->software_latency_max);


    auto prop_address = AudioObjectPropertyAddress{};
    prop_address.mSelector = kAudioDevicePropertyStreamConfiguration;
    prop_address.mScope = kAudioObjectPropertyScopeInput;
    prop_address.mElement = kAudioObjectPropertyElementMain;
    io_size = 0;
    os_err = AudioObjectGetPropertyDataSize(dca->device_id, &prop_address, 0, nullptr, &io_size);
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    isca.buffer_list.reset(static_cast<AudioBufferList*>(soundio_malloc(io_size)));
    if (!isca.buffer_list)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorNoMem;
    }

    os_err = AudioObjectGetPropertyData(dca->device_id, &prop_address, 0, nullptr, &io_size, isca.buffer_list.get());
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }


    auto desc = AudioComponentDescription{};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_HALOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    const auto component = AudioComponentFindNext(nullptr, &desc);
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

    auto enable_io = static_cast<std::uint32_t>(1);
    os_err = AudioUnitSetProperty(isca.instance, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, INPUT_ELEMENT, &enable_io, sizeof(std::uint32_t));
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    enable_io = 0;
    os_err = AudioUnitSetProperty(isca.instance, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, OUTPUT_ELEMENT, &enable_io, sizeof(std::uint32_t));
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


    auto format = AudioStreamBasicDescription{};
    format.mSampleRate = is->sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mBytesPerPacket = is->bytes_per_frame;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = is->bytes_per_frame;
    format.mChannelsPerFrame = is->layout.channel_count;

    const int err = set_ca_desc(is->format, &format);
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

    const AURenderCallbackStruct input_callback = {CoreAudioCallback::read_callback, is.get()};
    os_err = AudioUnitSetProperty(isca.instance, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Output, INPUT_ELEMENT, &input_callback, sizeof(AURenderCallbackStruct));
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }


    prop_address.mSelector = kAudioDevicePropertyBufferFrameSize;
    prop_address.mScope = kAudioObjectPropertyScopeOutput;
    prop_address.mElement = INPUT_ELEMENT;
    const auto buffer_frame_size = static_cast<std::uint32_t>(is->software_latency) * is->sample_rate;
    os_err = AudioObjectSetPropertyData(dca->device_id, &prop_address, 0, nullptr, sizeof(std::uint32_t), &buffer_frame_size);
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    prop_address.mSelector = kAudioDeviceProcessorOverload;
    prop_address.mScope = kAudioObjectPropertyScopeGlobal;
    os_err = AudioObjectAddPropertyListener(dca->device_id, &prop_address, CoreAudioCallback::on_instream_device_overload, is.get());
    if (os_err != noErr)
    {
        instream_destroy_ca(si, is);
        return SoundIoErrorOpeningDevice;
    }

    isca.hardware_latency = dca->latency_frames / static_cast<double>(is->sample_rate);

    return 0;
}

/**
 * @brief 调用 AudioOutputUnit (Input Scope) 的停止操作
 */
static int instream_pause_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is, bool pause)
{
    const auto& isca = is->backend_data.coreaudio;
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

/**
 * @brief 绑定读数据回调引用并通知底端开启 capture 过程
 */
static int instream_start_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is)
{
    return instream_pause_ca(si, is, false);
}

/**
 * @brief 请求系统内存并绑定指针地址给 instream 以供 app 截获麦克风等设备音频数据
 */
static int instream_begin_read_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is, SoundIoChannelArea** out_areas, int* out_frame_count) //NOLINT
{
    auto& isca = is->backend_data.coreaudio;

    if (*out_frame_count != isca.frames_left)
    {
        return SoundIoErrorInvalid;
    }

    *out_areas = isca.areas;
    return 0;
}

/**
 * @brief 完成读取标记，准备让出当前区域
 */
static int instream_end_read_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is)
{
    auto* isca = &is->backend_data.coreaudio;
    isca->frames_left = 0;
    return 0;
}

/**
 * @brief 评估 macOS 设备的音频捕捉 latency 时间差
 */
static int instream_get_latency_ca(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is, double* out_latency)
{
    const auto& isca = is->backend_data.coreaudio;
    *out_latency = isca.hardware_latency;
    return 0;
}


/**
 * @brief 通知退出背景更新进程并解除各类全局 system observers 的订阅
 */
static void destroy_ca(const std::shared_ptr<SoundIoPrivate>& si)
{
    LOGI(L"destroy core audio");

    auto& sica = si->backend_data->coreaudio;

    auto prop_address = AudioObjectPropertyAddress{kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &prop_address, CoreAudioCallback::on_devices_changed, si.get());

    prop_address.mSelector = kAudioHardwarePropertyDevices;
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &prop_address, CoreAudioCallback::on_devices_changed, si.get());

    prop_address.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &prop_address, CoreAudioCallback::on_devices_changed, si.get());


    prop_address.mSelector = kAudioHardwarePropertyServiceRestarted;
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &prop_address, CoreAudioCallback::on_service_restarted, si.get());

    CoreAudioCallback::unsubscribe_device_listeners(si);

    if (sica.thread)
    {
        {
            std::unique_lock lock(sica.scan_devices_mutex->get());
            sica.thread->request_stop();
            sica.scan_devices_cond->signal(&lock);
        }
        sica.thread = nullptr;
    }

    sica.mutex = nullptr;
    sica.cond = nullptr;

    sica.scan_devices_mutex = nullptr;
    sica.scan_devices_cond = nullptr;

    sica.ready_devices_info = nullptr;

    for (const auto& weak_os: si->outstreams | std::ranges::views::values)
    {
        if (const auto os = weak_os.lock())
        {
            outstream_destroy_ca(si, std::dynamic_pointer_cast<SoundIoOutStreamPrivate>(os));
        }
    }

    si->ClearOutStreams();

}

/**
 * @brief SoundIo coreaudio 的模块化总入口
 * 
 * 执行 CoreAudio devices 服务挂载、listener 设定，分配 OS resources 给设备扫描 task。
 * 
 * @param si libsoundio backend instances
 * @return int 成功返回 0，无内存报告则报错
 */
int soundio_coreaudio_init(const std::shared_ptr<SoundIoPrivate>& si)
{
    auto& sica = si->backend_data->coreaudio;
    sica.service_restarted.store(false);

    sica.mutex = soundio_os_mutex_create();
    if (!sica.mutex)
    {
        destroy_ca(si);
        return SoundIoErrorNoMem;
    }

    sica.cond = soundio_os_cond_create();
    if (!sica.cond)
    {
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
    if (!sica.scan_devices_cond)
    {
        destroy_ca(si);
        return SoundIoErrorNoMem;
    }

    auto prop_address = AudioObjectPropertyAddress{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    int err = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &prop_address, CoreAudioCallback::on_devices_changed, si.get());
    if (err != noErr)
    {
        destroy_ca(si);
        return SoundIoErrorSystemResources;
    }

    prop_address.mSelector = kAudioHardwarePropertyDefaultInputDevice;
    err = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &prop_address, CoreAudioCallback::on_devices_changed, si.get());
    if (err != noErr)
    {
        destroy_ca(si);
        return SoundIoErrorSystemResources;
    }

    prop_address.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    err = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &prop_address, CoreAudioCallback::on_devices_changed, si.get());
    if (err != noErr)
    {
        destroy_ca(si);
        return SoundIoErrorSystemResources;
    }

    prop_address.mSelector = kAudioHardwarePropertyServiceRestarted;
    err = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &prop_address, CoreAudioCallback::on_service_restarted, si.get());
    if (err != noErr)
    {
        destroy_ca(si);
        return SoundIoErrorSystemResources;
    }

    sica.thread = SoundIoOsThread::create(device_thread_run, si);

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
