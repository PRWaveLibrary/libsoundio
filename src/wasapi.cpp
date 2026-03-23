// ==============================================================================
// 1. Macros, Constants and Types
// ==============================================================================

/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of libsoundio, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define INITGUID
// #define CINTERFACE
#define COBJMACROS
#define CONST_VTABLE

#include <initguid.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include "logger/logger.hpp"

#include <mmdeviceapi.h>

#include "soundio_private.h"
#include "wasapi.h"

#include <cstdio>
#include <thread>
#include "util.h"

#ifdef _MSC_VER
#include <propkeydef.h>
#endif

#include <functiondiscoverykeys_devpkey.h>

// Some HRESULT values are not defined by the windows headers
#ifndef E_NOTFOUND
#define E_NOTFOUND 0x80070490
#endif // E_NOTFOUND

#ifdef __cplusplus

// In C++ mode, IsEqualGUID() takes its arguments by reference
#define IS_EQUAL_GUID(a, b) IsEqualGUID((a), (b))
#define IS_EQUAL_IID(a, b) IsEqualIID((a), (b))

// And some constants are passed by reference
#define IID_IAUDIOCLIENT (IID_IAudioClient)
#define IID_IAUDIOCLIENT3 (IID_IAudioClient3)
#define IID_IMMENDPOINT (IID_IMMEndpoint)
#define IID_IAUDIOCLOCKADJUSTMENT (IID_IAudioClockAdjustment)
#define IID_IAUDIOSESSIONCONTROL (IID_IAudioSessionControl)
#define IID_IAUDIORENDERCLIENT (IID_IAudioRenderClient)
#define IID_IMMDEVICEENUMERATOR (IID_IMMDeviceEnumerator)
#define IID_IAUDIOCAPTURECLIENT (IID_IAudioCaptureClient)
#define IID_ISIMPLEAUDIOVOLUME (IID_ISimpleAudioVolume)
#define CLSID_MMDEVICEENUMERATOR (CLSID_MMDeviceEnumerator)
#define PKEY_DEVICE_FRIENDLYNAME (PKEY_Device_FriendlyName)
#define PKEY_AUDIOENGINE_DEVICEFORMAT (PKEY_AudioEngine_DeviceFormat)
#define IID_IAUDIOCLOCK (IID_IAudioClock)
#define IID_IUNKNOWN (IID_IUnknown)
#define IID_IMMNOTFICATION_CLIENT (IID_IMMNotificationClient)

#ifdef _MSC_VER
// And some GUID are never implemented (Ignoring the INITGUID define)
static const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);

// MIDL_INTERFACE("7ED4EE07-8E67-4CD4-8C1A-2B7A5987AD42")
static const IID IID_IAudioClient3 = {
        0x7ed4ee07,
        0x8e67,
        0x4cd4,
        {0x8c, 0x1a, 0x2b, 0x7a, 0x59, 0x87, 0xad, 0x42}
};

// MIDL_INTERFACE("CD63314F-3FBA-4a1b-812C-EF96358728E7")
static const IID IID_IAudioClock = {
        0xcd63314f,
        0x3fba,
        0x4a1b,
        {0x81, 0x2c, 0xef, 0x96, 0x35, 0x87, 0x28, 0xe7}
};

static const IID IID_IMMDeviceEnumerator = {
        // MIDL_INTERFACE("A95664D2-9614-4F35-A746-DE8DB63617E6")
        0xa95664d2,
        0x9614,
        0x4f35,
        {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}
};
static const IID IID_IMMNotificationClient = {
        // MIDL_INTERFACE("7991EEC9-7E89-4D85-8390-6C703CEC60C0")
        0x7991eec9,
        0x7e89,
        0x4d85,
        {0x83, 0x90, 0x6c, 0x70, 0x3c, 0xec, 0x60, 0xc0}
};
static const IID IID_IAudioClient = {
        // MIDL_INTERFACE("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2")
        0x1cb9ad4c,
        0xdbfa,
        0x4c32,
        {0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2}
};
static const IID IID_IAudioRenderClient = {
        // MIDL_INTERFACE("F294ACFC-3146-4483-A7BF-ADDCA7C260E2")
        0xf294acfc,
        0x3146,
        0x4483,
        {0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2}
};
static const IID IID_IAudioSessionControl = {
        // MIDL_INTERFACE("F4B1A599-7266-4319-A8CA-E70ACB11E8CD")
        0xf4b1a599,
        0x7266,
        0x4319,
        {0xa8, 0xca, 0xe7, 0x0a, 0xcb, 0x11, 0xe8, 0xcd}
};
static const IID IID_IAudioSessionEvents = {
        // MIDL_INTERFACE("24918ACC-64B3-37C1-8CA9-74A66E9957A8")
        0x24918acc,
        0x64b3,
        0x37c1,
        {0x8c, 0xa9, 0x74, 0xa6, 0x6e, 0x99, 0x57, 0xa8}
};
static const IID IID_IMMEndpoint = {
        // MIDL_INTERFACE("1BE09788-6894-4089-8586-9A2A6C265AC5")
        0x1be09788,
        0x6894,
        0x4089,
        {0x85, 0x86, 0x9a, 0x2a, 0x6c, 0x26, 0x5a, 0xc5}
};
static const IID IID_IAudioClockAdjustment = {
        // MIDL_INTERFACE("f6e4c0a0-46d9-4fb8-be21-57a3ef2b626c")
        0xf6e4c0a0,
        0x46d9,
        0x4fb8,
        {0xbe, 0x21, 0x57, 0xa3, 0xef, 0x2b, 0x62, 0x6c}
};
static const IID IID_IAudioCaptureClient = {
        // MIDL_INTERFACE("C8ADBD64-E71E-48a0-A4DE-185C395CD317")
        0xc8adbd64,
        0xe71e,
        0x48a0,
        {0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17}
};
static const IID IID_ISimpleAudioVolume = {
        // MIDL_INTERFACE("87ce5498-68d6-44e5-9215-6da47ef883d8")
        0x87ce5498,
        0x68d6,
        0x44e5,
        {0x92, 0x15, 0x6d, 0xa4, 0x7e, 0xf8, 0x83, 0xd8}
};
#endif

#else
#define IS_EQUAL_GUID(a, b) IsEqualGUID((a), (b))
#define IS_EQUAL_IID(a, b) IsEqualIID((a), (b))
#define IID_IAUDIOCLIENT IID_IAudioClient
#define IID_IAUDIOCLIENT3 IID_IAudioClient3
#define IID_IMMENDPOINT IID_IMMEndpoint
#define PKEY_DEVICE_FRIENDLYNAME PKEY_Device_FriendlyName
#define PKEY_AUDIOENGINE_DEVICEFORMAT PKEY_AudioEngine_DeviceFormat
#define CLSID_MMDEVICEENUMERATOR CLSID_MMDeviceEnumerator
#define IID_IAUDIOCLOCKADJUSTMENT IID_IAudioClockAdjustment
#define IID_IAUDIOSESSIONCONTROL IID_IAudioSessionControl
#define IID_IAUDIORENDERCLIENT IID_IAudioRenderClient
#define IID_IAUDIOCLOCK IID_IAudioClock
#define IID_IMMDEVICEENUMERATOR IID_IMMDeviceEnumerator
#define IID_IAUDIOCAPTURECLIENT IID_IAudioCaptureClient
#define IID_ISIMPLEAUDIOVOLUME IID_ISimpleAudioVolume
#define IID_IUNKNOWN IID_IUnknown
#define IID_IMMNOTFICATION_CLIENT IID_IMMNotificationClient
#endif

