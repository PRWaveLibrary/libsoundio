
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

#include <mmdeviceapi.h>
#include <mmreg.h>

#include "soundio_private.h"
#include "wasapi.h"

#include <stdio.h>

#include "../../src/unity_util.h"

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

/*
// useful for debugging but no point in compiling into binary
static const char *hresult_to_str(HRESULT hr) {
    switch (hr) {
        default: return "(unknown)";
        case AUDCLNT_E_NOT_INITIALIZED: return "AUDCLNT_E_NOT_INITIALIZED";
        case AUDCLNT_E_ALREADY_INITIALIZED: return "AUDCLNT_E_ALREADY_INITIALIZED";
        case AUDCLNT_E_WRONG_ENDPOINT_TYPE: return "AUDCLNT_E_WRONG_ENDPOINT_TYPE";
        case AUDCLNT_E_DEVICE_INVALIDATED: return "AUDCLNT_E_DEVICE_INVALIDATED";
        case AUDCLNT_E_NOT_STOPPED: return "AUDCLNT_E_NOT_STOPPED";
        case AUDCLNT_E_BUFFER_TOO_LARGE: return "AUDCLNT_E_BUFFER_TOO_LARGE";
        case AUDCLNT_E_OUT_OF_ORDER: return "AUDCLNT_E_OUT_OF_ORDER";
        case AUDCLNT_E_UNSUPPORTED_FORMAT: return "AUDCLNT_E_UNSUPPORTED_FORMAT";
        case AUDCLNT_E_INVALID_SIZE: return "AUDCLNT_E_INVALID_SIZE";
        case AUDCLNT_E_DEVICE_IN_USE: return "AUDCLNT_E_DEVICE_IN_USE";
        case AUDCLNT_E_BUFFER_OPERATION_PENDING: return "AUDCLNT_E_BUFFER_OPERATION_PENDING";
        case AUDCLNT_E_THREAD_NOT_REGISTERED: return "AUDCLNT_E_THREAD_NOT_REGISTERED";
        case AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED: return "AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED";
        case AUDCLNT_E_ENDPOINT_CREATE_FAILED: return "AUDCLNT_E_ENDPOINT_CREATE_FAILED";
        case AUDCLNT_E_SERVICE_NOT_RUNNING: return "AUDCLNT_E_SERVICE_NOT_RUNNING";
        case AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED: return "AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED";
        case AUDCLNT_E_EXCLUSIVE_MODE_ONLY: return "AUDCLNT_E_EXCLUSIVE_MODE_ONLY";
        case AUDCLNT_E_BUFDURATION_PERIOD_NOT_EQUAL: return "AUDCLNT_E_BUFDURATION_PERIOD_NOT_EQUAL";
        case AUDCLNT_E_EVENTHANDLE_NOT_SET: return "AUDCLNT_E_EVENTHANDLE_NOT_SET";
        case AUDCLNT_E_INCORRECT_BUFFER_SIZE: return "AUDCLNT_E_INCORRECT_BUFFER_SIZE";
        case AUDCLNT_E_BUFFER_SIZE_ERROR: return "AUDCLNT_E_BUFFER_SIZE_ERROR";
        case AUDCLNT_E_CPUUSAGE_EXCEEDED: return "AUDCLNT_E_CPUUSAGE_EXCEEDED";
        case AUDCLNT_E_BUFFER_ERROR: return "AUDCLNT_E_BUFFER_ERROR";
        case AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED: return "AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED";
        case AUDCLNT_E_INVALID_DEVICE_PERIOD: return "AUDCLNT_E_INVALID_DEVICE_PERIOD";
        case AUDCLNT_E_INVALID_STREAM_FLAG: return "AUDCLNT_E_INVALID_STREAM_FLAG";
        case AUDCLNT_E_ENDPOINT_OFFLOAD_NOT_CAPABLE: return "AUDCLNT_E_ENDPOINT_OFFLOAD_NOT_CAPABLE";
        case AUDCLNT_E_OUT_OF_OFFLOAD_RESOURCES: return "AUDCLNT_E_OUT_OF_OFFLOAD_RESOURCES";
        case AUDCLNT_E_OFFLOAD_MODE_ONLY: return "AUDCLNT_E_OFFLOAD_MODE_ONLY";
        case AUDCLNT_E_NONOFFLOAD_MODE_ONLY: return "AUDCLNT_E_NONOFFLOAD_MODE_ONLY";
        case AUDCLNT_E_RESOURCES_INVALIDATED: return "AUDCLNT_E_RESOURCES_INVALIDATED";
        case AUDCLNT_S_BUFFER_EMPTY: return "AUDCLNT_S_BUFFER_EMPTY";
        case AUDCLNT_S_THREAD_ALREADY_REGISTERED: return "AUDCLNT_S_THREAD_ALREADY_REGISTERED";
        case AUDCLNT_S_POSITION_STALLED: return "AUDCLNT_S_POSITION_STALLED";

        case E_POINTER: return "E_POINTER";
        case E_INVALIDARG: return "E_INVALIDARG";
        case E_OUTOFMEMORY: return "E_OUTOFMEMORY";
    }
}
*/

// converts a windows wide string to a UTF-8 encoded char *
// Possible errors:
//  * SoundIoErrorNoMem
//  * SoundIoErrorEncodingString
// static int from_lpwstr(LPWSTR lpwstr, char** out_str, int* out_str_len)
// {
//     DWORD flags = 0;
//     int buf_size = WideCharToMultiByte(CP_UTF8, flags, lpwstr, -1, NULL, 0, NULL, NULL);
//
//     if (buf_size == 0)
//         return SoundIoErrorEncodingString;
//
//     char* buf = ALLOCATE(char, buf_size);
//     if (!buf)
//     {
//         return SoundIoErrorNoMem;
//     }
//
//     if (WideCharToMultiByte(CP_UTF8, flags, lpwstr, -1, buf, buf_size, NULL, NULL) != buf_size)
//     {
//         free(buf);
//         return SoundIoErrorEncodingString;
//     }
//
//     *out_str = buf;
//     *out_str_len = buf_size - 1;
//
//     return 0;
// }

// static int to_lpwstr(const char* str, int str_len, LPWSTR* out_lpwstr)
// {
//     DWORD flags = 0;
//     int w_len = MultiByteToWideChar(CP_UTF8, flags, str, str_len, NULL, 0);
//     if (w_len <= 0)
//         return SoundIoErrorEncodingString;
//
//     LPWSTR buf = ALLOCATE(wchar_t, w_len + 1);
//     if (!buf)
//         return SoundIoErrorNoMem;
//
//     if (MultiByteToWideChar(CP_UTF8, flags, str, str_len, buf, w_len) != w_len)
//     {
//         free(buf);
//         return SoundIoErrorEncodingString;
//     }
//
//     *out_lpwstr = buf;
//     return 0;
// }

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

static void from_wave_format_layout(WAVEFORMATEXTENSIBLE* wave_format, struct SoundIoChannelLayout* layout)
{
    assert(wave_format->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE);
    layout->channel_count = 0;
    from_channel_mask_layout(wave_format->dwChannelMask, layout);
}

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

// only needs to support the layouts in test_layouts
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

// only needs to support the formats in test_formats
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

static void complete_wave_format_data(WAVEFORMATEXTENSIBLE* wave_format)
{
    wave_format->Format.nBlockAlign = (wave_format->Format.wBitsPerSample * wave_format->Format.nChannels) / 8;
    wave_format->Format.nAvgBytesPerSec = wave_format->Format.nSamplesPerSec * wave_format->Format.nBlockAlign;
}

static enum SoundIoDeviceAim data_flow_to_aim(EDataFlow data_flow)
{
    return (data_flow == eRender) ? SoundIoDeviceAimOutput : SoundIoDeviceAimInput;
}


static double from_reference_time(REFERENCE_TIME rt)
{
    return ((double) rt) / 10000000.0;
}

static REFERENCE_TIME to_reference_time(double seconds)
{
    return (REFERENCE_TIME)(seconds * 10000000.0 + 0.5);
}