// Attempting to use the Windows-supplied versions of these constants resulted
// in `undefined reference` linker errors.
const static GUID SOUNDIO_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {
        0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

const static GUID SOUNDIO_KSDATAFORMAT_SUBTYPE_PCM = {
        0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

// Adding more common sample rates helps the heuristics; feel free to do that.
static int test_sample_rates[] = {
        8000, 11025, 16000, 22050, 32000, 37800, 44056, 44100, 47250, 48000,
        50000, 50400, 88200, 96000, 176400, 192000, 352800, 2822400, 5644800,
};

// If you modify this list, also modify `to_wave_format_format` appropriately.
static enum SoundIoFormat test_formats[] = {
        SoundIoFormatU8, SoundIoFormatS16LE, SoundIoFormatS24LE,
        SoundIoFormatS32LE, SoundIoFormatFloat32LE, SoundIoFormatFloat64LE,
};

// If you modify this list, also modify `to_wave_format_layout` appropriately.
static enum SoundIoChannelLayoutId test_layouts[] = {
        SoundIoChannelLayoutIdMono, SoundIoChannelLayoutIdStereo, SoundIoChannelLayoutIdQuad,
        SoundIoChannelLayoutId4Point0, SoundIoChannelLayoutId5Point1, SoundIoChannelLayoutId7Point1,
        SoundIoChannelLayoutId5Point1Back,
};

// ==============================================================================
// 2. Utility & Format Conversion
// ==============================================================================

#ifdef DEBUG
/**
 * @brief 从 IMMDevice 获取 user-friendly 的设备名
 * 
 * 访问 device 的 PKEY_Device_FriendlyName 属性，将其转换为 wstring 返回。
 * 
 * @param device 目标 IMMDevice 的指针
 * @return std::wstring 设备的 friendly name
 */
static std::wstring GetDeviceName(IMMDevice* device)
{
    IPropertyStore* pProps = nullptr;
    device->OpenPropertyStore(STGM_READ, &pProps);

    PROPVARIANT varName;
    PropVariantInit(&varName);

    pProps->GetValue(PKEY_Device_FriendlyName, &varName);
    return varName.pwszVal;
}
#endif

/**
 * @brief 从 channel mask 解析并构建 SoundIoChannelLayout
 * 
 * 根据 mask 定义的 bit 位图解析出对应的音频 channels（如 front left, front right 等）。
 * 
 * @param channel_mask WASAPI 或 Windows 标准的 channel_mask
 * @param layout 用于接收解析结果的 SoundIoChannelLayout 指针
 */
static void from_channel_mask_layout(UINT channel_mask, struct SoundIoChannelLayout* layout)
{
    layout->channel_count = 0;
    if (channel_mask & SPEAKER_FRONT_LEFT)
        layout->channels[layout->channel_count++] = SoundIoChannelIdFrontLeft;
    if (channel_mask & SPEAKER_FRONT_RIGHT)
        layout->channels[layout->channel_count++] = SoundIoChannelIdFrontRight;
    if (channel_mask & SPEAKER_FRONT_CENTER)
        layout->channels[layout->channel_count++] = SoundIoChannelIdFrontCenter;
    if (channel_mask & SPEAKER_LOW_FREQUENCY)
        layout->channels[layout->channel_count++] = SoundIoChannelIdLfe;
    if (channel_mask & SPEAKER_BACK_LEFT)
        layout->channels[layout->channel_count++] = SoundIoChannelIdBackLeft;
    if (channel_mask & SPEAKER_BACK_RIGHT)
        layout->channels[layout->channel_count++] = SoundIoChannelIdBackRight;
    if (channel_mask & SPEAKER_FRONT_LEFT_OF_CENTER)
        layout->channels[layout->channel_count++] = SoundIoChannelIdFrontLeftCenter;
    if (channel_mask & SPEAKER_FRONT_RIGHT_OF_CENTER)
        layout->channels[layout->channel_count++] = SoundIoChannelIdFrontRightCenter;
    if (channel_mask & SPEAKER_BACK_CENTER)
        layout->channels[layout->channel_count++] = SoundIoChannelIdBackCenter;
    if (channel_mask & SPEAKER_SIDE_LEFT)
        layout->channels[layout->channel_count++] = SoundIoChannelIdSideLeft;
    if (channel_mask & SPEAKER_SIDE_RIGHT)
        layout->channels[layout->channel_count++] = SoundIoChannelIdSideRight;
    if (channel_mask & SPEAKER_TOP_CENTER)
        layout->channels[layout->channel_count++] = SoundIoChannelIdTopCenter;
    if (channel_mask & SPEAKER_TOP_FRONT_LEFT)
        layout->channels[layout->channel_count++] = SoundIoChannelIdTopFrontLeft;
    if (channel_mask & SPEAKER_TOP_FRONT_CENTER)
        layout->channels[layout->channel_count++] = SoundIoChannelIdTopFrontCenter;
    if (channel_mask & SPEAKER_TOP_FRONT_RIGHT)
        layout->channels[layout->channel_count++] = SoundIoChannelIdTopFrontRight;
    if (channel_mask & SPEAKER_TOP_BACK_LEFT)
        layout->channels[layout->channel_count++] = SoundIoChannelIdTopBackLeft;
    if (channel_mask & SPEAKER_TOP_BACK_CENTER)
        layout->channels[layout->channel_count++] = SoundIoChannelIdTopBackCenter;
    if (channel_mask & SPEAKER_TOP_BACK_RIGHT)
        layout->channels[layout->channel_count++] = SoundIoChannelIdTopBackRight;

    soundio_channel_layout_detect_builtin(layout);
}

/**
 * @brief 从 WAVEFORMATEXTENSIBLE 获取 SoundIoChannelLayout
 * 
 * 使用 from_channel_mask_layout 提取并识别 channels。
 * 
 * @param wave_format 包含 channel mask 的 wave_format 指针
 * @param layout 输出的 layout 数据
 */
static void from_wave_format_layout(WAVEFORMATEXTENSIBLE* wave_format, struct SoundIoChannelLayout* layout)
{
    assert(wave_format->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE);
    layout->channel_count = 0;
    from_channel_mask_layout(wave_format->dwChannelMask, layout);
}

/**
 * @brief 从 WAVEFORMATEXTENSIBLE 解析出对应的 SoundIoFormat
 * 
 * 识别 bits_per_sample 和 subtype 来判断音频的具体格式 (如 Float32LE, S16LE).
 * 
 * @param wave_format 包含 format 参数的数据结构
 * @return enum SoundIoFormat 如果支持则返回有效格式，否则返回 Invalid
 */
static enum SoundIoFormat from_wave_format_format(WAVEFORMATEXTENSIBLE* wave_format)
{
    assert(wave_format->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE);
    bool is_pcm = IS_EQUAL_GUID(wave_format->SubFormat, SOUNDIO_KSDATAFORMAT_SUBTYPE_PCM);
    bool is_float = IS_EQUAL_GUID(wave_format->SubFormat, SOUNDIO_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);

    if (wave_format->Samples.wValidBitsPerSample == wave_format->Format.wBitsPerSample)
    {
        if (wave_format->Format.wBitsPerSample == 8)
        {
            if (is_pcm)
                return SoundIoFormatU8;
        }
        else if (wave_format->Format.wBitsPerSample == 16)
        {
            if (is_pcm)
                return SoundIoFormatS16LE;
        }
        else if (wave_format->Format.wBitsPerSample == 32)
        {
            if (is_pcm)
                return SoundIoFormatS32LE;
            else if (is_float)
                return SoundIoFormatFloat32LE;
        }
        else if (wave_format->Format.wBitsPerSample == 64)
        {
            if (is_float)
                return SoundIoFormatFloat64LE;
        }
    }
    else if (wave_format->Format.wBitsPerSample == 32 && wave_format->Samples.wValidBitsPerSample == 24)
    {
        return SoundIoFormatS24LE;
    }

    return SoundIoFormatInvalid;
}

/**
 * @brief 将 SoundIoChannelLayout 编码成对应的 channel mask
 * 
 * 仅需要支持 test_layouts 中覆盖的布局并写入 WAVEFORMATEXTENSIBLE 结构。
 * 
 * @param layout 要转换的 soundio 定义的 channel layout
 * @param wave_format 用于接收生成的 channel mask 的指向目标
 */
static void to_wave_format_layout(const struct SoundIoChannelLayout* layout, WAVEFORMATEXTENSIBLE* wave_format)
{
    wave_format->dwChannelMask = 0;
    wave_format->Format.nChannels = layout->channel_count;
    for (int i = 0; i < layout->channel_count; i += 1)
    {
        enum SoundIoChannelId channel_id = layout->channels[i];
        switch (channel_id)
        {
            case SoundIoChannelIdFrontLeft:
                wave_format->dwChannelMask |= SPEAKER_FRONT_LEFT;
                break;
            case SoundIoChannelIdFrontRight:
                wave_format->dwChannelMask |= SPEAKER_FRONT_RIGHT;
                break;
            case SoundIoChannelIdFrontCenter:
                wave_format->dwChannelMask |= SPEAKER_FRONT_CENTER;
                break;
            case SoundIoChannelIdLfe:
                wave_format->dwChannelMask |= SPEAKER_LOW_FREQUENCY;
                break;
            case SoundIoChannelIdBackLeft:
                wave_format->dwChannelMask |= SPEAKER_BACK_LEFT;
                break;
            case SoundIoChannelIdBackRight:
                wave_format->dwChannelMask |= SPEAKER_BACK_RIGHT;
                break;
            case SoundIoChannelIdFrontLeftCenter:
                wave_format->dwChannelMask |= SPEAKER_FRONT_LEFT_OF_CENTER;
                break;
            case SoundIoChannelIdFrontRightCenter:
                wave_format->dwChannelMask |= SPEAKER_FRONT_RIGHT_OF_CENTER;
                break;
            case SoundIoChannelIdBackCenter:
                wave_format->dwChannelMask |= SPEAKER_BACK_CENTER;
                break;
            case SoundIoChannelIdSideLeft:
                wave_format->dwChannelMask |= SPEAKER_SIDE_LEFT;
                break;
            case SoundIoChannelIdSideRight:
                wave_format->dwChannelMask |= SPEAKER_SIDE_RIGHT;
                break;
            case SoundIoChannelIdTopCenter:
                wave_format->dwChannelMask |= SPEAKER_TOP_CENTER;
                break;
            case SoundIoChannelIdTopFrontLeft:
                wave_format->dwChannelMask |= SPEAKER_TOP_FRONT_LEFT;
                break;
            case SoundIoChannelIdTopFrontCenter:
                wave_format->dwChannelMask |= SPEAKER_TOP_FRONT_CENTER;
                break;
            case SoundIoChannelIdTopFrontRight:
                wave_format->dwChannelMask |= SPEAKER_TOP_FRONT_RIGHT;
                break;
            case SoundIoChannelIdTopBackLeft:
                wave_format->dwChannelMask |= SPEAKER_TOP_BACK_LEFT;
                break;
            case SoundIoChannelIdTopBackCenter:
                wave_format->dwChannelMask |= SPEAKER_TOP_BACK_CENTER;
                break;
            case SoundIoChannelIdTopBackRight:
                wave_format->dwChannelMask |= SPEAKER_TOP_BACK_RIGHT;
                break;
            default:
                soundio_panic("to_wave_format_layout: unsupported channel id");
        }
    }
}

/**
 * @brief 按照指定的 SoundIoFormat 写入相应的格式到 WAVEFORMATEXTENSIBLE
 * 
 * 将内部音频 formats 结构翻译为 Windows 接口可识别的 SubFormat 等类型。
 * 
 * @param format 要被翻译的 SoundIoFormat 形式
 * @param wave_format 目标被写入的 struct 
 */
static void to_wave_format_format(enum SoundIoFormat format, WAVEFORMATEXTENSIBLE* wave_format)
{
    switch (format)
    {
        case SoundIoFormatU8:
            wave_format->SubFormat = SOUNDIO_KSDATAFORMAT_SUBTYPE_PCM;
            wave_format->Format.wBitsPerSample = 8;
            wave_format->Samples.wValidBitsPerSample = 8;
            break;
        case SoundIoFormatS16LE:
            wave_format->SubFormat = SOUNDIO_KSDATAFORMAT_SUBTYPE_PCM;
            wave_format->Format.wBitsPerSample = 16;
            wave_format->Samples.wValidBitsPerSample = 16;
            break;
        case SoundIoFormatS24LE:
            wave_format->SubFormat = SOUNDIO_KSDATAFORMAT_SUBTYPE_PCM;
            wave_format->Format.wBitsPerSample = 32;
            wave_format->Samples.wValidBitsPerSample = 24;
            break;
        case SoundIoFormatS32LE:
            wave_format->SubFormat = SOUNDIO_KSDATAFORMAT_SUBTYPE_PCM;
            wave_format->Format.wBitsPerSample = 32;
            wave_format->Samples.wValidBitsPerSample = 32;
            break;
        case SoundIoFormatFloat32LE:
            wave_format->SubFormat = SOUNDIO_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            wave_format->Format.wBitsPerSample = 32;
            wave_format->Samples.wValidBitsPerSample = 32;
            break;
        case SoundIoFormatFloat64LE:
            wave_format->SubFormat = SOUNDIO_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            wave_format->Format.wBitsPerSample = 64;
            wave_format->Samples.wValidBitsPerSample = 64;
            break;
        default:
            soundio_panic("to_wave_format_format: unsupported format");
    }
}

/**
 * @brief 补全 WAVEFORMATEXTENSIBLE 中的衍生数据
 * 
 * 根据当前填入的 channels 数和 bits 等基础信息来计算并补全 block align 和 byte rates。
 * 
 * @param wave_format 将被自动补全的目标 format 参数
 */
static void complete_wave_format_data(WAVEFORMATEXTENSIBLE* wave_format)
{
    wave_format->Format.nBlockAlign = (wave_format->Format.wBitsPerSample * wave_format->Format.nChannels) / 8;
    wave_format->Format.nAvgBytesPerSec = wave_format->Format.nSamplesPerSec * wave_format->Format.nBlockAlign;
}

/**
 * @brief 将 WASAPI 的 data flow 枚举转换为 soundio 设备目的(Input/Output)
 * 
 * @param data_flow eRender 或 eCapture
 * @return enum SoundIoDeviceAim 输出或捕获 aim
 */
static enum SoundIoDeviceAim data_flow_to_aim(EDataFlow data_flow)
{
    return (data_flow == eRender) ? SoundIoDeviceAimOutput : SoundIoDeviceAimInput;
}


/**
 * @brief 从 REFERENCE_TIME 转换为双精度秒单位
 * 
 * @param rt 100 纳秒为单位的时间
 * @return double 对应的 seconds
 */
static double from_reference_time(REFERENCE_TIME rt)
{
    return static_cast<double>(rt) / 10000000.0;
}

/**
 * @brief 将秒数时间转换为 REFERENCE_TIME
 * 
 * @param seconds 要被转换的时间 (秒)
 * @return REFERENCE_TIME 对应的 100 纳秒精度的整型时间
 */
static REFERENCE_TIME to_reference_time(double seconds)
{
    return static_cast<REFERENCE_TIME>(seconds * 10000000.0 + 0.5);
}

// ==============================================================================
// 3. Device Scanning & Enumeration
// ==============================================================================

struct RefreshDevices
{
    IMMDeviceCollection* collection = nullptr;
    std::shared_ptr<IMMDevice> mm_device;
    IMMDevice* default_render_device = nullptr;
    IMMDevice* default_capture_device = nullptr;
    IMMEndpoint* endpoint = nullptr;
    IPropertyStore* prop_store = nullptr;
    IAudioClient* audio_client = nullptr;
    PROPVARIANT prop_variant_value;
    WAVEFORMATEXTENSIBLE* wave_format = nullptr;
    bool prop_variant_value_inited = false;
    std::unique_ptr<SoundIoDevicesInfo> devices_info;
    std::shared_ptr<SoundIoDevice> device_shared;
    std::shared_ptr<SoundIoDevice> device_raw;
    std::wstring default_render_id;
    std::wstring default_capture_id;

    ~RefreshDevices()
    {
        if (collection)
            collection->Release();
        if (default_render_device)
            default_render_device->Release();
        if (default_capture_device)
            default_capture_device->Release();
        if (endpoint)
            endpoint->Release();
        if (prop_store)
            prop_store->Release();
        if (audio_client)
            audio_client->Release();
        if (prop_variant_value_inited)
            PropVariantClear(&prop_variant_value);
        if (wave_format)
            CoTaskMemFree(wave_format);
    }
};

/**
 * @brief 检测并筛选出设备支持的有效 sound channel layouts
 * 
 * 使用指定的 share_mode 尝试调用设备接口进行 format 初始化，
 * 将满足规则的 layout 加入到 dev->layouts 中。
 * 
 * @param rd RefreshDevices 状态的 shared_ptr
 * @param wave_format 将用于测试的 WAVEFORMATEXTENSIBLE 配置
 * @param dev 需要探测属性的 SoundIoDevicePrivate 实例
 * @param share_mode 指定测试是使用 shared 还是 exclusive mode
 * @return int 成功返回 0，错误返回对应 error code
 */
static int detect_valid_layouts(std::shared_ptr<RefreshDevices> rd, WAVEFORMATEXTENSIBLE* wave_format,
                                std::shared_ptr<SoundIoDevicePrivate> dev, AUDCLNT_SHAREMODE share_mode)
{
    std::shared_ptr<SoundIoDevice> device = dev;
    HRESULT hr;

    WAVEFORMATEX* closest_match = nullptr;
    WAVEFORMATEXTENSIBLE orig_wave_format = *wave_format;

    for (size_t i = 0; i < std::size(test_formats); i += 1)
    {
        enum SoundIoChannelLayoutId test_layout_id = test_layouts[i];
        const struct SoundIoChannelLayout* test_layout = soundio_channel_layout_get_builtin(test_layout_id);
        to_wave_format_layout(test_layout, wave_format);
        complete_wave_format_data(wave_format);

        hr = rd->audio_client->IsFormatSupported(share_mode, reinterpret_cast<WAVEFORMATEX*>(wave_format), &closest_match);
        if (closest_match)
        {
            CoTaskMemFree(closest_match);
            closest_match = nullptr;
        }
        if (hr == S_OK)
        {
            device->layouts.push_back(*test_layout);
        }
        else if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT || hr == S_FALSE || hr == E_INVALIDARG)
        {
            continue;
        }
        else
        {
            *wave_format = orig_wave_format;
            return SoundIoErrorOpeningDevice;
        }
    }

    *wave_format = orig_wave_format;
    return 0;
}

/**
 * @brief 探测设备支持的可行 sound formats
 * 
 * 遍历内部给定的 sound formats 列表并对指定的设备及 share_mode 进行检测，
 * 如果支持，就加入该 device 的可用 formats 列表。
 * 
 * @param rd RefreshDevices 实例的 shared_ptr
 * @param wave_format 指向当前 WAVEFORMATEXTENSIBLE 的指针
 * @param dev 需要更新参数的 SoundIoDevicePrivate
 * @param share_mode 指定验证所使用的共享模式
 * @return int 探测完成或错误时的对应 code
 */
static int detect_valid_formats(std::shared_ptr<RefreshDevices> rd, WAVEFORMATEXTENSIBLE* wave_format,
                                std::shared_ptr<SoundIoDevicePrivate> dev, AUDCLNT_SHAREMODE share_mode)
{
    std::shared_ptr<SoundIoDevice> device = dev;
    HRESULT hr;

    // device->format_count = 0;
    // device->formats = ALLOCATE(enum SoundIoFormat, ARRAY_LENGTH(test_formats));
    // if (!device->formats)
    //     return SoundIoErrorNoMem;

    WAVEFORMATEX* closest_match = nullptr;
    WAVEFORMATEXTENSIBLE orig_wave_format = *wave_format;

    for (size_t i = 0; i < std::size(test_formats); i += 1)
    {
        enum SoundIoFormat test_format = test_formats[i];
        to_wave_format_format(test_format, wave_format);
        complete_wave_format_data(wave_format);

        hr = rd->audio_client->IsFormatSupported(share_mode, (WAVEFORMATEX*) wave_format, &closest_match);
        if (closest_match)
        {
            CoTaskMemFree(closest_match);
            closest_match = nullptr;
        }
        if (hr == S_OK)
        {
            device->formats.push_back(test_format);
        }
        else if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT || hr == S_FALSE || hr == E_INVALIDARG)
        {
            continue;
        }
        else
        {
            *wave_format = orig_wave_format;
            return SoundIoErrorOpeningDevice;
        }
    }

    *wave_format = orig_wave_format;
    return 0;
}