struct RefreshDevices
{
    IMMDeviceCollection* collection;
    std::shared_ptr<IMMDevice> mm_device;
    IMMDevice* default_render_device;
    IMMDevice* default_capture_device;
    IMMEndpoint* endpoint;
    IPropertyStore* prop_store;
    IAudioClient* audio_client;
    PROPVARIANT prop_variant_value;
    WAVEFORMATEXTENSIBLE* wave_format;
    bool prop_variant_value_inited;
    std::unique_ptr<SoundIoDevicesInfo> devices_info;
    std::shared_ptr<SoundIoDevice> device_shared;
    std::shared_ptr<SoundIoDevice> device_raw;
    std::wstring default_render_id;
    std::wstring default_capture_id;
};

// static void deinit_refresh_devices(std::shared_ptr<RefreshDevices> rd)
// {
//     // soundio_destroy_devices_info(rd->devices_info);
//     if (rd->mm_device)
//     {
//         rd->mm_device->Release();
//     }
//     if (rd->default_render_device)
//     {
//         rd->default_render_device->Release();
//     }
//     if (rd->default_capture_device)
//     {
//         rd->default_capture_device->Release();
//     }
//     if (rd->collection)
//     {
//         rd->collection->Release();
//     }
//     if (rd->lpwstr)
//     {
//         CoTaskMemFree(rd->lpwstr);
//     }
//     if (rd->endpoint)
//     {
//         rd->endpoint->Release();
//     }
//     if (rd->prop_store)
//     {
//         rd->prop_store->Release();
//     }
//     if (rd->prop_variant_value_inited)
//     {
//         PropVariantClear(&rd->prop_variant_value);
//     }
//     if (rd->wave_format)
//     {
//         CoTaskMemFree(rd->wave_format);
//     }
//     if (rd->audio_client)
//     {
//         rd->audio_client->Release();
//     }
// }

static int detect_valid_layouts(std::shared_ptr<RefreshDevices> rd, WAVEFORMATEXTENSIBLE* wave_format,
                                std::shared_ptr<SoundIoDevicePrivate> dev, AUDCLNT_SHAREMODE share_mode)
{
    std::shared_ptr<SoundIoDevice> device = dev;
    HRESULT hr;

    WAVEFORMATEX* closest_match = NULL;
    WAVEFORMATEXTENSIBLE orig_wave_format = *wave_format;

    for (size_t i = 0; i < std::size(test_formats); i += 1)
    {
        enum SoundIoChannelLayoutId test_layout_id = test_layouts[i];
        const struct SoundIoChannelLayout* test_layout = soundio_channel_layout_get_builtin(test_layout_id);
        to_wave_format_layout(test_layout, wave_format);
        complete_wave_format_data(wave_format);

        hr = rd->audio_client->IsFormatSupported(share_mode, reinterpret_cast<WAVEFORMATEX *>(wave_format), &closest_match);
        if (closest_match)
        {
            CoTaskMemFree(closest_match);
            closest_match = NULL;
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

static int detect_valid_formats(std::shared_ptr<RefreshDevices> rd, WAVEFORMATEXTENSIBLE* wave_format,
                                std::shared_ptr<SoundIoDevicePrivate> dev, AUDCLNT_SHAREMODE share_mode)
{
    std::shared_ptr<SoundIoDevice> device = dev;
    HRESULT hr;

    // device->format_count = 0;
    // device->formats = ALLOCATE(enum SoundIoFormat, ARRAY_LENGTH(test_formats));
    // if (!device->formats)
    //     return SoundIoErrorNoMem;

    WAVEFORMATEX* closest_match = NULL;
    WAVEFORMATEXTENSIBLE orig_wave_format = *wave_format;

    for (size_t i = 0; i < std::size(test_formats); i += 1)
    {
        enum SoundIoFormat test_format = test_formats[i];
        to_wave_format_format(test_format, wave_format);
        complete_wave_format_data(wave_format);

        hr = rd->audio_client->IsFormatSupported(share_mode, (WAVEFORMATEX *) wave_format, &closest_match);
        if (closest_match)
        {
            CoTaskMemFree(closest_match);
            closest_match = NULL;
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
    WAVEFORMATEX* closest_match = NULL;
    int err;

    wave_format->Format.nSamplesPerSec = test_sample_rate;
    HRESULT hr = rd->audio_client->IsFormatSupported(share_mode, (WAVEFORMATEX *) wave_format, &closest_match);
    if (closest_match)
    {
        CoTaskMemFree(closest_match);
        closest_match = NULL;
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


static int refresh_devices(std::shared_ptr<SoundIoPrivate> si)
{
    std::shared_ptr<SoundIo> soundio = si;
    SoundIoWasapi& siw = si->backend_data->wasapi;
    std::shared_ptr<RefreshDevices> rd = std::make_shared<RefreshDevices>();
    int err;
    HRESULT hr;

    if (FAILED(hr = siw.device_enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &rd->default_render_device)))
    {
        if (hr != E_NOTFOUND)
        {
            if (hr == E_OUTOFMEMORY)
            {
                return SoundIoErrorNoMem;
            }
            return SoundIoErrorOpeningDevice;
        }
    }

    if (rd->default_render_device)
    {
        LPWSTR lpwstr;
        if (FAILED(hr = rd->default_render_device->GetId(&lpwstr)))
        {
            // MSDN states the IMMDevice_GetId can fail if the device is NULL, or if we're out of memory
            // We know the device point isn't NULL so we're necessarily out of memory
            return SoundIoErrorNoMem;
        }
        rd->default_render_id = lpwstr;
        CoTaskMemFree(lpwstr);
        lpwstr = nullptr;
    }

    if (FAILED(hr = siw.device_enumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, &rd->default_capture_device)))
    {
        if (hr != E_NOTFOUND)
        {
            if (hr == E_OUTOFMEMORY)
            {
                return SoundIoErrorNoMem;
            }
            return SoundIoErrorOpeningDevice;
        }
    }
    if (rd->default_capture_device)
    {
        LPWSTR lpwstr;
        if (FAILED(hr = rd->default_capture_device->GetId(&lpwstr)))
        {
            if (hr == E_OUTOFMEMORY)
            {
                return SoundIoErrorNoMem;
            }
            return SoundIoErrorOpeningDevice;
        }
        rd->default_capture_id = lpwstr;
        CoTaskMemFree(lpwstr);
        lpwstr = nullptr;
    }


    if (FAILED(hr = siw.device_enumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE, &rd->collection)))
    {
        if (hr == E_OUTOFMEMORY)
        {
            return SoundIoErrorNoMem;
        }
        return SoundIoErrorOpeningDevice;
    }

    UINT unsigned_count;
    if (FAILED(hr = rd->collection->GetCount(&unsigned_count)))
    {
        // In theory this shouldn't happen since the only documented failure case is that
        // rd.collection is NULL, but then EnumAudioEndpoints should have failed.
        return SoundIoErrorOpeningDevice;
    }

    if (unsigned_count > (UINT) INT_MAX)
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
        rd->device_shared->soundio = soundio;
        rd->device_shared->is_raw = false;
        rd->device_shared->software_latency_max = 2.0;

        assert(!rd->device_raw);
        std::shared_ptr<SoundIoDevicePrivate> raw_w = std::make_shared<SoundIoDevicePrivate>();

        rd->device_raw = raw_w;
        // rd->device_raw->ref_count = 1;
        rd->device_raw->soundio = soundio;
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

        if (FAILED(hr = rd->mm_device->QueryInterface(IID_IMMENDPOINT, reinterpret_cast<void **>(&rd->endpoint))))
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
        if (FAILED(hr = rd->mm_device->Activate(IID_IAUDIOCLIENT, CLSCTX_ALL, NULL, (void **) &rd->audio_client)))
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
        WAVEFORMATEXTENSIBLE* valid_wave_format = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(rd->prop_variant_value.blob.pBlobData);
        if (valid_wave_format->Format.wFormatTag != WAVE_FORMAT_EXTENSIBLE)
        {
            rd->device_shared->probe_error = SoundIoErrorOpeningDevice;
            rd->device_raw->probe_error = SoundIoErrorOpeningDevice;
            rd->device_shared = NULL;
            rd->device_raw = NULL;
            continue;
        }
        if ((err = detect_valid_sample_rates(rd, valid_wave_format, raw_w, AUDCLNT_SHAREMODE_EXCLUSIVE)))
        {
            rd->device_raw->probe_error = err;
            rd->device_raw = NULL;
        }
        if (rd->device_raw && (err = detect_valid_formats(rd, valid_wave_format, raw_w, AUDCLNT_SHAREMODE_EXCLUSIVE)))
        {
            rd->device_raw->probe_error = err;
            rd->device_raw = NULL;
        }
        if (rd->device_raw && (err = detect_valid_layouts(rd, valid_wave_format, raw_w, AUDCLNT_SHAREMODE_EXCLUSIVE)))
        {
            rd->device_raw->probe_error = err;
            rd->device_raw = NULL;
        }

        if (rd->wave_format)
        {
            CoTaskMemFree(rd->wave_format);
            rd->wave_format = NULL;
        }
        if (FAILED(hr = rd->audio_client->GetMixFormat(reinterpret_cast<WAVEFORMATEX **>(&rd->wave_format))))
        {
            // According to MSDN GetMixFormat only applies to shared-mode devices.
            rd->device_shared->probe_error = SoundIoErrorOpeningDevice;
            rd->device_shared = NULL;
        }
        else if (rd->wave_format && (rd->wave_format->Format.wFormatTag != WAVE_FORMAT_EXTENSIBLE))
        {
            rd->device_shared->probe_error = SoundIoErrorOpeningDevice;
            rd->device_shared = NULL;
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
                rd->device_shared = NULL;
            }
            else
            {
                from_wave_format_layout(rd->wave_format, &rd->device_shared->current_layout);
                rd->device_shared->layouts.push_back(rd->device_shared->current_layout);
            }
        }

        shared_w->backend_data.wasapi->mm_device = rd->mm_device;
        raw_w->backend_data.wasapi->mm_device = rd->mm_device;

        rd->mm_device = NULL;
        rd->device_shared = NULL;
        rd->device_raw = NULL;
    }

    soundio_os_mutex_lock(siw.mutex);
    siw.ready_devices_info = std::move(rd->devices_info);
    siw.have_devices_flag = true;
    soundio_os_cond_signal(siw.cond.get(), siw.mutex.get());
    soundio->on_events_signal(soundio);
    soundio_os_mutex_unlock(siw.mutex);
    return 0;
}


static void shutdown_backend(std::shared_ptr<SoundIoPrivate> si, int err)
{
    std::shared_ptr<SoundIo> soundio = si;
    SoundIoWasapi& siw = si->backend_data->wasapi;
    soundio_os_mutex_lock(siw.mutex);
    siw.shutdown_err = err;
    soundio_os_cond_signal(siw.cond.get(), siw.mutex.get());
    soundio->on_events_signal(soundio);
    soundio_os_mutex_unlock(siw.mutex);
}


static void my_flush_events(std::shared_ptr<SoundIoPrivate> si, bool wait)
{
    std::shared_ptr<SoundIo> soundio = si;
    SoundIoWasapi& siw = si->backend_data->wasapi;

    bool change = false;
    bool cb_shutdown = false;
    // std::shared_ptr<SoundIoDevicesInfo> old_devices_info = NULL;

    soundio_os_mutex_lock(siw.mutex);

    // block until have devices
    while (wait || (!siw.have_devices_flag && !siw.shutdown_err))
    {
        soundio_os_cond_wait(siw.cond.get(), siw.mutex.get());
        wait = false;
    }

    if (siw.shutdown_err && !siw.emitted_shutdown_cb)
    {
        siw.emitted_shutdown_cb = true;
        cb_shutdown = true;
    }
    else if (siw.ready_devices_info)
    {
        // old_devices_info = si->safe_devices_info;
        si->safe_devices_info = std::move(siw.ready_devices_info);
        change = true;
    }

    soundio_os_mutex_unlock(siw.mutex);

    if (cb_shutdown)
        soundio->on_backend_disconnect(soundio, siw.shutdown_err);
    else if (change)
        soundio->on_devices_change(soundio);

    // soundio_destroy_devices_info(old_devices_info);
}

static void flush_events_ca(std::shared_ptr<SoundIoPrivate> si)
{
    my_flush_events(si, false);
}

static void wait_events_ca(std::shared_ptr<SoundIoPrivate> si)
{
    my_flush_events(si, true);
}


static void device_thread_run(std::shared_ptr<void> arg)
{
    std::shared_ptr<SoundIoPrivate> si = std::static_pointer_cast<SoundIoPrivate>(arg);
    SoundIoWasapi& siw = si->backend_data->wasapi;
    int err;

    HRESULT hr = CoCreateInstance(CLSID_MMDEVICEENUMERATOR, NULL, CLSCTX_ALL, IID_IMMDEVICEENUMERATOR, reinterpret_cast<void **>(&siw.device_enumerator));
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

    soundio_os_mutex_lock(siw.scan_devices_mutex);
    for (;;)
    {
        if (siw.abort_flag)
        {
            break;
        }
        if (siw.device_scan_queued)
        {
            siw.device_scan_queued = false;
            soundio_os_mutex_unlock(siw.scan_devices_mutex);
            err = refresh_devices(si);
            if (err)
            {
                shutdown_backend(si, err);
                return;
            }
            soundio_os_mutex_lock(siw.scan_devices_mutex);
            continue;
        }
        soundio_os_cond_wait(siw.scan_devices_cond.get(), siw.scan_devices_mutex.get());
    }
    soundio_os_mutex_unlock(siw.scan_devices_mutex);

    siw.device_enumerator->UnregisterEndpointNotificationCallback(siw.device_events.get());
    siw.device_enumerator->Release();
    siw.device_enumerator = NULL;
}

static void wakeup_wasapi(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoWasapi& siw = si->backend_data->wasapi;
    soundio_os_cond_signal(siw.cond.get(), siw.mutex.get());
}

static void force_device_scan_wasapi(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoWasapi& siw = si->backend_data->wasapi;
    soundio_os_mutex_lock(siw.scan_devices_mutex);
    siw.device_scan_queued = true;
    soundio_os_cond_signal(siw.scan_devices_cond.get(), siw.scan_devices_mutex.get());
    soundio_os_mutex_unlock(siw.scan_devices_mutex);
}

static void outstream_thread_deinit(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;

    if (osw->audio_volume_control)
    {
        osw->audio_volume_control->Release();
        osw->audio_volume_control = nullptr;
    }
    if (osw->audio_render_client)
    {
        osw->audio_render_client->Release();
        osw->audio_render_client = nullptr;
    }
    if (osw->audio_session_control)
    {
        osw->audio_session_control->Release();
        osw->audio_session_control = nullptr;
    }
    if (osw->audio_clock_adjustment)
    {
        osw->audio_clock_adjustment->Release();
        osw->audio_clock_adjustment = nullptr;
    }
    if (osw->audio_clock)
    {
        osw->audio_clock->Release();
        osw->audio_clock = nullptr;
    }
    if (osw->audio_client)
    {
        osw->audio_client->Release();
        osw->audio_client = nullptr;
    }
}

static void outstream_destroy_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;

    if (osw->thread)
    {
        SOUNDIO_ATOMIC_FLAG_CLEAR(osw->thread_exit_flag);
        if (osw->h_event)
        {
            SetEvent(osw->h_event);
        }

        soundio_os_mutex_lock(osw->mutex);
        soundio_os_cond_signal(osw->cond.get(), osw->mutex.get());
        soundio_os_cond_signal(osw->start_cond.get(), osw->mutex.get());
        soundio_os_mutex_unlock(osw->mutex);

        // soundio_os_thread_destroy(osw->thread);

        osw->thread = NULL;
    }

    if (osw->h_event)
    {
        CloseHandle(osw->h_event);
        osw->h_event = NULL;
    }

    osw->stream_name = std::wstring();

    // soundio_os_cond_destroy(osw->cond);
    osw->cond = NULL;

    // soundio_os_cond_destroy(osw->start_cond);
    osw->start_cond = NULL;

    // soundio_os_mutex_destroy(osw->mutex);
    osw->mutex = NULL;
}
#ifdef DEBUG
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

static IAudioClient3* open_audio_client3(std::shared_ptr<SoundIoOutStreamPrivate> os, WAVEFORMATEXTENSIBLE* wave_format)
{
    IAudioClient3* audio_client3 = NULL;
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;
    std::shared_ptr<SoundIoOutStream> outstream = os;
    std::shared_ptr<SoundIoDevice> device = outstream->device;
    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(device);
    std::shared_ptr<SoundIoDeviceWasapi> dw = dev->backend_data.wasapi;

    WAVEFORMATEXTENSIBLE* mix_format;
    HRESULT hr;
    if (!osw->is_raw && FAILED(hr = dw->mm_device->Activate(IID_IAUDIOCLIENT3, CLSCTX_ALL, NULL, reinterpret_cast<void **>(&audio_client3))))
    {
        audio_client3->Release();
        return NULL;
    }

    if (!osw->is_raw)
    {
        if (FAILED(hr = audio_client3->GetMixFormat((WAVEFORMATEX **) &mix_format)))
        {
            audio_client3->Release();
            return NULL;
        }

        auto need_resample = mix_format->Format.nSamplesPerSec != outstream->sample_rate;
        if (need_resample)
        {
            // we can't use resampling with this new interface, fallback to old method
            CoTaskMemFree(mix_format);
            audio_client3->Release();
            return NULL;
        }

        wave_format->Format.nSamplesPerSec = mix_format->Format.nSamplesPerSec;
        CoTaskMemFree(mix_format);
    }

    return audio_client3;
}

static IAudioClient* open_audio_client(std::shared_ptr<SoundIoOutStreamPrivate> os, WAVEFORMATEXTENSIBLE* wave_format)
{
    IAudioClient* audio_client = NULL;
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;
    std::shared_ptr<SoundIoOutStream> outstream = os;
    std::shared_ptr<SoundIoDevice> device = outstream->device;
    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(device);
    std::shared_ptr<SoundIoDeviceWasapi> dw = dev->backend_data.wasapi;
    HRESULT hr;

    WAVEFORMATEXTENSIBLE* mix_format;

    if (FAILED(hr = dw->mm_device->Activate(IID_IAUDIOCLIENT, CLSCTX_ALL, NULL, reinterpret_cast<void **>(&audio_client))))
    {
        return NULL;
    }

    if (!osw->is_raw)
    {
        if (FAILED(hr = audio_client->GetMixFormat(reinterpret_cast<WAVEFORMATEX **>(&mix_format))))
        {
            audio_client->Release();
            return NULL;
        }
        // wave_format->Format.nSamplesPerSec = mix_format->Format.nSamplesPerSec;
        // outstream->sample_rate = mix_format->Format.nSamplesPerSec;
        osw->need_resample = mix_format->Format.nSamplesPerSec != outstream->sample_rate;
        CoTaskMemFree(mix_format);
        mix_format = NULL;
    }
    return audio_client;
}

static void deinit_mix_format(WAVEFORMATEXTENSIBLE** mix_format)
{
    if (*mix_format != NULL)
    {
        CoTaskMemFree(*mix_format);
        *mix_format = NULL;
    }
}

static void deinit_client3_and_callback(IAudioClient3** client3, IAudioClient** client, std::shared_ptr<SoundIoOutStreamPrivate> os, WAVEFORMATEXTENSIBLE* wave_format)
{
    if (*client3 != NULL)
    {
        (*client3)->Release();
        *client3 = NULL;
    }
    *client = open_audio_client(os, wave_format);
}


static int Initialize(IAudioClient** audio_client, IAudioClient3** audio_client3, std::shared_ptr<SoundIoOutStreamPrivate> os,
                      WAVEFORMATEXTENSIBLE* wave_format, DWORD flags, AUDCLNT_SHAREMODE share_mode,
                      REFERENCE_TIME buffer_duration, REFERENCE_TIME periodicity)
{
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;
    std::shared_ptr<SoundIoOutStream> outstream = os;
    std::shared_ptr<SoundIoDevice> device = outstream->device;
    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(device);
    std::shared_ptr<SoundIoDeviceWasapi> dw = dev->backend_data.wasapi;
    HRESULT hr;

    if (*audio_client3 != NULL)
    {
        if (osw->need_resample)
        {
            deinit_client3_and_callback(audio_client3, audio_client, os, wave_format);
            goto NEXT;
        }

        WAVEFORMATEXTENSIBLE* mix_format;
        if (FAILED(hr = (*audio_client3)->GetMixFormat(reinterpret_cast<WAVEFORMATEX **>(&mix_format))))
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
        if (outstream->software_latency < 1.0)
        {
            periodicity_in_frames = fundamental_period * (UINT32)(mix_format->Format.nSamplesPerSec * outstream->software_latency / fundamental_period);
            periodicity_in_frames = soundio_uint_clamp(min_period, periodicity_in_frames, max_period);
        }

        if (!osw->need_resample && SUCCEEDED(hr = (*audio_client3)->InitializeSharedAudioStream(flags, periodicity_in_frames, reinterpret_cast<WAVEFORMATEX *>(wave_format), NULL)))
        {
            deinit_mix_format(&mix_format);
            return SoundIoErrorNone;
        }
    }

NEXT:
    if (FAILED(hr = (*audio_client)->Initialize(share_mode, flags, buffer_duration, periodicity, reinterpret_cast<WAVEFORMATEX *>(wave_format), NULL)))
    {
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
        {
            UINT32 buffer_size;

            if (FAILED(hr = (*audio_client)->GetBufferSize(&buffer_size)))
            {
                (*audio_client)->Release();
                *audio_client = NULL;
                return SoundIoErrorOpeningDevice;
            }

            osw->buffer_frame_count = (int) buffer_size;

            (*audio_client)->Release();
            *audio_client = NULL;


            if (FAILED(hr = dw->mm_device->Activate(IID_IAUDIOCLIENT, CLSCTX_ALL, NULL, reinterpret_cast<void **>(audio_client))))
            {
                return SoundIoErrorOpeningDevice;
            }
            if (!osw->is_raw)
            {
                WAVEFORMATEXTENSIBLE* mix_format;
                if (FAILED(hr = (*audio_client)->GetMixFormat(reinterpret_cast<WAVEFORMATEX **>(&mix_format))))
                {
                    return SoundIoErrorOpeningDevice;
                }
                wave_format->Format.nSamplesPerSec = mix_format->Format.nSamplesPerSec;
                outstream->sample_rate = mix_format->Format.nSamplesPerSec;
                // osw->need_resample = mix_format->Format.nSamplesPerSec != wave_format->Format.nSamplesPerSec;

                deinit_mix_format(&mix_format);

                flags = (osw->need_resample ? AUDCLNT_STREAMFLAGS_RATEADJUST : 0) | AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
                to_wave_format_layout(&outstream->layout, wave_format);
                to_wave_format_format(outstream->format, wave_format);
                complete_wave_format_data(wave_format);
            }

            buffer_duration = to_reference_time(osw->buffer_frame_count / static_cast<double>(outstream->sample_rate));
            if (osw->is_raw)
                periodicity = buffer_duration;
            if (FAILED(hr = (*audio_client)->Initialize(share_mode, flags, buffer_duration, periodicity, reinterpret_cast<WAVEFORMATEX *>(wave_format), NULL)))
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


static int outstream_do_open(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;
    std::shared_ptr<SoundIoOutStream> outstream = os;
    std::shared_ptr<SoundIoDevice> device = outstream->device;

    HRESULT hr;
    if (outstream->software_latency <= 0.0)
    {
        outstream->software_latency = 1.0;
    }
    outstream->software_latency = soundio_double_clamp(device->software_latency_min, outstream->software_latency, device->software_latency_max);
    AUDCLNT_SHAREMODE share_mode;
    DWORD flags;
    REFERENCE_TIME buffer_duration;
    REFERENCE_TIME periodicity;
    WAVEFORMATEXTENSIBLE wave_format = {0};
    wave_format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wave_format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wave_format.Format.nSamplesPerSec = outstream->sample_rate;

    IAudioClient3* audio_client3 = open_audio_client3(os, &wave_format);
    IAudioClient* audio_client = NULL;

    if (audio_client3 == NULL)
    {
        audio_client = open_audio_client(os, &wave_format);
        if (audio_client == NULL)
        {
            return SoundIoErrorOpeningDevice;
        }
    }
    else
    {
        if (FAILED(hr = audio_client3->QueryInterface(IID_IAUDIOCLIENT, reinterpret_cast<void **>(&audio_client))))
        {
            audio_client3->Release();
            audio_client3 = NULL;
            return SoundIoErrorOpeningDevice;
        }
    }


    if (osw->is_raw)
    {
        flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        share_mode = AUDCLNT_SHAREMODE_EXCLUSIVE;
        periodicity = to_reference_time(outstream->software_latency);
        buffer_duration = periodicity;
    }
    else
    {
        if (osw->need_resample)
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

    to_wave_format_layout(&outstream->layout, &wave_format);
    to_wave_format_format(outstream->format, &wave_format);
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
    osw->min_padding_frames = (max_latency_sec * outstream->sample_rate) + 0.5;

    UINT32 buffer_size;

    if (FAILED(hr = audio_client->GetBufferSize(&buffer_size)))
    {
        return SoundIoErrorOpeningDevice;
    }

    osw->buffer_frame_count = static_cast<int>(buffer_size);
    outstream->software_latency = osw->buffer_frame_count / static_cast<double>(outstream->sample_rate);

    if (FAILED(hr = audio_client->SetEventHandle(osw->h_event)))
    {
        return SoundIoErrorOpeningDevice;
    }

    if (osw->need_resample)
    {
        if (FAILED(hr = audio_client->GetService(IID_IAUDIOCLOCKADJUSTMENT, reinterpret_cast<void **>(&osw->audio_clock_adjustment))))
        {
            return SoundIoErrorOpeningDevice;
        }

        // if (FAILED(hr = IAudioClockAdjustment_SetSampleRate(osw->audio_clock_adjustment, outstream->sample_rate)))
        // {
        //     return SoundIoErrorOpeningDevice;
        // }
    }

    if (!outstream->name.empty())
    {
        if (FAILED(hr = audio_client->GetService(IID_IAUDIOSESSIONCONTROL, reinterpret_cast<void **>(&osw->audio_session_control))))
        {
            return SoundIoErrorOpeningDevice;
        }

        osw->stream_name = outstream->name;
        if (FAILED(hr = osw->audio_session_control->SetDisplayName(osw->stream_name.c_str(), NULL)))
        {
            return SoundIoErrorOpeningDevice;
        }
    }

    if (FAILED(hr = audio_client->GetService(IID_IAUDIORENDERCLIENT, reinterpret_cast<void **>(&osw->audio_render_client))))
    {
        return SoundIoErrorOpeningDevice;
    }

    if (FAILED(hr = audio_client->GetService(IID_IAUDIOCLOCK, reinterpret_cast<void **>(&osw->audio_clock))))
    {
        return SoundIoErrorOpeningDevice;
    }

    if (FAILED(hr = audio_client->GetService(IID_ISIMPLEAUDIOVOLUME, reinterpret_cast<void **>(&osw->audio_volume_control))))
    {
        return SoundIoErrorOpeningDevice;
    }

    if (FAILED(hr = osw->audio_volume_control->GetMasterVolume(&outstream->volume)))
    {
        return SoundIoErrorOpeningDevice;
    }

    osw->audio_client = audio_client;
    return 0;
}

static void outstream_shared_run(std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;
    std::shared_ptr<SoundIoOutStream> outstream = os;

    HRESULT hr;

    UINT32 frames_used;
    if (FAILED(hr = osw->audio_client->GetCurrentPadding(&frames_used)))
    {
        outstream->error_callback(outstream, SoundIoErrorStreaming);
        return;
    }
    osw->writable_frame_count = osw->buffer_frame_count - (int) frames_used;
    if (osw->writable_frame_count <= 0)
    {
        outstream->error_callback(outstream, SoundIoErrorStreaming);
        return;
    }
    int frame_count_min = soundio_int_max(0, osw->min_padding_frames - (int) frames_used);
    outstream->write_callback(outstream, frame_count_min, osw->writable_frame_count);

    if (FAILED(hr = osw->audio_client->Start()))
    {
        outstream->error_callback(outstream, SoundIoErrorStreaming);
        return;
    }

    for (;;)
    {
        WaitForSingleObject(osw->h_event, INFINITE);
        if (FAILED(hr = osw->audio_client->GetCurrentPadding(&frames_used)))
        {
            outstream->error_callback(outstream, SoundIoErrorStreaming);
            return;
        }
        osw->writable_frame_count = osw->buffer_frame_count - frames_used;
        if (!SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(osw->thread_exit_flag))
        {
            return;
        }
        if (!SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(osw->pause_resume_flag))
        {
            bool pause = SOUNDIO_ATOMIC_LOAD(osw->desired_pause_state);
            if (pause && !osw->is_paused)
            {
                if (FAILED(hr = osw->audio_client->Stop()))
                {
                    outstream->error_callback(outstream, SoundIoErrorStreaming);
                    return;
                }
                osw->is_paused = true;
            }
            else if (!pause && osw->is_paused)
            {
                if (FAILED(hr = osw->audio_client->Start()))
                {
                    outstream->error_callback(outstream, SoundIoErrorStreaming);
                    return;
                }
                osw->is_paused = false;
            }
        }

        if (frames_used == 0 && !osw->is_paused)
        {
            outstream->underflow_callback(outstream);
        }
        frame_count_min = soundio_uint_max(0U, osw->min_padding_frames - frames_used);
        outstream->write_callback(outstream, frame_count_min, osw->writable_frame_count);
    }
}

static void outstream_raw_run(std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;
    std::shared_ptr<SoundIoOutStream> outstream = os;

    HRESULT hr;

    outstream->write_callback(outstream, osw->buffer_frame_count, osw->buffer_frame_count);

    if (FAILED(hr = osw->audio_client->Start()))
    {
        outstream->error_callback(outstream, SoundIoErrorStreaming);
        return;
    }

    for (;;)
    {
        WaitForSingleObject(osw->h_event, INFINITE);
        if (!SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(osw->thread_exit_flag))
            return;
        if (!SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(osw->pause_resume_flag))
        {
            bool pause = SOUNDIO_ATOMIC_LOAD(osw->desired_pause_state);
            if (pause && !osw->is_paused)
            {
                if (FAILED(hr = osw->audio_client->Stop()))
                {
                    outstream->error_callback(outstream, SoundIoErrorStreaming);
                    return;
                }
                osw->is_paused = true;
            }
            else if (!pause && osw->is_paused)
            {
                if (FAILED(hr = osw->audio_client->Start()))
                {
                    outstream->error_callback(outstream, SoundIoErrorStreaming);
                    return;
                }
                osw->is_paused = false;
            }
        }

        outstream->write_callback(outstream, osw->buffer_frame_count, osw->buffer_frame_count);
    }
}

static void outstream_thread_run(std::shared_ptr<void> arg)
{
    std::shared_ptr<SoundIoOutStreamPrivate> os = std::static_pointer_cast<SoundIoOutStreamPrivate>(arg);
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;
    std::shared_ptr<SoundIoOutStream> outstream = os;
    std::shared_ptr<SoundIoDevice> device = outstream->device;
    std::shared_ptr<SoundIo> soundio = device->soundio.lock();
    std::shared_ptr<SoundIoPrivate> si = std::dynamic_pointer_cast<SoundIoPrivate>(soundio);

    int err;
    if ((err = outstream_do_open(si, os)))
    {
        outstream_thread_deinit(si, os);

        soundio_os_mutex_lock(osw->mutex);
        osw->open_err = err;
        osw->open_complete = true;
        soundio_os_cond_signal(osw->cond.get(), osw->mutex.get());
        soundio_os_mutex_unlock(osw->mutex);
        return;
    }

    soundio_os_mutex_lock(osw->mutex);
    osw->open_complete = true;
    soundio_os_cond_signal(osw->cond.get(), osw->mutex.get());
    for (;;)
    {
        if (!SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(osw->thread_exit_flag))
        {
            soundio_os_mutex_unlock(osw->mutex);
            return;
        }
        if (osw->started)
        {
            soundio_os_mutex_unlock(osw->mutex);
            break;
        }
        soundio_os_cond_wait(osw->start_cond.get(), osw->mutex.get());
    }

    if (osw->is_raw)
    {
        outstream_raw_run(os);
    }
    else
    {
        outstream_shared_run(os);
    }

    outstream_thread_deinit(si, os);
}

static int outstream_open_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;
    std::shared_ptr<SoundIoOutStream> outstream = os;
    std::shared_ptr<SoundIoDevice> device = outstream->device;
    std::shared_ptr<SoundIo> soundio = si;

    SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(osw->pause_resume_flag);
    SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(osw->clear_buffer_flag);
    SOUNDIO_ATOMIC_STORE(osw->desired_pause_state, false);

    // All the COM functions are supposed to be called from the same thread. libsoundio API does not
    // restrict the calling thread context in this way. Furthermore, the user might have called
    // CoInitializeEx with a different threading model than Single Threaded Apartment.
    // So we create a thread to do all the initialization and teardown, and communicate state
    // via conditions and signals. The thread for initialization and teardown is also used
    // for the realtime code calls the user write_callback.

    osw->is_raw = device->is_raw;

    if (!(osw->cond = soundio_os_cond_create()))
    {
        outstream_destroy_wasapi(si, os);
        return SoundIoErrorNoMem;
    }

    if (!(osw->start_cond = soundio_os_cond_create()))
    {
        outstream_destroy_wasapi(si, os);
        return SoundIoErrorNoMem;
    }

    if (!(osw->mutex = soundio_os_mutex_create()))
    {
        outstream_destroy_wasapi(si, os);
        return SoundIoErrorNoMem;
    }

    osw->h_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!osw->h_event)
    {
        outstream_destroy_wasapi(si, os);
        return SoundIoErrorOpeningDevice;
    }

    SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(osw->thread_exit_flag);
    int err;
    if ((err = soundio_os_thread_create(outstream_thread_run, os, soundio->emit_rtprio_warning, &osw->thread)))
    {
        outstream_destroy_wasapi(si, os);
        return err;
    }
    soundio_os_mutex_lock(osw->mutex);
    while (!osw->open_complete)
    {
        soundio_os_cond_wait(osw->cond.get(), osw->mutex.get());
    }
    soundio_os_mutex_unlock(osw->mutex);

    if (osw->open_err)
    {
        outstream_destroy_wasapi(si, os);
        return osw->open_err;
    }

    return 0;
}

static int outstream_pause_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, bool pause)
{
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;

    SOUNDIO_ATOMIC_STORE(osw->desired_pause_state, pause);
    SOUNDIO_ATOMIC_FLAG_CLEAR(osw->pause_resume_flag);
    if (osw->h_event)
    {
        SetEvent(osw->h_event);
    }
    else
    {
        soundio_os_mutex_lock(osw->mutex);
        soundio_os_cond_signal(osw->cond.get(), osw->mutex.get());
        soundio_os_mutex_unlock(osw->mutex);
    }

    return 0;
}

static int outstream_start_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;

    soundio_os_mutex_lock(osw->mutex);
    osw->started = true;
    soundio_os_cond_signal(osw->start_cond.get(), osw->mutex.get());
    soundio_os_mutex_unlock(osw->mutex);

    return 0;
}

static int outstream_begin_write_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, SoundIoChannelArea** out_areas, int* frame_count)
{
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;
    std::shared_ptr<SoundIoOutStream> outstream = os;
    HRESULT hr;

    osw->write_frame_count = *frame_count;


    char* data;
    if (FAILED(hr = osw->audio_render_client->GetBuffer(osw->write_frame_count, reinterpret_cast<BYTE **>(&data))))
    {
        return SoundIoErrorStreaming;
    }

    for (int ch = 0; ch < outstream->layout.channel_count; ch += 1)
    {
        osw->areas[ch].ptr = data + ch * outstream->bytes_per_sample;
        osw->areas[ch].step = outstream->bytes_per_frame;
    }

    *out_areas = osw->areas;

    return 0;
}