static void add_sample_rate(std::vector<SoundIoSampleRateRange>& sample_rates, int current_min, int the_max)
{
    SoundIoSampleRateRange range = {};
    range.min = current_min;
    range.max = the_max;

    sample_rates.push_back(range);
}

static int do_sample_rate_test(std::shared_ptr<RefreshDevices> rd, std::shared_ptr<SoundIoDevicePrivate> dev,
                               WAVEFORMATEXTENSIBLE* wave_format, int test_sample_rate, AUDCLNT_SHAREMODE share_mode,
                               int* current_min, int* last_success_rate)
{
    WAVEFORMATEX* closest_match = nullptr;

    wave_format->Format.nSamplesPerSec = test_sample_rate;
    HRESULT hr = rd->audio_client->IsFormatSupported(share_mode, (WAVEFORMATEX*) wave_format, &closest_match);
    if (closest_match)
    {
        CoTaskMemFree(closest_match);
        closest_match = nullptr;
    }
    if (hr == S_OK)
    {
        if (*current_min == -1)
        {
            *current_min = test_sample_rate;
        }
        *last_success_rate = test_sample_rate;
    }
    else if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT || hr == S_FALSE || hr == E_INVALIDARG)
    {
        if (*current_min != -1)
        {
            add_sample_rate(dev->sample_rates, *current_min, *last_success_rate);
            *current_min = -1;
        }
    }
    else
    {
        return SoundIoErrorOpeningDevice;
    }

    return 0;
}

static int detect_valid_sample_rates(std::shared_ptr<RefreshDevices> rd, WAVEFORMATEXTENSIBLE* wave_format,
                                     std::shared_ptr<SoundIoDevicePrivate> dev, AUDCLNT_SHAREMODE share_mode)
{
    int err;

    DWORD orig_sample_rate = wave_format->Format.nSamplesPerSec;

    assert(dev->sample_rates.size() == 0);

    int current_min = -1;
    int last_success_rate = -1;
    for (size_t i = 0; i < std::size(test_sample_rates); i += 1)
    {
        for (int offset = -1; offset <= 1; offset += 1)
        {
            int test_sample_rate = test_sample_rates[i] + offset;
            if ((err = do_sample_rate_test(rd, dev, wave_format, test_sample_rate, share_mode, &current_min,
                                           &last_success_rate)))
            {
                wave_format->Format.nSamplesPerSec = orig_sample_rate;
                return err;
            }
        }
    }

    if (current_min != -1)
    {
        add_sample_rate(dev->sample_rates, current_min, last_success_rate);
    }

    wave_format->Format.nSamplesPerSec = orig_sample_rate;
    return 0;
}


/**
 * @brief 执行完整的设备枚举与属性更新操作
 * 
 * 获取目前所有的 audio render devices 和 capture devices，构建 collection。
 * 为每个有效的 device 挖掘相应的 formats、layouts 和 sample rates 等 properties。
 * 此操作可能很耗时，通常在 background thread 中运行。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @return int 完成设备的 refresh 则返回 0
 */
static int refresh_devices(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoWasapi& siw = si->backend_data->wasapi;
    std::shared_ptr<RefreshDevices> rd = std::make_shared<RefreshDevices>();
    int err;
    HRESULT hr;

    if (FAILED(hr = siw.device_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &rd->default_render_device)))
    {
        if (hr == E_OUTOFMEMORY)
        {
            return SoundIoErrorNoMem;
        }
        if (hr != E_NOTFOUND)
        {
            return SoundIoErrorOpeningDevice;
        }
    }

    if (rd->default_render_device)
    {
        LPWSTR lpwstr;
        if (FAILED(hr = rd->default_render_device->GetId(&lpwstr)))
        {
            // MSDN states the IMMDevice_GetId can fail if the device is nullptr, or if we're out of memory
            // We know the device point isn't nullptr so we're necessarily out of memory
            return SoundIoErrorNoMem;
        }
        rd->default_render_id = lpwstr;
        CoTaskMemFree(lpwstr);
        lpwstr = nullptr;
    }

    if (FAILED(hr = siw.device_enumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, &rd->default_capture_device)))
    {
        if (hr == E_OUTOFMEMORY)
        {
            return SoundIoErrorNoMem;
        }
        if (hr != E_NOTFOUND)
        {
            return SoundIoErrorOpeningDevice;
        }
    }

    if (rd->default_capture_device)
    {
        LPWSTR lpwstr;
        if (hr == E_OUTOFMEMORY)
        {
            return SoundIoErrorNoMem;
        }
        if (FAILED(hr = rd->default_capture_device->GetId(&lpwstr)))
        {
            return SoundIoErrorOpeningDevice;
        }
        rd->default_capture_id = lpwstr;
        CoTaskMemFree(lpwstr);
        lpwstr = nullptr;
    }


    if (FAILED(hr = siw.device_enumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE, &rd->collection)))
    {
        return hr == E_OUTOFMEMORY ? SoundIoErrorNoMem : SoundIoErrorOpeningDevice;
    }

    UINT unsigned_count;
    if (FAILED(hr = rd->collection->GetCount(&unsigned_count)))
    {
        // In theory this shouldn't happen since the only documented failure case is that
        // rd.collection is nullptr, but then EnumAudioEndpoints should have failed.
        return SoundIoErrorOpeningDevice;
    }

    if (unsigned_count > static_cast<UINT>(INT_MAX))
    {
        return SoundIoErrorIncompatibleDevice;
    }

    int device_count = unsigned_count;
    rd->devices_info = std::make_unique<SoundIoDevicesInfo>();
    rd->devices_info->default_input_index = -1;
    rd->devices_info->default_output_index = -1;

    for (int device_i = 0; device_i < device_count; device_i += 1)
    {
        rd->mm_device = nullptr;

        IMMDevice* device;
        if (FAILED(hr = rd->collection->Item(device_i, &device)))
        {
            continue;
        }

        rd->mm_device = std::shared_ptr<IMMDevice>(device, IMMDeviceDeleter());

        LPWSTR lpwstr;
        if (FAILED(hr = rd->mm_device->GetId(&lpwstr)))
        {
            continue;
        }

        // std::shared_ptr<SoundIoDeviceWasapi> dev_w_shared = dev_shared->backend_data.wasapi;
        assert(!rd->device_shared);

        std::shared_ptr<SoundIoDevicePrivate> shared_w = std::make_shared<SoundIoDevicePrivate>();

        rd->device_shared = shared_w;
        // rd->device_shared->ref_count = 1;
        rd->device_shared->soundio = si;
        rd->device_shared->is_raw = false;
        rd->device_shared->software_latency_max = 2.0;

        assert(!rd->device_raw);
        std::shared_ptr<SoundIoDevicePrivate> raw_w = std::make_shared<SoundIoDevicePrivate>();

        rd->device_raw = raw_w;
        // rd->device_raw->ref_count = 1;
        rd->device_raw->soundio = si;
        rd->device_raw->is_raw = true;
        rd->device_raw->software_latency_max = 0.5;

        rd->device_shared->id = lpwstr;
        rd->device_raw->id = rd->device_shared->id;
        CoTaskMemFree(lpwstr);
        lpwstr = nullptr;

        if (rd->endpoint)
        {
            rd->endpoint->Release();
            rd->endpoint = nullptr;
        }

        if (FAILED(hr = rd->mm_device->QueryInterface(IID_IMMENDPOINT, reinterpret_cast<void**>(&rd->endpoint))))
        {
            rd->device_shared->probe_error = SoundIoErrorOpeningDevice;
            rd->device_raw->probe_error = SoundIoErrorOpeningDevice;
            rd->device_shared = nullptr;
            rd->device_raw = nullptr;
            continue;
        }

        EDataFlow data_flow;
        if (FAILED(hr = rd->endpoint->GetDataFlow(&data_flow)))
        {
            rd->device_shared->probe_error = SoundIoErrorOpeningDevice;
            rd->device_raw->probe_error = SoundIoErrorOpeningDevice;
            rd->device_shared = nullptr;
            rd->device_raw = nullptr;
            continue;
        }

        rd->device_shared->aim = data_flow_to_aim(data_flow);
        rd->device_raw->aim = rd->device_shared->aim;

        std::vector<std::shared_ptr<SoundIoDevice>>* device_list;
        if (rd->device_shared->aim == SoundIoDeviceAimOutput)
        {
            device_list = &rd->devices_info->output_devices;
            if (rd->device_shared->id == rd->default_render_id)
            {
                rd->devices_info->default_output_index = device_list->size();
            }
        }
        else
        {
            assert(rd->device_shared->aim == SoundIoDeviceAimInput);
            device_list = &rd->devices_info->input_devices;
            if (rd->device_shared->id == rd->default_capture_id)
            {
                rd->devices_info->default_input_index = device_list->size();
            }
        }

        device_list->push_back(rd->device_shared);
        device_list->push_back(rd->device_raw);

        if (rd->audio_client)
        {
            rd->audio_client->Release();
            rd->audio_client = nullptr;
        }
        if (FAILED(hr = rd->mm_device->Activate(IID_IAUDIOCLIENT, CLSCTX_ALL, nullptr, (void**) &rd->audio_client)))
        {
            rd->device_shared->probe_error = SoundIoErrorOpeningDevice;
            rd->device_raw->probe_error = SoundIoErrorOpeningDevice;
            rd->device_shared = nullptr;
            rd->device_raw = nullptr;
            continue;
        }

        REFERENCE_TIME default_device_period;
        REFERENCE_TIME min_device_period;
        if (FAILED(hr = rd->audio_client->GetDevicePeriod(&default_device_period, &min_device_period)))
        {
            rd->device_shared->probe_error = SoundIoErrorOpeningDevice;
            rd->device_raw->probe_error = SoundIoErrorOpeningDevice;
            rd->device_shared = nullptr;
            rd->device_raw = nullptr;
            continue;
        }


        double time = from_reference_time(default_device_period);
        shared_w->backend_data.wasapi->period_duration = time;
        rd->device_shared->software_latency_current = time;

        time = from_reference_time(min_device_period);
        raw_w->backend_data.wasapi->period_duration = time;
        rd->device_raw->software_latency_min = time * 2;

        if (rd->prop_store)
        {
            rd->prop_store->Release();
            rd->prop_store = nullptr;
        }
        if (FAILED(hr = rd->mm_device->OpenPropertyStore(STGM_READ, &rd->prop_store)))
        {
            rd->device_shared->probe_error = SoundIoErrorOpeningDevice;
            rd->device_raw->probe_error = SoundIoErrorOpeningDevice;
            rd->device_shared = nullptr;
            rd->device_raw = nullptr;
            continue;
        }

        if (rd->prop_variant_value_inited)
        {
            PropVariantClear(&rd->prop_variant_value);
            rd->prop_variant_value_inited = false;
        }
        PropVariantInit(&rd->prop_variant_value);
        rd->prop_variant_value_inited = true;
        if (FAILED(hr = rd->prop_store->GetValue(PKEY_DEVICE_FRIENDLYNAME, &rd->prop_variant_value)))
        {
            rd->device_shared->probe_error = SoundIoErrorOpeningDevice;
            rd->device_raw->probe_error = SoundIoErrorOpeningDevice;
            rd->device_shared = nullptr;
            rd->device_raw = nullptr;
            continue;
        }
        if (!rd->prop_variant_value.pwszVal)
        {
            rd->device_shared->probe_error = SoundIoErrorOpeningDevice;
            rd->device_raw->probe_error = SoundIoErrorOpeningDevice;
            rd->device_shared = nullptr;
            rd->device_raw = nullptr;
            continue;
        }

        rd->device_shared->name = rd->prop_variant_value.pwszVal;
        rd->device_raw->name = rd->device_shared->name;

        // Get the format that WASAPI opens the device with for shared streams.
        // This is guaranteed to work, so we use this to modulate the sample
        // rate while holding the format constant and vice versa.
        if (rd->prop_variant_value_inited)
        {
            PropVariantClear(&rd->prop_variant_value);
            rd->prop_variant_value_inited = false;
        }
        PropVariantInit(&rd->prop_variant_value);
        rd->prop_variant_value_inited = true;
        if (FAILED(hr = rd->prop_store->GetValue(PKEY_AUDIOENGINE_DEVICEFORMAT, &rd->prop_variant_value)))
        {
            rd->device_shared->probe_error = SoundIoErrorOpeningDevice;
            rd->device_raw->probe_error = SoundIoErrorOpeningDevice;
            rd->device_shared = nullptr;
            rd->device_raw = nullptr;
            continue;
        }
        WAVEFORMATEXTENSIBLE* valid_wave_format = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(rd->prop_variant_value.blob.pBlobData);
        if (valid_wave_format->Format.wFormatTag != WAVE_FORMAT_EXTENSIBLE)
        {
            rd->device_shared->probe_error = SoundIoErrorOpeningDevice;
            rd->device_raw->probe_error = SoundIoErrorOpeningDevice;
            rd->device_shared = nullptr;
            rd->device_raw = nullptr;
            continue;
        }

        err = detect_valid_sample_rates(rd, valid_wave_format, raw_w, AUDCLNT_SHAREMODE_EXCLUSIVE);
        if (err != SoundIoErrorNone)
        {
            rd->device_raw->probe_error = err;
            rd->device_raw = nullptr;
        }
        if (rd->device_raw)
        {
            err = detect_valid_formats(rd, valid_wave_format, raw_w, AUDCLNT_SHAREMODE_EXCLUSIVE);
            if (err != SoundIoErrorNone)
            {
                rd->device_raw->probe_error = err;
                rd->device_raw = nullptr;
            }
        }
        if (rd->device_raw)
        {
            err = detect_valid_layouts(rd, valid_wave_format, raw_w, AUDCLNT_SHAREMODE_EXCLUSIVE);
            if (err != SoundIoErrorNone)
            {
                rd->device_raw->probe_error = err;
                rd->device_raw = nullptr;
            }
        }

        if (rd->wave_format)
        {
            CoTaskMemFree(rd->wave_format);
            rd->wave_format = nullptr;
        }
        if (FAILED(hr = rd->audio_client->GetMixFormat(reinterpret_cast<WAVEFORMATEX**>(&rd->wave_format))))
        {
            // According to MSDN GetMixFormat only applies to shared-mode devices.
            rd->device_shared->probe_error = SoundIoErrorOpeningDevice;
            rd->device_shared = nullptr;
        }
        else if (rd->wave_format && rd->wave_format->Format.wFormatTag != WAVE_FORMAT_EXTENSIBLE)
        {
            rd->device_shared->probe_error = SoundIoErrorOpeningDevice;
            rd->device_shared = nullptr;
        }

        if (rd->device_shared)
        {
            rd->device_shared->sample_rate_current = rd->wave_format->Format.nSamplesPerSec;
            rd->device_shared->current_format = from_wave_format_format(rd->wave_format);

            if (rd->device_shared->aim == SoundIoDeviceAimOutput)
            {
                // For output streams in shared mode,
                // WASAPI performs resampling, so any value is valid.
                // Let's pick some reasonable min and max values.
                // rd->device_shared->sample_rate_count = 1;
                SoundIoSampleRateRange range{};
                range.min = soundio_int_min(SOUNDIO_MIN_SAMPLE_RATE, rd->device_shared->sample_rate_current);
                range.max = soundio_int_max(SOUNDIO_MAX_SAMPLE_RATE, rd->device_shared->sample_rate_current);
                rd->device_shared->sample_rates.push_back(range);
            }
            else
            {
                // Shared mode input stream: mix format is all we can do.
                // rd->device_shared->sample_rate_count = 1;

                SoundIoSampleRateRange range{};
                range.min = rd->device_shared->sample_rate_current;
                range.max = rd->device_shared->sample_rate_current;
                rd->device_shared->sample_rates.push_back(range);
            }

            if ((err = detect_valid_formats(rd, rd->wave_format, shared_w, AUDCLNT_SHAREMODE_SHARED)))
            {
                rd->device_shared->probe_error = err;
                rd->device_shared = nullptr;
            }
            else
            {
                from_wave_format_layout(rd->wave_format, &rd->device_shared->current_layout);
                rd->device_shared->layouts.push_back(rd->device_shared->current_layout);
            }
        }

        shared_w->backend_data.wasapi->mm_device = rd->mm_device;
        raw_w->backend_data.wasapi->mm_device = rd->mm_device;

        rd->mm_device = nullptr;
        rd->device_shared = nullptr;
        rd->device_raw = nullptr;
    }

    std::unique_lock lock(siw.mutex->get());

    siw.ready_devices_info = std::move(rd->devices_info);
    siw.have_devices_flag = true;
    siw.cond->signal(&lock);
    si->on_events_signal(si);
    return 0;
}