static int outstream_end_write_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;
    HRESULT hr;
    if (FAILED(hr = osw->audio_render_client->ReleaseBuffer(osw->write_frame_count, 0)))
    {
        return SoundIoErrorStreaming;
    }
    return 0;
}

static int outstream_clear_buffer_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os)
{
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;

    if (osw->h_event)
    {
        return SoundIoErrorIncompatibleDevice;
    }
    else
    {
        SOUNDIO_ATOMIC_FLAG_CLEAR(osw->clear_buffer_flag);
        soundio_os_mutex_lock(osw->mutex);
        soundio_os_cond_signal(osw->cond.get(), osw->mutex.get());
        soundio_os_mutex_unlock(osw->mutex);
    }

    return 0;
}

static int outstream_get_latency_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, double* out_latency)
{
    std::shared_ptr<SoundIoOutStream> outstream = os;
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;

    HRESULT hr;
    UINT32 frames_used;
    if (FAILED(hr = osw->audio_client->GetCurrentPadding(&frames_used)))
    {
        return SoundIoErrorStreaming;
    }

    *out_latency = frames_used / static_cast<double>(outstream->sample_rate);
    return 0;
}

static int outstream_get_time_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, double* out_time)
{
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;

    UINT64 position = 0;
    UINT64 qpc = 0;

    if (SUCCEEDED(osw->audio_clock->GetPosition(&position, &qpc)))
    {
        UINT64 freq = 0;
        if (SUCCEEDED(osw->audio_clock->GetFrequency(&freq) && freq > 0))
        {
            *out_time = static_cast<double>(position) / static_cast<double>(freq);
            return 0;
        }
    }
    *out_time = 0;
    return -1;
}

static int outstream_set_volume_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoOutStreamPrivate> os, float volume)
{
    std::shared_ptr<SoundIoOutStream> outstream = os;
    struct SoundIoOutStreamWasapi* osw = &os->backend_data.wasapi;

    HRESULT hr;
    if (FAILED(hr = osw->audio_volume_control->SetMasterVolume(volume, NULL)))
    {
        return SoundIoErrorIncompatibleDevice;
    }

    outstream->volume = volume;
    return 0;
}

static void instream_thread_deinit(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    struct SoundIoInStreamWasapi* isw = &is->backend_data.wasapi;

    if (isw->audio_capture_client)
    {
        isw->audio_capture_client->Release();
    }
    if (isw->audio_client)
    {
        isw->audio_client->Release();
    }
}


static void instream_destroy_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    struct SoundIoInStreamWasapi* isw = &is->backend_data.wasapi;

    if (isw->thread)
    {
        SOUNDIO_ATOMIC_FLAG_CLEAR(isw->thread_exit_flag);
        if (isw->h_event)
        {
            SetEvent(isw->h_event);
        }

        soundio_os_mutex_lock(isw->mutex);
        soundio_os_cond_signal(isw->cond.get(), isw->mutex.get());
        soundio_os_cond_signal(isw->start_cond.get(), isw->mutex.get());
        soundio_os_mutex_unlock(isw->mutex);
        // soundio_os_thread_destroy(isw->thread);

        isw->thread = nullptr;
    }

    if (isw->h_event)
    {
        CloseHandle(isw->h_event);
        isw->h_event = nullptr;
    }

    // soundio_os_cond_destroy(isw->cond);
    isw->cond = nullptr;

    // soundio_os_cond_destroy(isw->start_cond);
    isw->start_cond = nullptr;

    // soundio_os_mutex_destroy(isw->mutex);
    isw->mutex = nullptr;
}