// ==============================================================================
// 4. Backend Context & Event Dispatch
// ==============================================================================

/**
 * @brief 关闭正在运行的 backend 服务并分发错误事件
 * 
 * 用于在发生了不可恢复的错误后安全终止 background routines 和回调 error 函数。
 * 
 * @param si 当前的 SoundIoPrivate instance
 * @param err 触发 backend shutdown 的对应 error code
 */
static void shutdown_backend(std::shared_ptr<SoundIoPrivate> si, int err)
{
    SoundIoWasapi& siw = si->backend_data->wasapi;
    std::unique_lock lock(siw.mutex->get());

    siw.shutdown_err = err;
    siw.cond->signal(&lock);
    si->on_events_signal(si);
}


/**
 * @brief 内部 flush events 方法
 * 
 * 判断 events 当前的触发情况并将它们 emit，使用 mutex 同步设备 scan 的最新状态。
 * 
 * @param si 此前初始化的 SoundIoPrivate 对象
 * @param wait 指定如果没有 event 可用时是否需要进行 wait 阻塞
 */
static void my_flush_events(std::shared_ptr<SoundIoPrivate> si, bool wait)
{
    SoundIoWasapi& siw = si->backend_data->wasapi;

    bool change = false;
    bool cb_shutdown = false;

    std::unique_lock lock(siw.mutex->get());

    // block until have devices
    while (wait || (!siw.have_devices_flag && !siw.shutdown_err))
    {
        siw.cond->wait(&lock);
        wait = false;
    }

    if (siw.shutdown_err && !siw.emitted_shutdown_cb)
    {
        siw.emitted_shutdown_cb = true;
        cb_shutdown = true;
    }
    else if (siw.ready_devices_info)
    {
        si->safe_devices_info = std::move(siw.ready_devices_info);
        change = true;
    }

    lock.unlock();

    if (cb_shutdown)
    {
        si->on_backend_disconnect(si, siw.shutdown_err);
    }
    else if (change)
    {
        si->on_devices_change(si);
    }
}

/**
 * @brief 暴露给公共接口的非阻塞 event flush
 * 
 * 发送事件更新而不去强制 wait。
 * 
 * @param si SoundIoPrivate 初始化对象
 */
static void flush_events_wasapi(std::shared_ptr<SoundIoPrivate> si)
{
    my_flush_events(si, false);
}

static void wait_events_wasapi(std::shared_ptr<SoundIoPrivate> si)
{
    my_flush_events(si, true);
}


/**
 * @brief 专用于维护设备变化的 background thread 主循环
 * 
 * 监听 scan_devices_cond 并触发实际的 refresh_devices 以获取 devices 列表。
 * 
 * @param arg 用户传递的 SoundIoPrivate pointer 参数
 */
static void device_thread_run(std::shared_ptr<void> arg)
{
    std::shared_ptr<SoundIoPrivate> si = std::static_pointer_cast<SoundIoPrivate>(arg);
    SoundIoWasapi& siw = si->backend_data->wasapi;

    HRESULT hr = CoCreateInstance(CLSID_MMDEVICEENUMERATOR, nullptr, CLSCTX_ALL, IID_IMMDEVICEENUMERATOR, reinterpret_cast<void**>(&siw.device_enumerator));
    if (FAILED(hr))
    {
        shutdown_backend(si, SoundIoErrorSystemResources);
        return;
    }

    if (FAILED(hr = siw.device_enumerator->RegisterEndpointNotificationCallback(siw.device_events.get())))
    {
        shutdown_backend(si, SoundIoErrorSystemResources);
        return;
    }

    std::unique_lock lock(siw.scan_devices_mutex->get());

    while (siw.abort_flag.test())
    {
        if (siw.device_scan_queued)
        {
            siw.device_scan_queued = false;
            lock.unlock();
            if (int err = refresh_devices(si))
            {
                shutdown_backend(si, err);
                lock.lock();
                break;
            }
            flush_events_wasapi(si);
            lock.lock();
            continue;
        }
        siw.scan_devices_cond->wait(&lock);
    }
    lock.unlock();

    siw.device_enumerator->UnregisterEndpointNotificationCallback(siw.device_events.get());
    siw.device_enumerator->Release();
    siw.device_enumerator = nullptr;
}

/**
 * @brief 主动唤醒处于 wait state 的 WASAPI event loop
 * 
 * 给 cond 变量发送 signal，从而使得通过 wait_events_wasapi block 的 thread 获取执行权。
 * 
 * @param si 顶层的 SoundIoPrivate 的 shared_ptr
 */
static void wakeup_wasapi(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoWasapi& siw = si->backend_data->wasapi;
    std::unique_lock lock(siw.mutex->get());
    siw.cond->signal(&lock);
}

/**
 * @brief 强行触发设备的 scan 解析流程
 * 
 * 通知 background 的 device 处理 thread 认为存在 device change 并重新获取 endpoint list。
 * 
 * @param si 顶层的 SoundIoPrivate 的 shared_ptr
 */
static void force_device_scan_wasapi(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoWasapi& siw = si->backend_data->wasapi;

    std::unique_lock lock(siw.mutex->get());
    siw.have_devices_flag = false;
    lock.unlock();

    std::unique_lock scan_lock(siw.scan_devices_mutex->get());
    siw.device_scan_queued = true;
    siw.scan_devices_cond->signal(&scan_lock);
}

// ==============================================================================
// 5. OutStream (Output & Playback)
// ==============================================================================

/**
 * @brief 在 backend thread 关闭时执行的清理操作
 * 
 * 释放 audio_render_client, audio_client, clock 等 COM 对象资源。
 * 
 * @param si 当前 SoundIoPrivate 实例
 * @param os 输出流内部数据对象 SoundIoOutStreamPrivate
 */
static void outstream_thread_deinit(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;

    if (osw.audio_volume_control)
    {
        osw.audio_volume_control->Release();
        osw.audio_volume_control = nullptr;
    }
    if (osw.audio_render_client)
    {
        osw.audio_render_client->Release();
        osw.audio_render_client = nullptr;
    }
    if (osw.audio_session_control)
    {
        osw.audio_session_control->Release();
        osw.audio_session_control = nullptr;
    }
    if (osw.audio_clock_adjustment)
    {
        osw.audio_clock_adjustment->Release();
        osw.audio_clock_adjustment = nullptr;
    }
    if (osw.audio_clock)
    {
        osw.audio_clock->Release();
        osw.audio_clock = nullptr;
    }
    if (osw.audio_client)
    {
        osw.audio_client->Release();
        osw.audio_client = nullptr;
    }
}

/**
 * @brief 从主线程调用的输出流销毁逻辑
 * 
 * 设置 thread_exit_flag 以通知工作线程安全退出，随后清理系统分配的各类 events 和 sync primitives。
 * 
 * @param si 根级别 SoundIoPrivate context
 * @param os 即将销毁的输出流指针
 */
static void outstream_destroy_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    LOGI("destroy wasapi outstream");
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;

    if (osw.thread)
    {
        osw.thread_exit_flag.test_and_set();
        if (osw.h_event)
        {
            SetEvent(osw.h_event);
        }

        std::unique_lock lock(osw.mutex->get());
        osw.cond->signal(&lock);
        osw.start_cond->signal(&lock);
        lock.unlock();
        osw.thread = nullptr;
    }

    if (osw.h_event)
    {
        CloseHandle(osw.h_event);
        osw.h_event = nullptr;
    }

    osw.stream_name = std::wstring();
    osw.cond = nullptr;
    osw.start_cond = nullptr;
    osw.mutex = nullptr;
}

/**
 * @brief 尝试使用较低 latency 或 hardware 特性的 IAudioClient3 初始化并打开设备
 * 
 * 配置共享模式的 parameters 并在必要时设定 periodicity 属性。
 * 
 * @param os 输出流组件
 * @param wave_format 用户配置的音频波形格式
 * @return IAudioClient3* 如果创建失败则返回 nullptr
 */
static IAudioClient3* open_audio_client3(std::shared_ptr<SoundIoOutStreamPrivate> os, WAVEFORMATEXTENSIBLE* wave_format)
{
    IAudioClient3* audio_client3 = nullptr;
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;
    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(os->device);
    std::shared_ptr<SoundIoDeviceWasapi> dw = dev->backend_data.wasapi;

    WAVEFORMATEXTENSIBLE* mix_format;
    HRESULT hr;
    if (!osw.is_raw && FAILED(hr = dw->mm_device->Activate(IID_IAUDIOCLIENT3, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&audio_client3))))
    {
        audio_client3->Release();
        return nullptr;
    }

    if (!osw.is_raw)
    {
        if (FAILED(hr = audio_client3->GetMixFormat(reinterpret_cast<WAVEFORMATEX**>(&mix_format))))
        {
            audio_client3->Release();
            return nullptr;
        }

        auto need_resample = mix_format->Format.nSamplesPerSec != static_cast<DWORD>(os->sample_rate);
        if (need_resample)
        {
            // we can't use resampling with this new interface, fallback to old method
            CoTaskMemFree(mix_format);
            audio_client3->Release();
            return nullptr;
        }

        wave_format->Format.nSamplesPerSec = mix_format->Format.nSamplesPerSec;
        CoTaskMemFree(mix_format);
    }

    return audio_client3;
}

/**
 * @brief 传统版本 IAudioClient 实例的获取逻辑
 * 
 * 直接尝试激活对应的 device client。作为 client3 失败或不支持情况下的备用手段。
 * 
 * @param os 需要获取 client 实例的输出流对象
 * @param wave_format 指定的格式
 * @return IAudioClient* 返回激活的 client 组件，否则为空
 */
static IAudioClient* open_audio_client(std::shared_ptr<SoundIoOutStreamPrivate> os, WAVEFORMATEXTENSIBLE* wave_format)
{
    IAudioClient* audio_client = nullptr;
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;
    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(os->device);
    std::shared_ptr<SoundIoDeviceWasapi> dw = dev->backend_data.wasapi;
    HRESULT hr;

    WAVEFORMATEXTENSIBLE* mix_format;

    if (FAILED(hr = dw->mm_device->Activate(IID_IAUDIOCLIENT, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&audio_client))))
    {
        return nullptr;
    }

    if (!osw.is_raw)
    {
        if (FAILED(hr = audio_client->GetMixFormat(reinterpret_cast<WAVEFORMATEX**>(&mix_format))))
        {
            audio_client->Release();
            return nullptr;
        }
        // wave_format->Format.nSamplesPerSec = mix_format->Format.nSamplesPerSec;
        // outstream->sample_rate = mix_format->Format.nSamplesPerSec;
        osw.need_resample = mix_format->Format.nSamplesPerSec != static_cast<DWORD>(os->sample_rate);
        CoTaskMemFree(mix_format);
        mix_format = nullptr;
    }
    return audio_client;
}

/**
 * @brief 安全释放 mix format 结构体内存
 * 
 * 使用 CoTaskMemFree 进行对应的操作。
 * 
 * @param mix_format 从系统获取需要释放的格式配置的指针引用
 */
static void deinit_mix_format(WAVEFORMATEXTENSIBLE** mix_format)
{
    if (*mix_format != nullptr)
    {
        CoTaskMemFree(*mix_format);
        *mix_format = nullptr;
    }
}

/**
 * @brief 回退或中断时集中进行的 COM 组件释放操作
 * 
 * @param client3 audio client 3
 * @param client fallback 的传统 audio client 
 * @param os 输出流对象
 * @param wave_format 相关格式信息
 */
static void deinit_client3_and_callback(IAudioClient3** client3, IAudioClient** client, std::shared_ptr<SoundIoOutStreamPrivate> os, WAVEFORMATEXTENSIBLE* wave_format)
{
    if (*client3 != nullptr)
    {
        (*client3)->Release();
        *client3 = nullptr;
    }
    *client = open_audio_client(os, wave_format);
}


/**
 * @brief 为获取到的 audio_client 统一步骤调用 Initialize
 * 
 * 调用前根据所拥有的 client / client3 分别执行初始化，包括校验和重分配。
 * 
 * @param audio_client 指向 audio_client 的内存指针的引用
 * @param audio_client3 指向 audio_client3 的引用
 * @param os 流私有数据
 * @param wave_format 即将被初始化的对应 format 配置
 * @param flags 初始化修饰标识符
 * @param share_mode 指定访问时为 exclusive 或 shared
 * @param buffer_duration 期望的 buffer 大小时间长度
 * @param periodicity 请求的调度的 period
 * @return int 返回错误代码定义，如果是 OK 则返回 0
 */
static int Initialize(IAudioClient** audio_client, IAudioClient3** audio_client3, std::shared_ptr<SoundIoOutStreamPrivate> os, WAVEFORMATEXTENSIBLE* wave_format, DWORD flags, AUDCLNT_SHAREMODE share_mode, REFERENCE_TIME buffer_duration,
                      REFERENCE_TIME periodicity)
{
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;
    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(os->device);
    std::shared_ptr<SoundIoDeviceWasapi> dw = dev->backend_data.wasapi;
    HRESULT hr;

    if (*audio_client3 != nullptr)
    {
        if (osw.need_resample)
        {
            deinit_client3_and_callback(audio_client3, audio_client, os, wave_format);
            goto NEXT;
        }

        WAVEFORMATEXTENSIBLE* mix_format;
        if (FAILED(hr = (*audio_client3)->GetMixFormat(reinterpret_cast<WAVEFORMATEX**>(&mix_format))))
        {
            deinit_mix_format(&mix_format);
            deinit_client3_and_callback(audio_client3, audio_client, os, wave_format);
            goto NEXT;
        }

        UINT32 default_period;
        UINT32 fundamental_period;
        UINT32 min_period;
        UINT32 max_period;

        if (FAILED(hr = (*audio_client3)->GetSharedModeEnginePeriod(&mix_format->Format, &default_period, &fundamental_period, &min_period, &max_period)))
        {
            deinit_mix_format(&mix_format);
        }

        UINT32 periodicity_in_frames = default_period;
        if (os->software_latency < 1.0)
        {
            periodicity_in_frames = fundamental_period * static_cast<UINT32>(mix_format->Format.nSamplesPerSec * os->software_latency / fundamental_period);
            periodicity_in_frames = soundio_uint_clamp(min_period, periodicity_in_frames, max_period);
        }

        if (!osw.need_resample && SUCCEEDED(
                    hr = (*audio_client3)->InitializeSharedAudioStream(flags, periodicity_in_frames, reinterpret_cast<WAVEFORMATEX*>(wave_format), nullptr)))
        {
            deinit_mix_format(&mix_format);
            return SoundIoErrorNone;
        }
    }

NEXT:
    if (FAILED(hr = (*audio_client)->Initialize(share_mode, flags, buffer_duration, periodicity, reinterpret_cast<WAVEFORMATEX*>(wave_format), nullptr)))
    {
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
        {
            UINT32 buffer_size;

            if (FAILED(hr = (*audio_client)->GetBufferSize(&buffer_size)))
            {
                (*audio_client)->Release();
                *audio_client = nullptr;
                return SoundIoErrorOpeningDevice;
            }

            osw.buffer_frame_count = static_cast<int>(buffer_size);

            (*audio_client)->Release();
            *audio_client = nullptr;


            if (FAILED(hr = dw->mm_device->Activate(IID_IAUDIOCLIENT, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(audio_client))))
            {
                return SoundIoErrorOpeningDevice;
            }
            if (!osw.is_raw)
            {
                WAVEFORMATEXTENSIBLE* mix_format;
                if (FAILED(hr = (*audio_client)->GetMixFormat(reinterpret_cast<WAVEFORMATEX**>(&mix_format))))
                {
                    return SoundIoErrorOpeningDevice;
                }
                wave_format->Format.nSamplesPerSec = mix_format->Format.nSamplesPerSec;
                os->sample_rate = mix_format->Format.nSamplesPerSec;
                // osw->need_resample = mix_format->Format.nSamplesPerSec != wave_format->Format.nSamplesPerSec;

                deinit_mix_format(&mix_format);

                flags = (osw.need_resample ? AUDCLNT_STREAMFLAGS_RATEADJUST : 0) | AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
                to_wave_format_layout(&os->layout, wave_format);
                to_wave_format_format(os->format, wave_format);
                complete_wave_format_data(wave_format);
            }

            buffer_duration = to_reference_time(osw.buffer_frame_count / static_cast<double>(os->sample_rate));
            if (osw.is_raw)
                periodicity = buffer_duration;
            if (FAILED(hr = (*audio_client)->Initialize(share_mode, flags, buffer_duration, periodicity, reinterpret_cast<WAVEFORMATEX*>(wave_format), nullptr)))
            {
                if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT)
                {
                    return SoundIoErrorIncompatibleDevice;
                }
                if (hr == E_OUTOFMEMORY)
                {
                    return SoundIoErrorNoMem;
                }
                return SoundIoErrorOpeningDevice;
            }
        }
        else if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT)
        {
            return SoundIoErrorIncompatibleDevice;
        }
        else if (hr == E_OUTOFMEMORY)
        {
            return SoundIoErrorNoMem;
        }
        else
        {
            return SoundIoErrorOpeningDevice;
        }
    }
    return SoundIoErrorNone;
}


/**
 * @brief 处理底层打开输出流设备的实际逻辑
 * 
 * 此方法根据 shared 或者 raw 的配置以及是否指定 fallback，设置正确的 latency 和缓冲区。
 * 主要完成 WAVEFORMAT 的校准及 AUDIOCLIENT 的启动配置。
 * 
 * @param si 管理全局行为的 SoundIoPrivate 对象
 * @param os 需要被启动的输出流实例
 * @return int 如果无法符合设备支持则返回报错代码
 */