static int instream_do_open(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    struct SoundIoInStreamWasapi* isw = &is->backend_data.wasapi;
    std::shared_ptr<SoundIoInStream> instream = is;
    std::shared_ptr<SoundIoDevice> device = instream->device;
    std::shared_ptr<SoundIoDevicePrivate> dev = std::dynamic_pointer_cast<SoundIoDevicePrivate>(device);
    std::shared_ptr<SoundIoDeviceWasapi> dw = dev->backend_data.wasapi;
    HRESULT hr;

    if (FAILED(hr = dw->mm_device->Activate(IID_IAUDIOCLIENT, CLSCTX_ALL, NULL, reinterpret_cast<void **>(&isw->audio_client))))
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
    if (isw->is_raw)
    {
        wave_format.Format.nSamplesPerSec = instream->sample_rate;
        flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        share_mode = AUDCLNT_SHAREMODE_EXCLUSIVE;
        periodicity = to_reference_time(dw->period_duration);
        buffer_duration = periodicity;
    }
    else
    {
        WAVEFORMATEXTENSIBLE* mix_format;
        if (FAILED(hr = isw->audio_client->GetMixFormat(reinterpret_cast<WAVEFORMATEX **>(&mix_format))))
        {
            return SoundIoErrorOpeningDevice;
        }
        wave_format.Format.nSamplesPerSec = mix_format->Format.nSamplesPerSec;
        CoTaskMemFree(mix_format);
        mix_format = NULL;
        if (wave_format.Format.nSamplesPerSec != static_cast<DWORD>(instream->sample_rate))
        {
            return SoundIoErrorIncompatibleDevice;
        }
        flags = 0;
        share_mode = AUDCLNT_SHAREMODE_SHARED;
        periodicity = 0;
        buffer_duration = to_reference_time(4.0);
    }
    to_wave_format_layout(&instream->layout, &wave_format);
    to_wave_format_format(instream->format, &wave_format);
    complete_wave_format_data(&wave_format);

    if (FAILED(hr = isw->audio_client->Initialize(share_mode, flags, buffer_duration, periodicity, reinterpret_cast<WAVEFORMATEX *>(&wave_format), NULL)))
    {
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED)
        {
            if (FAILED(hr = isw->audio_client->GetBufferSize(&isw->buffer_frame_count)))
            {
                return SoundIoErrorOpeningDevice;
            }
            isw->audio_client->Release();
            isw->audio_client = NULL;
            if (FAILED(hr = dw->mm_device->Activate(IID_IAUDIOCLIENT, CLSCTX_ALL, NULL, reinterpret_cast<void **>(&isw->audio_client))))
            {
                return SoundIoErrorOpeningDevice;
            }
            if (!isw->is_raw)
            {
                WAVEFORMATEXTENSIBLE* mix_format;
                if (FAILED(hr = isw->audio_client->GetMixFormat(reinterpret_cast<WAVEFORMATEX **>(&mix_format))))
                {
                    return SoundIoErrorOpeningDevice;
                }
                wave_format.Format.nSamplesPerSec = mix_format->Format.nSamplesPerSec;
                CoTaskMemFree(mix_format);
                mix_format = NULL;
                flags = 0;
                to_wave_format_layout(&instream->layout, &wave_format);
                to_wave_format_format(instream->format, &wave_format);
                complete_wave_format_data(&wave_format);
            }

            buffer_duration = to_reference_time(isw->buffer_frame_count / static_cast<double>(instream->sample_rate));
            if (isw->is_raw)
                periodicity = buffer_duration;
            if (FAILED(hr = isw->audio_client->Initialize(share_mode, flags, buffer_duration, periodicity, reinterpret_cast<WAVEFORMATEX *>(&wave_format), NULL)))
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
    if (FAILED(hr = isw->audio_client->GetBufferSize(&isw->buffer_frame_count)))
    {
        return SoundIoErrorOpeningDevice;
    }
    if (instream->software_latency == 0.0)
        instream->software_latency = 1.0;
    instream->software_latency = soundio_double_clamp(device->software_latency_min, instream->software_latency,
                                                      device->software_latency_max);
    if (isw->is_raw)
        instream->software_latency = isw->buffer_frame_count / (double) instream->sample_rate;

    if (isw->is_raw)
    {
        if (FAILED(hr = isw->audio_client->SetEventHandle(isw->h_event)))
        {
            return SoundIoErrorOpeningDevice;
        }
    }

    if (!instream->name.empty())
    {
        if (FAILED(hr = isw->audio_client->GetService(IID_IAUDIOSESSIONCONTROL, reinterpret_cast<void **>(&isw->audio_session_control))))
        {
            return SoundIoErrorOpeningDevice;
        }

        isw->stream_name = instream->name;
        if (FAILED(hr = isw->audio_session_control->SetDisplayName(isw->stream_name.c_str(), NULL)))
        {
            return SoundIoErrorOpeningDevice;
        }
    }

    if (FAILED(hr = isw->audio_client->GetService(IID_IAUDIOCAPTURECLIENT, reinterpret_cast<void **>(&isw->audio_capture_client))))
    {
        return SoundIoErrorOpeningDevice;
    }

    return 0;
}

static void instream_raw_run(std::shared_ptr<SoundIoInStreamPrivate> is)
{
    struct SoundIoInStreamWasapi* isw = &is->backend_data.wasapi;
    std::shared_ptr<SoundIoInStream> instream = is;

    HRESULT hr;

    if (FAILED(hr = isw->audio_client->Start()))
    {
        instream->error_callback(instream, SoundIoErrorStreaming);
        return;
    }

    for (;;)
    {
        WaitForSingleObject(isw->h_event, INFINITE);
        if (!SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(isw->thread_exit_flag))
            return;

        instream->read_callback(instream, isw->buffer_frame_count, isw->buffer_frame_count);
    }
}

static void instream_shared_run(std::shared_ptr<SoundIoInStreamPrivate> is)
{
    struct SoundIoInStreamWasapi* isw = &is->backend_data.wasapi;
    std::shared_ptr<SoundIoInStream> instream = is;

    HRESULT hr;

    if (FAILED(hr = isw->audio_client->Start()))
    {
        instream->error_callback(instream, SoundIoErrorStreaming);
        return;
    }

    for (;;)
    {
        soundio_os_mutex_lock(isw->mutex);
        soundio_os_cond_timed_wait(isw->cond.get(), isw->mutex.get(), instream->software_latency / 2.0);
        if (!SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(isw->thread_exit_flag))
        {
            soundio_os_mutex_unlock(isw->mutex);
            return;
        }
        soundio_os_mutex_unlock(isw->mutex);

        UINT32 frames_available;
        if (FAILED(hr = isw->audio_client->GetCurrentPadding(&frames_available)))
        {
            instream->error_callback(instream, SoundIoErrorStreaming);
            return;
        }

        isw->readable_frame_count = frames_available;
        if (isw->readable_frame_count > 0)
            instream->read_callback(instream, 0, isw->readable_frame_count);
    }
}

static void instream_thread_run(std::shared_ptr<void> arg)
{
    std::shared_ptr<SoundIoInStreamPrivate> is = std::static_pointer_cast<SoundIoInStreamPrivate>(arg);
    struct SoundIoInStreamWasapi* isw = &is->backend_data.wasapi;
    std::shared_ptr<SoundIoInStream> instream = is;
    std::shared_ptr<SoundIoDevice> device = instream->device;
    std::shared_ptr<SoundIo> soundio = device->soundio.lock();
    std::shared_ptr<SoundIoPrivate> si = std::dynamic_pointer_cast<SoundIoPrivate>(soundio);

    int err;
    if ((err = instream_do_open(si, is)))
    {
        instream_thread_deinit(si, is);

        soundio_os_mutex_lock(isw->mutex);
        isw->open_err = err;
        isw->open_complete = true;
        soundio_os_cond_signal(isw->cond.get(), isw->mutex.get());
        soundio_os_mutex_unlock(isw->mutex);
        return;
    }

    soundio_os_mutex_lock(isw->mutex);
    isw->open_complete = true;
    soundio_os_cond_signal(isw->cond.get(), isw->mutex.get());
    for (;;)
    {
        if (!SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(isw->thread_exit_flag))
        {
            soundio_os_mutex_unlock(isw->mutex);
            return;
        }
        if (isw->started)
        {
            soundio_os_mutex_unlock(isw->mutex);
            break;
        }
        soundio_os_cond_wait(isw->start_cond.get(), isw->mutex.get());
    }

    if (isw->is_raw)
        instream_raw_run(is);
    else
        instream_shared_run(is);

    instream_thread_deinit(si, is);
}

static int instream_open_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    struct SoundIoInStreamWasapi* isw = &is->backend_data.wasapi;
    std::shared_ptr<SoundIoInStream> instream = is;
    std::shared_ptr<SoundIoDevice> device = instream->device;
    std::shared_ptr<SoundIo> soundio = si;

    // All the COM functions are supposed to be called from the same thread. libsoundio API does not
    // restrict the calling thread context in this way. Furthermore, the user might have called
    // CoInitializeEx with a different threading model than Single Threaded Apartment.
    // So we create a thread to do all the initialization and teardown, and communicate state
    // via conditions and signals. The thread for initialization and teardown is also used
    // for the realtime code calls the user write_callback.

    isw->is_raw = device->is_raw;

    if (!(isw->cond = soundio_os_cond_create()))
    {
        instream_destroy_wasapi(si, is);
        return SoundIoErrorNoMem;
    }

    if (!(isw->start_cond = soundio_os_cond_create()))
    {
        instream_destroy_wasapi(si, is);
        return SoundIoErrorNoMem;
    }

    if (!(isw->mutex = soundio_os_mutex_create()))
    {
        instream_destroy_wasapi(si, is);
        return SoundIoErrorNoMem;
    }

    if (isw->is_raw)
    {
        isw->h_event = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (!isw->h_event)
        {
            instream_destroy_wasapi(si, is);
            return SoundIoErrorOpeningDevice;
        }
    }

    SOUNDIO_ATOMIC_FLAG_TEST_AND_SET(isw->thread_exit_flag);
    int err;
    if ((err = soundio_os_thread_create(instream_thread_run, is, soundio->emit_rtprio_warning, &isw->thread)))
    {
        instream_destroy_wasapi(si, is);
        return err;
    }

    soundio_os_mutex_lock(isw->mutex);
    while (!isw->open_complete)
    {
        soundio_os_cond_wait(isw->cond.get(), isw->mutex.get());
    }
    soundio_os_mutex_unlock(isw->mutex);

    if (isw->open_err)
    {
        instream_destroy_wasapi(si, is);
        return isw->open_err;
    }

    return 0;
}

static int instream_pause_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is, bool pause)
{
    struct SoundIoInStreamWasapi* isw = &is->backend_data.wasapi;
    HRESULT hr;
    if (pause && !isw->is_paused)
    {
        if (FAILED(hr = isw->audio_client->Stop()))
            return SoundIoErrorStreaming;
        isw->is_paused = true;
    }
    else if (!pause && isw->is_paused)
    {
        if (FAILED(hr = isw->audio_client->Start()))
            return SoundIoErrorStreaming;
        isw->is_paused = false;
    }
    return 0;
}

static int instream_start_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    SoundIoInStreamWasapi& isw = is->backend_data.wasapi;

    soundio_os_mutex_lock(isw.mutex);
    isw.started = true;
    soundio_os_cond_signal(isw.start_cond.get(), isw.mutex.get());
    soundio_os_mutex_unlock(isw.mutex);

    return 0;
}

static int instream_begin_read_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is,
                                      struct SoundIoChannelArea** out_areas, int* frame_count)
{
    struct SoundIoInStreamWasapi* isw = &is->backend_data.wasapi;
    std::shared_ptr<SoundIoInStream> instream = is;
    HRESULT hr;

    if (isw->read_buf_frames_left <= 0)
    {
        UINT32 frames_to_read;
        DWORD flags;
        if (FAILED(hr = isw->audio_capture_client->GetBuffer(reinterpret_cast<BYTE **>(&isw->read_buf), &frames_to_read, &flags, NULL, NULL)))
        {
            return SoundIoErrorStreaming;
        }
        isw->opened_buf_frames = frames_to_read;
        isw->read_buf_frames_left = frames_to_read;

        if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
            isw->read_buf = NULL;
    }