static int outstream_do_open(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;
    std::shared_ptr<SoundIoDevice> device = os->device;

    HRESULT hr;
    if (os->software_latency <= 0.0)
    {
        os->software_latency = 1.0;
    }
    os->software_latency = soundio_double_clamp(device->software_latency_min, os->software_latency, device->software_latency_max);
    AUDCLNT_SHAREMODE share_mode;
    DWORD flags;
    REFERENCE_TIME buffer_duration;
    REFERENCE_TIME periodicity;
    WAVEFORMATEXTENSIBLE wave_format = {0};
    wave_format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wave_format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wave_format.Format.nSamplesPerSec = os->sample_rate;

    IAudioClient3* audio_client3 = open_audio_client3(os, &wave_format);
    IAudioClient* audio_client = nullptr;

    if (audio_client3 == nullptr)
    {
        audio_client = open_audio_client(os, &wave_format);
        if (audio_client == nullptr)
        {
            return SoundIoErrorOpeningDevice;
        }
    }
    else
    {
        if (FAILED(hr = audio_client3->QueryInterface(IID_IAUDIOCLIENT, reinterpret_cast<void**>(&audio_client))))
        {
            audio_client3->Release();
            audio_client3 = nullptr;
            return SoundIoErrorOpeningDevice;
        }
    }


    if (osw.is_raw)
    {
        flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        share_mode = AUDCLNT_SHAREMODE_EXCLUSIVE;
        periodicity = to_reference_time(os->software_latency);
        buffer_duration = periodicity;
    }
    else
    {
        if (osw.need_resample)
        {
            flags = AUDCLNT_STREAMFLAGS_RATEADJUST |
                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                    AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        }
        else
        {
            flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        }
        share_mode = AUDCLNT_SHAREMODE_SHARED;
        periodicity = 0;
        buffer_duration = 0;
    }

    to_wave_format_layout(&os->layout, &wave_format);
    to_wave_format_format(os->format, &wave_format);
    complete_wave_format_data(&wave_format);

    int soundIoError = Initialize(&audio_client, &audio_client3, os, &wave_format, flags, share_mode, buffer_duration, periodicity);
    if (soundIoError > 0)
    {
        return soundIoError;
    }

    REFERENCE_TIME max_latency_ref_time;
    if (FAILED(hr = audio_client->GetStreamLatency(&max_latency_ref_time)))
    {
        return SoundIoErrorOpeningDevice;
    }

    double max_latency_sec = from_reference_time(max_latency_ref_time);
    osw.min_padding_frames = (max_latency_sec * os->sample_rate) + 0.5;

    UINT32 buffer_size;

    if (FAILED(hr = audio_client->GetBufferSize(&buffer_size)))
    {
        return SoundIoErrorOpeningDevice;
    }

    osw.buffer_frame_count = static_cast<int>(buffer_size);
    os->software_latency = osw.buffer_frame_count / static_cast<double>(os->sample_rate);

    if (FAILED(hr = audio_client->SetEventHandle(osw.h_event)))
    {
        return SoundIoErrorOpeningDevice;
    }

    if (osw.need_resample)
    {
        if (FAILED(hr = audio_client->GetService(IID_IAUDIOCLOCKADJUSTMENT, reinterpret_cast<void**>(&osw.audio_clock_adjustment))))
        {
            return SoundIoErrorOpeningDevice;
        }

        // if (FAILED(hr = IAudioClockAdjustment_SetSampleRate(osw->audio_clock_adjustment, outstream->sample_rate)))
        // {
        //     return SoundIoErrorOpeningDevice;
        // }
    }

    if (!os->name.empty())
    {
        if (FAILED(hr = audio_client->GetService(IID_IAUDIOSESSIONCONTROL, reinterpret_cast<void**>(&osw.audio_session_control))))
        {
            return SoundIoErrorOpeningDevice;
        }

        osw.stream_name = os->name;
        if (FAILED(hr = osw.audio_session_control->SetDisplayName(osw.stream_name.c_str(), nullptr)))
        {
            return SoundIoErrorOpeningDevice;
        }
    }

    if (FAILED(hr = audio_client->GetService(IID_IAUDIORENDERCLIENT, reinterpret_cast<void**>(&osw.audio_render_client))))
    {
        return SoundIoErrorOpeningDevice;
    }

    if (FAILED(hr = audio_client->GetService(IID_IAUDIOCLOCK, reinterpret_cast<void**>(&osw.audio_clock))))
    {
        return SoundIoErrorOpeningDevice;
    }

    if (FAILED(hr = audio_client->GetService(IID_ISIMPLEAUDIOVOLUME, reinterpret_cast<void**>(&osw.audio_volume_control))))
    {
        return SoundIoErrorOpeningDevice;
    }

    if (FAILED(hr = osw.audio_volume_control->GetMasterVolume(&os->volume)))
    {
        return SoundIoErrorOpeningDevice;
    }

    osw.audio_client = audio_client;
    return 0;
}

/**
 * @brief WASAPI 输出流的 Shared mode event loop 执行框架
 * 
 * 依照设备的调度频率计算 buffer 可用的 frames，并唤醒 user thread 补充音频数据。
 * 处理 play state, pause state 及 clear events。
 * 
 * @param os 被驱动处理的 stream
 */
static void outstream_shared_run(std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;

    HRESULT hr;

    UINT32 frames_used;
    if (FAILED(hr = osw.audio_client->GetCurrentPadding(&frames_used)))
    {
        os->error_callback(os, SoundIoErrorStreaming);
        return;
    }
    osw.writable_frame_count = osw.buffer_frame_count - static_cast<int>(frames_used);
    if (osw.writable_frame_count <= 0)
    {
        os->error_callback(os, SoundIoErrorStreaming);
        return;
    }
    int frame_count_min = soundio_int_max(0, osw.min_padding_frames - static_cast<int>(frames_used));
    os->write_callback(os, frame_count_min, osw.writable_frame_count);

    if (FAILED(hr = osw.audio_client->Start()))
    {
        os->error_callback(os, SoundIoErrorStreaming);
        return;
    }

    for (;;)
    {
        WaitForSingleObject(osw.h_event, INFINITE);
        if (FAILED(hr = osw.audio_client->GetCurrentPadding(&frames_used)))
        {
            os->error_callback(os, SoundIoErrorStreaming);
            return;
        }
        osw.writable_frame_count = osw.buffer_frame_count - frames_used;
        if (osw.thread_exit_flag.test())
        {
            return;
        }
        if (osw.pause_resume_flag.test())
        {
            osw.pause_resume_flag.clear();
            bool pause = osw.desired_pause_state.load();
            if (pause && !osw.is_paused)
            {
                if (FAILED(hr = osw.audio_client->Stop()))
                {
                    os->error_callback(os, SoundIoErrorStreaming);
                    return;
                }
                osw.is_paused = true;
            }
            else if (!pause && osw.is_paused)
            {
                if (FAILED(hr = osw.audio_client->Start()))
                {
                    os->error_callback(os, SoundIoErrorStreaming);
                    return;
                }
                osw.is_paused = false;
            }
        }

        if (frames_used == 0 && !osw.is_paused)
        {
            os->underflow_callback(os);
        }

        frame_count_min = soundio_uint_max(0U, osw.min_padding_frames - frames_used);
        os->write_callback(os, frame_count_min, osw.writable_frame_count);
    }
}

/**
 * @brief WASAPI 输出流的 Raw mode (Exclusive) 事件循环
 * 
 * 无中介直写给硬件，确保最小 latency，等待事件触发完成 frame 处理。
 * 
 * @param os 需要驱动操作的流对象引用
 */
static void outstream_raw_run(std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;

    HRESULT hr;

    os->write_callback(os, osw.buffer_frame_count, osw.buffer_frame_count);

    if (FAILED(hr = osw.audio_client->Start()))
    {
        os->error_callback(os, SoundIoErrorStreaming);
        return;
    }

    for (;;)
    {
        WaitForSingleObject(osw.h_event, INFINITE);
        if (osw.thread_exit_flag.test())
        {
            return;
        }
        if (osw.pause_resume_flag.test())
        {
            osw.pause_resume_flag.clear();
            bool pause = osw.desired_pause_state.load();
            if (pause && !osw.is_paused)
            {
                if (FAILED(hr = osw.audio_client->Stop()))
                {
                    os->error_callback(os, SoundIoErrorStreaming);
                    return;
                }
                osw.is_paused = true;
            }
            else if (!pause && osw.is_paused)
            {
                if (FAILED(hr = osw.audio_client->Start()))
                {
                    os->error_callback(os, SoundIoErrorStreaming);
                    return;
                }
                osw.is_paused = false;
            }
        }

        os->write_callback(os, osw.buffer_frame_count, osw.buffer_frame_count);
    }
}

/**
 * @brief OutStream 的 background thread 入口点
 * 
 * 执行前期做必要的 setup 并向主线程反馈结果，随后依照 device mode 进入具体的 run-loop 流程。
 * 
 * @param arg 从底层传递入内部的 thread argument，代表 stream instance
 */
static void outstream_thread_run(std::shared_ptr<void> arg)
{
    std::shared_ptr<SoundIoOutStreamPrivate> os = std::static_pointer_cast<SoundIoOutStreamPrivate>(arg);
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;
    std::shared_ptr<SoundIo> soundio = os->device->soundio.lock();
    std::shared_ptr<SoundIoPrivate> si = std::dynamic_pointer_cast<SoundIoPrivate>(soundio);

    if (const int err = outstream_do_open(si, os))
    {
        outstream_thread_deinit(si, os);
        std::unique_lock lock(osw.mutex->get());

        osw.open_err = err;
        osw.open_complete = true;
        osw.cond->signal(&lock);
        return;
    }

    std::unique_lock lock(osw.mutex->get());
    osw.open_complete = true;
    osw.cond->signal(&lock);
    for (;;)
    {
        if (osw.thread_exit_flag.test())
        {
            outstream_thread_deinit(si, os);
            return;
        }
        if (osw.started)
        {
            break;
        }
        osw.start_cond->wait(&lock);
    }

    lock.unlock();

    if (osw.is_raw)
    {
        outstream_raw_run(os);
    }
    else
    {
        outstream_shared_run(os);
    }

    outstream_thread_deinit(si, os);
}

/**
 * @brief 打开 WASAPI OutStream (Output & Playback)
 * 
 * 初始化 WASAPI 的 playback 功能。包括创建 conditions、mutexes 以及启动 backend thread。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param os SoundIoOutStreamPrivate 的 shared_ptr
 * @return int 返回错误码，成功则返回 0
 */
static int outstream_open_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    LOGI("open wasapi outstream");
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;
    std::shared_ptr<SoundIoDevice> device = os->device;
    std::shared_ptr<SoundIo> soundio = si;

    osw.pause_resume_flag.clear();
    osw.clear_buffer_flag.test_and_set();
    osw.desired_pause_state.store(false);

    // All the COM functions are supposed to be called from the same thread. libsoundio API does not
    // restrict the calling thread context in this way. Furthermore, the user might have called
    // CoInitializeEx with a different threading model than Single Threaded Apartment.
    // So we create a thread to do all the initialization and teardown, and communicate state
    // via conditions and signals. The thread for initialization and teardown is also used
    // for the realtime code calls the user write_callback.

    osw.is_raw = device->is_raw;

    if (!(osw.cond = soundio_os_cond_create()))
    {
        outstream_destroy_wasapi(si, os);
        return SoundIoErrorNoMem;
    }

    if (!(osw.start_cond = soundio_os_cond_create()))
    {
        outstream_destroy_wasapi(si, os);
        return SoundIoErrorNoMem;
    }

    if (!(osw.mutex = soundio_os_mutex_create()))
    {
        outstream_destroy_wasapi(si, os);
        return SoundIoErrorNoMem;
    }

    osw.h_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!osw.h_event)
    {
        outstream_destroy_wasapi(si, os);
        return SoundIoErrorOpeningDevice;
    }

    osw.thread_exit_flag.clear();
    osw.thread = SoundIoOsThread::create(outstream_thread_run, os);

    std::unique_lock lock(osw.mutex->get());
    while (!osw.open_complete)
    {
        osw.cond->wait(&lock);
    }
    lock.unlock();

    if (osw.open_err)
    {
        outstream_destroy_wasapi(si, os);
        return osw.open_err;
    }

    os->paused = false;

    return 0;
}

/**
 * @brief 暂停或恢复 WASAPI OutStream 的 playback
 * 
 * 设置 desired_pause_state 标志位并发送 signal，等待 background thread 处理流的 stop 或 start。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param os SoundIoOutStreamPrivate 的 shared_ptr
 * @param pause 为 true 则暂停，false 则恢复
 * @return int 成功返回 0
 */
static int outstream_pause_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, bool pause)
{
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;
    os->paused = pause;

    osw.desired_pause_state.store(pause);
    osw.pause_resume_flag.test_and_set();
    if (osw.h_event)
    {
        SetEvent(osw.h_event);
    }
    else
    {
        std::unique_lock lock(osw.mutex->get());
        osw.cond->signal(&lock);
    }

    return 0;
}

/**
 * @brief 启动 WASAPI OutStream 流处理
 * 
 * 向 background thread 发送 signal 等待它开始推送 output frames。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param os SoundIoOutStreamPrivate 的 shared_ptr
 * @return int 成功返回 0
 */
static int outstream_start_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;

    std::unique_lock lock(osw->mutex->get());
    osw->started = true;
    osw->start_cond->signal(&lock);
    return 0;
}

/**
 * @brief 准备向 WASAPI OutStream 写入音频 frames
 * 
 * 从 audio_render_client 获取指定 frame_count 大小的 buffer 以供写入。
 * 并初始化各个 channel 的 layout area 供上层填充数据。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param os SoundIoOutStreamPrivate 的 shared_ptr
 * @param out_areas 返回被成功映射的音频数据区域通道 layout
 * @param frame_count 希望写入的 frame 数量
 * @return int 成功返回 0
 */
static int outstream_begin_write_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, SoundIoChannelArea** out_areas, int* frame_count)
{
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;
    HRESULT hr;

    osw.write_frame_count = *frame_count;


    char* data;
    if (FAILED(hr = osw.audio_render_client->GetBuffer(osw.write_frame_count, reinterpret_cast<BYTE**>(&data))))
    {
        return SoundIoErrorStreaming;
    }

    for (int ch = 0; ch < os->layout.channel_count; ch += 1)
    {
        osw.areas[ch].ptr = data + ch * os->bytes_per_sample;
        osw.areas[ch].step = os->bytes_per_frame;
    }

    *out_areas = osw.areas;

    return 0;
}