    isw->read_frame_count = soundio_int_min(*frame_count, isw->read_buf_frames_left);
    *frame_count = isw->read_frame_count;

    if (isw->read_buf)
    {
        for (int ch = 0; ch < instream->layout.channel_count; ch += 1)
        {
            isw->areas[ch].ptr = isw->read_buf + ch * instream->bytes_per_sample;
            isw->areas[ch].step = instream->bytes_per_frame;

            isw->areas[ch].ptr += instream->bytes_per_frame * (isw->opened_buf_frames - isw->read_buf_frames_left);
        }

        *out_areas = isw->areas;
    }
    else
    {
        *out_areas = NULL;
    }

    return 0;
}

static int instream_end_read_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is)
{
    struct SoundIoInStreamWasapi* isw = &is->backend_data.wasapi;
    HRESULT hr;

    isw->read_buf_frames_left -= isw->read_frame_count;

    if (isw->read_buf_frames_left <= 0)
    {
        if (FAILED(hr = isw->audio_capture_client->ReleaseBuffer(isw->opened_buf_frames)))
        {
            return SoundIoErrorStreaming;
        }
    }

    return 0;
}

static int instream_get_latency_wasapi(std::shared_ptr<SoundIoPrivate> si, std::shared_ptr<SoundIoInStreamPrivate> is, double* out_latency)
{
    std::shared_ptr<SoundIoInStream> instream = is;
    struct SoundIoInStreamWasapi* isw = &is->backend_data.wasapi;

    HRESULT hr;
    UINT32 frames_used;
    if (FAILED(hr = isw->audio_client->GetCurrentPadding(&frames_used)))
    {
        return SoundIoErrorStreaming;
    }

    *out_latency = frames_used / static_cast<double>(instream->sample_rate);
    return 0;
}

static void destroy_wasapi(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoWasapi& siw = si->backend_data->wasapi;

    if (siw.thread)
    {
        soundio_os_mutex_lock(siw.scan_devices_mutex);
        siw.abort_flag = true;
        soundio_os_cond_signal(siw.scan_devices_cond.get(), siw.scan_devices_mutex.get());
        soundio_os_mutex_unlock(siw.scan_devices_mutex);
        siw.thread = nullptr;
    }

    siw.cond = nullptr;
    siw.scan_devices_cond = nullptr;
    siw.scan_devices_mutex = nullptr;
    siw.mutex = nullptr;
}


// static inline std::shared_ptr<SoundIoPrivate> soundio_MMNotificationClient_si(soundio_NotificationClient* client)
// {
//     SoundIoWasapi& siw = (SoundIoWasapi&) (((char*) client) - offsetof(struct SoundIoWasapi, device_events));
//     std::shared_ptr<SoundIoPrivate> si = (std::shared_ptr<SoundIoPrivate>) (((char*) siw) - offsetof(struct SoundIoPrivate, backend_data));
//     return si;
// }

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
        *ppv = NULL;
        return E_NOINTERFACE;
    }
}

STDMETHODIMP_ (ULONG) soundio_NotificationClient::AddRef()
{
    // SoundIoWasapi& siw = &si.lock()->backend_data.wasapi;
    // return InterlockedIncrement(&siw->device_events_refs);
    return S_OK;
}

STDMETHODIMP_ (ULONG) soundio_NotificationClient::Release()
{
    // SoundIoWasapi& siw = &si.lock()->backend_data.wasapi;
    // return InterlockedDecrement(&siw->device_events_refs);
    return S_OK;
}

static HRESULT queue_device_scan(soundio_NotificationClient* client)
{
    force_device_scan_wasapi(client->si.lock());
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
    if (role != eConsole)
    {
        return S_OK;
    }

    std::shared_ptr<SoundIoPrivate> s = si.lock();
    if (s == nullptr)
    {
        return S_OK;
    }

    auto result = queue_device_scan(this);

    soundio_wait_events(s);
    return result;
}

STDMETHODIMP soundio_NotificationClient::OnPropertyValueChanged(LPCWSTR wid, const PROPERTYKEY key)
{
    return S_OK;
    // return queue_device_scan(this);
}


int soundio_wasapi_init(std::shared_ptr<SoundIoPrivate> si)
{
    SoundIoWasapi& siw = si->backend_data->wasapi;
    int err;

    siw.device_scan_queued = true;

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

    // siw->device_events.lpVtbl = &soundio_MMNotificationClient;


    if ((err = soundio_os_thread_create(device_thread_run, si, NULL, &siw.thread)))
    {
        destroy_wasapi(si);
        return err;
    }

    siw.device_events = std::make_unique<soundio_NotificationClient>(si);

    si->destroy = destroy_wasapi;
    si->flush_events = flush_events_ca;
    si->wait_events = wait_events_ca;
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