/**
 * @brief 结束在 WASAPI OutStream 的写入操作
 * 
 * 调用 ReleaseBuffer 将填充好的数据提交给 WASAPI render 引擎。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param os SoundIoOutStreamPrivate 的 shared_ptr
 * @return int 成功返回 0
 */
static int outstream_end_write_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;
    HRESULT hr;
    if (FAILED(hr = osw.audio_render_client->ReleaseBuffer(osw.write_frame_count, 0)))
    {
        return SoundIoErrorStreaming;
    }
    return 0;
}

/**
 * @brief 清除 WASAPI OutStream 的内部 buffer 数据
 * 
 * 仅对 shared mode 有效。设置 clear_buffer_flag 以清空剩余等待播放的 samples。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param os SoundIoOutStreamPrivate 的 shared_ptr
 * @return int 成功返回 0，如果在 raw 模式则返回错误码
 */
static int outstream_clear_buffer_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;

    if (osw.h_event)
    {
        return SoundIoErrorIncompatibleDevice;
    }
    osw.clear_buffer_flag.clear();
    std::unique_lock lock(osw.mutex->get());
    osw.cond->signal(&lock);

    return 0;
}

/**
 * @brief 获取 WASAPI OutStream 当前的软件 latency
 * 
 * 查询 audio_client 中等待发送的 frame 数量，并转化为秒数。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param os SoundIoOutStreamPrivate 的 shared_ptr
 * @param out_latency 返回当前的 latency
 * @return int 成功返回 0
 */
static int outstream_get_latency_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, double* out_latency)
{
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;

    HRESULT hr;
    UINT32 frames_used;
    if (FAILED(hr = osw.audio_client->GetCurrentPadding(&frames_used)))
    {
        return SoundIoErrorStreaming;
    }

    *out_latency = frames_used / static_cast<double>(os->sample_rate);
    return 0;
}

/**
 * @brief 获取 WASAPI OutStream 关联的 time clock 当前的播放时间
 * 
 * 通过 audio_clock 接口获取设备级别的主时钟位置。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param os SoundIoOutStreamPrivate 的 shared_ptr
 * @param out_time 输出当前播放时间的时间戳参数 (精确到秒)
 * @return int 成功返回 0，未找到时间戳信息则返回 -1
 */
static int outstream_get_time_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, double* out_time)
{
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;

    UINT64 position = 0;
    UINT64 qpc = 0;

    if (SUCCEEDED(osw.audio_clock->GetPosition(&position, &qpc)))
    {
        UINT64 freq = 0;
        if (SUCCEEDED(osw.audio_clock->GetFrequency(&freq) && freq > 0))
        {
            *out_time = static_cast<double>(position) / static_cast<double>(freq);
            return 0;
        }
    }
    *out_time = 0;
    return -1;
}

/**
 * @brief 配置 WASAPI OutStream 特定的 volume 增益水平
 * 
 * 使用 audio_volume_control 配置该 stream 的 master volume 进行全局缩放控制。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param os SoundIoOutStreamPrivate 的 shared_ptr
 * @param volume 范围应在 0.0 ~ 1.0 的音量倍数
 * @return int 成功返回 0
 */
static int outstream_set_volume_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, float volume)
{
    SoundIoOutStreamWasapi& osw = os->backend_data.wasapi;

    HRESULT hr;
    if (FAILED(hr = osw.audio_volume_control->SetMasterVolume(volume, nullptr)))
    {
        return SoundIoErrorIncompatibleDevice;
    }

    os->volume = volume;
    return 0;
}

// ==============================================================================
// 6. InStream (Input & Capture)
// ==============================================================================

static void instream_thread_deinit(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    struct SoundIoInStreamWasapi* isw = &is->backend_data.wasapi;

    if (isw->audio_capture_client)
    {
        isw->audio_capture_client->Release();
        isw->audio_capture_client = nullptr;
    }
    if (isw->audio_client)
    {
        isw->audio_client->Release();
        isw->audio_client = nullptr;
    }
}


static void instream_destroy_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    struct SoundIoInStreamWasapi* isw = &is->backend_data.wasapi;

    if (isw->thread)
    {
        isw->thread_exit_flag.test_and_set();
        if (isw->h_event)
        {
            SetEvent(isw->h_event);
        }

        std::unique_lock lock(isw->mutex->get());
        isw->cond->signal(&lock);
        isw->start_cond->signal(&lock);
        lock.unlock();

        isw->thread = nullptr;
    }

    if (isw->h_event)
    {
        CloseHandle(isw->h_event);
        isw->h_event = nullptr;
    }

    isw->cond = nullptr;
    isw->start_cond = nullptr;
    isw->mutex = nullptr;
}

static int instream_do_open(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    SoundIoInStreamWasapi& isw = is->backend_data.wasapi;
    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(is->device);
    std::shared_ptr<SoundIoDeviceWasapi> dw = dev->backend_data.wasapi;
    HRESULT hr;

    if (FAILED(hr = dw->mm_device->Activate(IID_IAUDIOCLIENT, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&isw.audio_client))))
    {
        return SoundIoErrorOpeningDevice;
    }

    AUDCLNT_SHAREMODE share_mode;
    DWORD flags;
    REFERENCE_TIME buffer_duration;
    REFERENCE_TIME periodicity;
    WAVEFORMATEXTENSIBLE wave_format = {0};
    wave_format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wave_format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    if (isw.is_raw)
    {
        wave_format.Format.nSamplesPerSec = is->sample_rate;
        flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        share_mode = AUDCLNT_SHAREMODE_EXCLUSIVE;
        periodicity = to_reference_time(dw->period_duration);
        buffer_duration = periodicity;
    }
    else
    {
        WAVEFORMATEXTENSIBLE* mix_format;
        if (FAILED(hr = isw.audio_client->GetMixFormat(reinterpret_cast<WAVEFORMATEX**>(&mix_format))))
        {
            return SoundIoErrorOpeningDevice;
        }
        wave_format.Format.nSamplesPerSec = mix_format->Format.nSamplesPerSec;
        CoTaskMemFree(mix_format);
        mix_format = nullptr;
        if (wave_format.Format.nSamplesPerSec != static_cast<DWORD>(is->sample_rate))
        {
            return SoundIoErrorIncompatibleDevice;
        }
        flags = 0;
        share_mode = AUDCLNT_SHAREMODE_SHARED;
        periodicity = 0;
        buffer_duration = to_reference_time(4.0);
    }
    to_wave_format_layout(&is->layout, &wave_format);
    to_wave_format_format(is->format, &wave_format);
    complete_wave_format_data(&wave_format);

    if (FAILED(hr = isw.audio_client->Initialize(share_mode, flags, buffer_duration, periodicity, reinterpret_cast<WAVEFORMATEX*>(&wave_format), nullptr)))
    {
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
        {
            if (FAILED(hr = isw.audio_client->GetBufferSize(&isw.buffer_frame_count)))
            {
                return SoundIoErrorOpeningDevice;
            }
            isw.audio_client->Release();
            isw.audio_client = nullptr;
            if (FAILED(hr = dw->mm_device->Activate(IID_IAUDIOCLIENT, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&isw.audio_client))))
            {
                return SoundIoErrorOpeningDevice;
            }
            if (!isw.is_raw)
            {
                WAVEFORMATEXTENSIBLE* mix_format;
                if (FAILED(hr = isw.audio_client->GetMixFormat(reinterpret_cast<WAVEFORMATEX**>(&mix_format))))
                {
                    return SoundIoErrorOpeningDevice;
                }
                wave_format.Format.nSamplesPerSec = mix_format->Format.nSamplesPerSec;
                CoTaskMemFree(mix_format);
                mix_format = nullptr;
                flags = 0;
                to_wave_format_layout(&is->layout, &wave_format);
                to_wave_format_format(is->format, &wave_format);
                complete_wave_format_data(&wave_format);
            }

            buffer_duration = to_reference_time(isw.buffer_frame_count / static_cast<double>(is->sample_rate));
            if (isw.is_raw)
                periodicity = buffer_duration;
            if (FAILED(hr = isw.audio_client->Initialize(share_mode, flags, buffer_duration, periodicity, reinterpret_cast<WAVEFORMATEX*>(&wave_format), nullptr)))
            {
                if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT)
                {
                    return SoundIoErrorIncompatibleDevice;
                }
                else if (hr == E_OUTOFMEMORY)
                {
                    return SoundIoErrorNoMem;
                }
                else
                {
                    return SoundIoErrorOpeningDevice;
                }
            }
        }
        else if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT)
        {
            return SoundIoErrorIncompatibleDevice;
        }
        else if (hr == E_OUTOFMEMORY)
        {
            return SoundIoErrorNoMem;
        }
        else
        {
            return SoundIoErrorOpeningDevice;
        }
    }
    if (FAILED(hr = isw.audio_client->GetBufferSize(&isw.buffer_frame_count)))
    {
        return SoundIoErrorOpeningDevice;
    }
    if (is->software_latency == 0.0)
        is->software_latency = 1.0;
    is->software_latency = soundio_double_clamp(dev->software_latency_min, is->software_latency, dev->software_latency_max);
    if (isw.is_raw)
    {
        is->software_latency = isw.buffer_frame_count / static_cast<double>(is->sample_rate);
    }

    if (isw.is_raw)
    {
        if (FAILED(hr = isw.audio_client->SetEventHandle(isw.h_event)))
        {
            return SoundIoErrorOpeningDevice;
        }
    }

    if (!is->name.empty())
    {
        if (FAILED(hr = isw.audio_client->GetService(IID_IAUDIOSESSIONCONTROL, reinterpret_cast<void**>(&isw.audio_session_control))))
        {
            return SoundIoErrorOpeningDevice;
        }

        isw.stream_name = is->name;
        if (FAILED(hr = isw.audio_session_control->SetDisplayName(isw.stream_name.c_str(), nullptr)))
        {
            return SoundIoErrorOpeningDevice;
        }
    }

    if (FAILED(hr = isw.audio_client->GetService(IID_IAUDIOCAPTURECLIENT, reinterpret_cast<void**>(&isw.audio_capture_client))))
    {
        return SoundIoErrorOpeningDevice;
    }

    return 0;
}

static void instream_raw_run(std::shared_ptr<SoundIoInStreamPrivate> is)
{
    SoundIoInStreamWasapi& isw = is->backend_data.wasapi;

    HRESULT hr;

    if (FAILED(hr = isw.audio_client->Start()))
    {
        is->error_callback(is, SoundIoErrorStreaming);
        return;
    }

    for (;;)
    {
        WaitForSingleObject(isw.h_event, INFINITE);
        if (isw.thread_exit_flag.test())
        {
            return;
        }

        is->read_callback(is, isw.buffer_frame_count, isw.buffer_frame_count);
    }
}

static void instream_shared_run(std::shared_ptr<SoundIoInStreamPrivate> is)
{
    SoundIoInStreamWasapi& isw = is->backend_data.wasapi;

    HRESULT hr;

    if (FAILED(hr = isw.audio_client->Start()))
    {
        is->error_callback(is, SoundIoErrorStreaming);
        return;
    }

    for (;;)
    {
        WaitForSingleObject(isw.h_event, INFINITE);
        if (isw.thread_exit_flag.test())
        {
            return;
        }

        UINT32 frames_available;
        if (FAILED(hr = isw.audio_client->GetCurrentPadding(&frames_available)))
        {
            is->error_callback(is, SoundIoErrorStreaming);
            return;
        }

        isw.readable_frame_count = frames_available;
        if (isw.readable_frame_count > 0)
        {
            is->read_callback(is, 0, isw.readable_frame_count);
        }
    }
}

static void instream_thread_run(std::shared_ptr<void> arg)
{
    std::shared_ptr<SoundIoInStreamPrivate> is = std::static_pointer_cast<SoundIoInStreamPrivate>(arg);
    SoundIoInStreamWasapi& isw = is->backend_data.wasapi;
    std::shared_ptr<SoundIoDevice> device = is->device;
    std::shared_ptr<SoundIo> soundio = device->soundio.lock();
    std::shared_ptr<SoundIoPrivate> si = std::dynamic_pointer_cast<SoundIoPrivate>(soundio);

    int err;
    if ((err = instream_do_open(si, is)))
    {
        instream_thread_deinit(si, is);

        std::unique_lock lock(isw.mutex->get());

        isw.open_err = err;
        isw.open_complete = true;
        isw.cond->signal(&lock);
        return;
    }

    std::unique_lock lock(isw.mutex->get());
    isw.open_complete = true;
    isw.cond->signal(&lock);
    for (;;)
    {
        if (isw.thread_exit_flag.test())
        {
            return;
        }
        if (isw.started)
        {
            break;
        }
        isw.start_cond->wait(&lock);
    }

    lock.unlock();

    if (isw.is_raw)
    {
        instream_raw_run(is);
    }
    else
    {
        instream_shared_run(is);
    }

    instream_thread_deinit(si, is);
}

/**
 * @brief 打开 WASAPI InStream (Input & Capture)
 * 
 * 初始化 WASAPI 采集流，创建相关的 conditions, mutexes 和 thread_exit_flag。
 * 如果 device 支持 raw mode，会配置对应的 event handle。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param is SoundIoInStreamPrivate 的 shared_ptr
 * @return int 返回错误码，成功则返回 0
 */
static int instream_open_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    SoundIoInStreamWasapi& isw = is->backend_data.wasapi;
    std::shared_ptr<SoundIoDevice> device = is->device;
    std::shared_ptr<SoundIo> soundio = si;

    // All the COM functions are supposed to be called from the same thread. libsoundio API does not
    // restrict the calling thread context in this way. Furthermore, the user might have called
    // CoInitializeEx with a different threading model than Single Threaded Apartment.
    // So we create a thread to do all the initialization and teardown, and communicate state
    // via conditions and signals. The thread for initialization and teardown is also used
    // for the realtime code calls the user write_callback.

    isw.is_raw = device->is_raw;

    if (!(isw.cond = soundio_os_cond_create()))
    {
        instream_destroy_wasapi(si, is);
        return SoundIoErrorNoMem;
    }

    if (!(isw.start_cond = soundio_os_cond_create()))
    {
        instream_destroy_wasapi(si, is);
        return SoundIoErrorNoMem;
    }

    if (!(isw.mutex = soundio_os_mutex_create()))
    {
        instream_destroy_wasapi(si, is);
        return SoundIoErrorNoMem;
    }

    if (isw.is_raw)
    {
        isw.h_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!isw.h_event)
        {
            instream_destroy_wasapi(si, is);
            return SoundIoErrorOpeningDevice;
        }
    }

    isw.thread_exit_flag.clear();
    isw.thread = SoundIoOsThread::create(instream_thread_run, is);

    std::unique_lock lock(isw.mutex->get());
    while (!isw.open_complete)
    {
        isw.cond->wait(&lock);
    }
    lock.unlock();

    if (isw.open_err)
    {
        instream_destroy_wasapi(si, is);
        return isw.open_err;
    }

    return 0;
}

/**
 * @brief 暂停或恢复 WASAPI InStream
 * 
 * 根据传入的 pause 状态调用 audio_client 的 Stop 或 Start 方法实现流控制。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param is SoundIoInStreamPrivate 的 shared_ptr
 * @param pause 若为 true 则暂停，false 则恢复
 * @return int 返回操作结果，成功为 0
 */
static int instream_pause_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is, bool pause)
{
    SoundIoInStreamWasapi& isw = is->backend_data.wasapi;
    HRESULT hr;
    if (pause && !isw.is_paused)
    {
        if (FAILED(hr = isw.audio_client->Stop()))
        {
            return SoundIoErrorStreaming;
        }
        isw.is_paused = true;
    }
    else if (!pause && isw.is_paused)
    {
        if (FAILED(hr = isw.audio_client->Start()))
        {
            return SoundIoErrorStreaming;
        }
        isw.is_paused = false;
    }
    return 0;
}

/**
 * @brief 启动 WASAPI InStream 流处理
 * 
 * 发送 signal 唤醒工作 thread 以开始捕获音频数据。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param is SoundIoInStreamPrivate 的 shared_ptr
 * @return int 成功返回 0
 */
static int instream_start_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    SoundIoInStreamWasapi& isw = is->backend_data.wasapi;
    std::unique_lock lock(isw.mutex->get());
    isw.started = true;
    isw.start_cond->signal(&lock);
    return 0;
}

/**
 * @brief 开始从 WASAPI InStream 读取音频 frames
 * 
 * 请求 audio_capture_client 提供具有指定 frame_count 大小的 buffer 进行读取操作。
 * 如果检测到 AUDCLNT_BUFFERFLAGS_SILENT，则数据置空表示静音。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param is SoundIoInStreamPrivate 的 shared_ptr
 * @param out_areas 接收音频数据的通道区域指针
 * @param frame_count 期望读取的 frame 数量，在此方法中可能会被修改为实际可用数量
 * @return int 成功返回 0
 */
static int instream_begin_read_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is,
                                      struct SoundIoChannelArea** out_areas, int* frame_count)
{
    SoundIoInStreamWasapi& isw = is->backend_data.wasapi;
    HRESULT hr;

    if (isw.read_buf_frames_left <= 0)
    {
        UINT32 frames_to_read;
        DWORD flags;
        if (FAILED(hr = isw.audio_capture_client->GetBuffer(reinterpret_cast<BYTE**>(&isw.read_buf), &frames_to_read, &flags, nullptr, nullptr)))
        {
            return SoundIoErrorStreaming;
        }
        isw.opened_buf_frames = static_cast<int>(frames_to_read);
        isw.read_buf_frames_left = static_cast<int>(frames_to_read);

        if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
        {
            isw.read_buf = nullptr;
        }
    }

    isw.read_frame_count = soundio_int_min(*frame_count, isw.read_buf_frames_left);
    *frame_count = isw.read_frame_count;

    if (isw.read_buf)
    {
        for (int ch = 0; ch < is->layout.channel_count; ch += 1)
        {
            isw.areas[ch].ptr = isw.read_buf + ch * is->bytes_per_sample;
            isw.areas[ch].step = is->bytes_per_frame;

            isw.areas[ch].ptr += is->bytes_per_frame * (isw.opened_buf_frames - isw.read_buf_frames_left);
        }

        *out_areas = isw.areas;
    }
    else
    {
        *out_areas = nullptr;
    }

    return 0;
}

/**
 * @brief 结束从 WASAPI InStream 读取
 * 
 * 释放 buffer，告知 audio_capture_client 读取操作完成。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param is SoundIoInStreamPrivate 的 shared_ptr
 * @return int 成功返回 0
 */
static int instream_end_read_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    SoundIoInStreamWasapi& isw = is->backend_data.wasapi;
    HRESULT hr;

    isw.read_buf_frames_left -= isw.read_frame_count;

    if (isw.read_buf_frames_left <= 0)
    {
        if (FAILED(hr = isw.audio_capture_client->ReleaseBuffer(isw.opened_buf_frames)))
        {
            return SoundIoErrorStreaming;
        }
    }

    return 0;
}

/**
 * @brief 获取 WASAPI InStream 当前的软件 latency
 * 
 * 通过 audio_client 获取 frames_used 并转换为 latency 时间。
 * 
 * @param si SoundIoPrivate 的 shared_ptr
 * @param is SoundIoInStreamPrivate 的 shared_ptr
 * @param out_latency 返回当前的 latency (秒)
 * @return int 成功返回 0
 */
static int instream_get_latency_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is, double* out_latency)
{
    SoundIoInStreamWasapi& isw = is->backend_data.wasapi;

    HRESULT hr;
    UINT32 frames_used;
    if (FAILED(hr = isw.audio_client->GetCurrentPadding(&frames_used)))
    {
        return SoundIoErrorStreaming;
    }

    *out_latency = frames_used / static_cast<double>(is->sample_rate);
    return 0;
}

// ==============================================================================
// 7. Backend Initialization & Destruction (Top Level)
// ==============================================================================

/**
 * @brief 执行 WASAPI 系统的顶级 destroy 操作
 * 
 * 通知 background thread 退出，并清理同步 primitives 比如 mutexes 和 conditions。
 * 
 * @param si 需要被销毁的 SoundIoPrivate 的 shared_ptr
 */
static void destroy_wasapi(std::shared_ptr<SoundIoPrivate> si)
{
    LOGI("destroy wasapi");
    SoundIoWasapi& siw = si->backend_data->wasapi;

    if (siw.thread)
    {
        std::unique_lock lock(siw.scan_devices_mutex->get());
        siw.abort_flag.clear();
        siw.scan_devices_cond->signal(&lock);
        lock.unlock();

        siw.thread = nullptr;
    }

    siw.cond = nullptr;
    siw.scan_devices_cond = nullptr;
    siw.scan_devices_mutex = nullptr;
    siw.mutex = nullptr;


    outstream_destroy_wasapi(si, std::dynamic_pointer_cast<SoundIoOutStreamPrivate>(si->outstream));

}

STDMETHODIMP soundio_NotificationClient::QueryInterface(REFIID riid, void** ppv)
{
    if (IS_EQUAL_IID(riid, IID_IUNKNOWN) || IS_EQUAL_IID(riid, IID_IMMNOTFICATION_CLIENT))
    {
        *ppv = this;
        AddRef();
        return S_OK;
    }
    else
    {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
}

STDMETHODIMP_(ULONG) soundio_NotificationClient::AddRef()
{
    // SoundIoWasapi& siw = &si.lock()->backend_data.wasapi;
    // return InterlockedIncrement(&siw->device_events_refs);
    return S_OK;
}

STDMETHODIMP_(ULONG) soundio_NotificationClient::Release()
{
    // SoundIoWasapi& siw = &si.lock()->backend_data.wasapi;
    // return InterlockedDecrement(&siw->device_events_refs);
    return S_OK;
}

/**
 * @brief 排队执行设备的 device scan
 * 
 * 当 NotificationClient 检测到 device 变化时，调用强制刷新 event。
 * 
 * @param client 通知客户端实例
 * @return HRESULT 返回 S_OK 即不处理异常
 */
static HRESULT queue_device_scan(soundio_NotificationClient* client)
{
    std::shared_ptr<SoundIoPrivate> si = client->si.lock();
    if (si == nullptr)
    {
        LOGE("os is nullptr!");
        return S_OK;
    }
    force_device_scan_wasapi(si);
    return S_OK;
}

STDMETHODIMP soundio_NotificationClient::OnDeviceStateChanged(LPCWSTR wid, DWORD state)
{
    return queue_device_scan(this);
}

STDMETHODIMP soundio_NotificationClient::OnDeviceAdded(LPCWSTR wid)
{
    return queue_device_scan(this);
}

STDMETHODIMP soundio_NotificationClient::OnDeviceRemoved(LPCWSTR wid)
{
    return queue_device_scan(this);
}

STDMETHODIMP soundio_NotificationClient::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR wid)
{
    if (role != eMultimedia)
    {
        return S_OK;
    }

    return queue_device_scan(this);
}

STDMETHODIMP soundio_NotificationClient::OnPropertyValueChanged(LPCWSTR wid, const PROPERTYKEY key)
{
    return S_OK;
}


/**
 * @brief 初始化 WASAPI backend (Top Level)
 * 
 * 初始化 WASAPI 的事件系统、线程互斥锁、条件变量以及 NotificationClient。
 * 这是整个 libsoundio WASAPI support 开始工作的入口。
 * 
 * @param si SoundIoPrivate 实例关联
 * @return int 初始化错误码，0 表示成功
 */
int soundio_wasapi_init(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoWasapi& siw = si->backend_data->wasapi;

    siw.device_scan_queued = true;
    siw.abort_flag.test_and_set();

    siw.mutex = soundio_os_mutex_create();
    if (!siw.mutex)
    {
        destroy_wasapi(si);
        return SoundIoErrorNoMem;
    }

    siw.scan_devices_mutex = soundio_os_mutex_create();
    if (!siw.scan_devices_mutex)
    {
        destroy_wasapi(si);
        return SoundIoErrorNoMem;
    }

    siw.cond = soundio_os_cond_create();
    if (!siw.cond)
    {
        destroy_wasapi(si);
        return SoundIoErrorNoMem;
    }

    siw.scan_devices_cond = soundio_os_cond_create();
    if (!siw.scan_devices_cond)
    {
        destroy_wasapi(si);
        return SoundIoErrorNoMem;
    }

    siw.thread = SoundIoOsThread::create(device_thread_run, si);

    siw.device_events = std::make_unique<soundio_NotificationClient>(si);

    si->destroy = destroy_wasapi;
    si->flush_events = flush_events_wasapi;
    si->wait_events = wait_events_wasapi;
    si->wakeup = wakeup_wasapi;
    si->force_device_scan = force_device_scan_wasapi;

    si->outstream_open = outstream_open_wasapi;
    si->outstream_destroy = outstream_destroy_wasapi;
    si->outstream_start = outstream_start_wasapi;
    si->outstream_begin_write = outstream_begin_write_wasapi;
    si->outstream_end_write = outstream_end_write_wasapi;
    si->outstream_clear_buffer = outstream_clear_buffer_wasapi;
    si->outstream_pause = outstream_pause_wasapi;
    si->outstream_get_latency = outstream_get_latency_wasapi;
    si->outstream_set_volume = outstream_set_volume_wasapi;
    si->outstream_get_time = outstream_get_time_wasapi;

    si->instream_open = instream_open_wasapi;
    si->instream_destroy = instream_destroy_wasapi;
    si->instream_start = instream_start_wasapi;
    si->instream_begin_read = instream_begin_read_wasapi;
    si->instream_end_read = instream_end_read_wasapi;
    si->instream_pause = instream_pause_wasapi;
    si->instream_get_latency = instream_get_latency_wasapi;

    return 0;
}
