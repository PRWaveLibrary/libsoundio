//
// Created by mda on 2026/11/16.
//
// PipeWire backend for libsoundio.
//
// 结构上与 wasapi.cpp 保持一致：
//   * backend 一条 thread，负责 registry 监听与设备重扫，结果放进 ready_devices_info，
//     由 flush_events 搬到 safe_devices_info 并回调 on_devices_change。
//   * 每条 stream 一条独立线程 + 一条 pw_thread_loop，打开流程在线程里做，
//     主线程通过 cond / open_complete 等待结果。
//
// 数据通路：
//   pw_stream 的 process 回调就是实时线程。回调里向用户要数据（write_callback），
//   用户通过 begin_write/end_write 写进 ring buffer，再从这里拷进 PipeWire 的 buffer。
//   采集方向相反：先拷进 ring buffer，再调 read_callback 让用户取走。

#include "pipewire.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/raw-utils.h>
#include <spa/utils/dict.h>

#include "ring_buffer.h"
#include "soundio_private.h"
#include "util/logger.hpp"

#define DEFAULT_LATENCY (256.0 / 48000.0)

namespace
{
    struct PwNodeInfo
    {
        std::uint32_t id = 0;
        std::uint32_t device_id = 0;
        SoundIoDeviceAim aim = SoundIoDeviceAimOutput;
        std::wstring node_name;
        std::wstring description;
        int channels = 2;
        std::vector<SoundIoChannelId> positions;
        double period_duration = 0.0;
    };

    struct ChannelMapEntry
    {
        const char* name;
        SoundIoChannelId id;
        spa_audio_channel spa;
    };

} // namespace

// ============================================================================
// 0. 字符串 / 枚举转换
// ============================================================================

//! node.name 只允许安全字符
static std::string SanitizeNodeName(const std::wstring& name)
{
    auto out = Util::String::to_string(name);
    if (out.empty())
    {
        out = "libsoundio";
    }
    for (auto& c: out)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.')
        {
            c = '_';
        }
    }
    return out;
}


// PipeWire 的 audio.position 用的是短名（"FL,FR"）
static const ChannelMapEntry kChannelMap[] = {
        {"MONO", SoundIoChannelIdFrontCenter, SPA_AUDIO_CHANNEL_MONO},
        {"FL", SoundIoChannelIdFrontLeft, SPA_AUDIO_CHANNEL_FL},
        {"FR", SoundIoChannelIdFrontRight, SPA_AUDIO_CHANNEL_FR},
        {"FC", SoundIoChannelIdFrontCenter, SPA_AUDIO_CHANNEL_FC},
        {"LFE", SoundIoChannelIdLfe, SPA_AUDIO_CHANNEL_LFE},
        {"SL", SoundIoChannelIdSideLeft, SPA_AUDIO_CHANNEL_SL},
        {"SR", SoundIoChannelIdSideRight, SPA_AUDIO_CHANNEL_SR},
        {"BL", SoundIoChannelIdBackLeft, SPA_AUDIO_CHANNEL_RL},
        {"BR", SoundIoChannelIdBackRight, SPA_AUDIO_CHANNEL_RR},
        {"RL", SoundIoChannelIdBackLeft, SPA_AUDIO_CHANNEL_RL},
        {"RR", SoundIoChannelIdBackRight, SPA_AUDIO_CHANNEL_RR},
        {"FLC", SoundIoChannelIdFrontLeftCenter, SPA_AUDIO_CHANNEL_FLC},
        {"FRC", SoundIoChannelIdFrontRightCenter, SPA_AUDIO_CHANNEL_FRC},
        {"BC", SoundIoChannelIdBackCenter, SPA_AUDIO_CHANNEL_RC},
        {"RC", SoundIoChannelIdBackCenter, SPA_AUDIO_CHANNEL_RC},
        {"TC", SoundIoChannelIdTopCenter, SPA_AUDIO_CHANNEL_TC},
        {"TFL", SoundIoChannelIdTopFrontLeft, SPA_AUDIO_CHANNEL_TFL},
        {"TFC", SoundIoChannelIdTopFrontCenter, SPA_AUDIO_CHANNEL_TFC},
        {"TFR", SoundIoChannelIdTopFrontRight, SPA_AUDIO_CHANNEL_TFR},
        {"TBL", SoundIoChannelIdTopBackLeft, SPA_AUDIO_CHANNEL_TRL},
        {"TBC", SoundIoChannelIdTopBackCenter, SPA_AUDIO_CHANNEL_TRC},
        {"TBR", SoundIoChannelIdTopBackRight, SPA_AUDIO_CHANNEL_TRR},
        {"FLW", SoundIoChannelIdFrontLeftWide, SPA_AUDIO_CHANNEL_FLW},
        {"FRW", SoundIoChannelIdFrontRightWide, SPA_AUDIO_CHANNEL_FRW},
        {"FLH", SoundIoChannelIdFrontLeftHigh, SPA_AUDIO_CHANNEL_FLH},
        {"FCH", SoundIoChannelIdFrontCenterHigh, SPA_AUDIO_CHANNEL_FCH},
        {"FRH", SoundIoChannelIdFrontRightHigh, SPA_AUDIO_CHANNEL_FRH},
};

static SoundIoChannelId ChannelIdFromPosition(const std::string& name)
{
    for (const auto& entry: kChannelMap)
    {
        if (name == entry.name)
        {
            return entry.id;
        }
    }
    return SoundIoChannelIdInvalid;
}

static enum spa_audio_channel SpaChannelFromId(const SoundIoChannelId id)
{
    for (const auto& entry: kChannelMap)
    {
        if (entry.id == id)
        {
            return entry.spa;
        }
    }
    return SPA_AUDIO_CHANNEL_UNKNOWN;
}

static enum spa_audio_format SpaFormatFromSoundIo(const SoundIoFormat format)
{
    switch (format)
    {
        case SoundIoFormatS8:
            return SPA_AUDIO_FORMAT_S8;
        case SoundIoFormatU8:
            return SPA_AUDIO_FORMAT_U8;
        case SoundIoFormatS16LE:
            return SPA_AUDIO_FORMAT_S16_LE;
        case SoundIoFormatS16BE:
            return SPA_AUDIO_FORMAT_S16_BE;
        case SoundIoFormatU16LE:
            return SPA_AUDIO_FORMAT_U16_LE;
        case SoundIoFormatU16BE:
            return SPA_AUDIO_FORMAT_U16_BE;
        case SoundIoFormatS24LE:
            return SPA_AUDIO_FORMAT_S24_LE;
        case SoundIoFormatS24BE:
            return SPA_AUDIO_FORMAT_S24_BE;
        case SoundIoFormatU24LE:
            return SPA_AUDIO_FORMAT_U24_LE;
        case SoundIoFormatU24BE:
            return SPA_AUDIO_FORMAT_U24_BE;
        case SoundIoFormatS32LE:
            return SPA_AUDIO_FORMAT_S32_LE;
        case SoundIoFormatS32BE:
            return SPA_AUDIO_FORMAT_S32_BE;
        case SoundIoFormatU32LE:
            return SPA_AUDIO_FORMAT_U32_LE;
        case SoundIoFormatU32BE:
            return SPA_AUDIO_FORMAT_U32_BE;
        case SoundIoFormatFloat32LE:
            return SPA_AUDIO_FORMAT_F32_LE;
        case SoundIoFormatFloat32BE:
            return SPA_AUDIO_FORMAT_F32_BE;
        case SoundIoFormatFloat64LE:
            return SPA_AUDIO_FORMAT_F64_LE;
        case SoundIoFormatFloat64BE:
            return SPA_AUDIO_FORMAT_F64_BE;
        default:
            return SPA_AUDIO_FORMAT_UNKNOWN;
    }
}

//! 解析 "1024/48000" 这种分式，返回秒
static double ParseFraction(const char* text, const double fallback)
{
    if (text == nullptr || *text == '\0')
    {
        return fallback;
    }
    char* end = nullptr;
    const double num = std::strtod(text, &end);
    if (end == text || num <= 0.0)
    {
        return fallback;
    }
    if (*end == '/')
    {
        char* end2 = nullptr;
        const double denom = std::strtod(end + 1, &end2);
        if (end2 != end + 1 && denom > 0.0)
        {
            return num / denom;
        }
    }
    return fallback;
}

//! metadata 里 default.audio.sink 的值形如 { "name": "alsa_output.xxx", "monitor": null }
static std::wstring ExtractMetadataName(const char* value)
{
    if (value == nullptr)
    {
        return {};
    }
    const std::string text = value;
    const auto key = text.find("\"name\"");
    if (key == std::string::npos)
    {
        return {};
    }
    auto pos = text.find(':', key + 6);
    if (pos == std::string::npos)
    {
        return {};
    }
    pos += 1;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
    {
        pos += 1;
    }
    if (pos >= text.size() || text[pos] != '"')
    {
        return {};
    }
    const auto end = text.find('"', pos + 1);
    if (end == std::string::npos)
    {
        return {};
    }
    return Util::String::to_wstring(text.substr(pos + 1, end - pos - 1));
}

// ============================================================================
// 1. registry 数据 (backend)
// ============================================================================

struct PipeWireBackend
{
    pw_thread_loop* thread_loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;
    pw_metadata* metadata = nullptr;

    // listener 会保存 events 指针，这些结构体必须活到 listener 被移除
    pw_core_events core_events = {};
    pw_registry_events registry_events = {};
    pw_metadata_events metadata_events = {};
    spa_hook core_hook = {};
    spa_hook registry_hook = {};
    spa_hook metadata_hook = {};

    // registry 数据：pw thread 写，device thread 读
    std::mutex data_mutex;
    std::vector<PwNodeInfo> nodes;
    std::unordered_map<std::uint32_t, std::wstring> device_names;
    std::wstring default_output_name;
    std::wstring default_input_name;

    // 同步一次 round-trip，等 registry 把 global 全送过来
    std::mutex sync_mutex;
    std::condition_variable sync_cv;
    int last_done_seq = 0;

    // registry 上有变化时用来排队重扫
    std::weak_ptr<SoundIoPrivate> owner;
};

static void QueueDeviceScan(const std::shared_ptr<SoundIoPrivate>& si);

static int OnMetadataProperty(void* data, uint32_t subject, const char* key, const char* type, const char* value)
{
    const auto backend = static_cast<PipeWireBackend*>(data);
    if (key == nullptr)
    {
        return 0;
    }
    const std::string name = key;
    if (name != "default.audio.sink" && name != "default.audio.source" && name != "default.configured.audio.sink" && name != "default.configured.audio.source")
    {
        return 0;
    }
    const auto node_name = ExtractMetadataName(value);

    bool changed = false;
    {
        std::lock_guard lock(backend->data_mutex);
        std::wstring& slot = (name == "default.audio.sink" || name == "default.configured.audio.sink") ? backend->default_output_name : backend->default_input_name;
        if (slot != node_name)
        {
            slot = node_name;
            changed = true;
        }
    }

    // 默认设备变了：is_default 标记跟着变，必须重扫一次并投递 on_devices_change
    if (changed)
    {
        LOGD(L"pipewire: default device changed, rescan queued");
        if (const auto owner = backend->owner.lock())
        {
            QueueDeviceScan(owner);
        }
    }
    return 0;
}

static void ParsePositions(const char* text, PwNodeInfo& node)
{
    node.positions.clear();
    if (text == nullptr)
    {
        return;
    }
    const std::string all = text;
    std::size_t start = 0;
    while (start <= all.size())
    {
        const auto comma = all.find(',', start);
        const auto piece = all.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!piece.empty() && node.positions.size() < static_cast<std::size_t>(SOUNDIO_MAX_CHANNELS))
        {
            node.positions.push_back(ChannelIdFromPosition(piece));
        }
        if (comma == std::string::npos)
        {
            break;
        }
        start = comma + 1;
    }
}

static void OnRegistryGlobal(void* data, uint32_t id, uint32_t permissions, const char* type, uint32_t version, const spa_dict* props)
{
    const auto backend = static_cast<PipeWireBackend*>(data);
    if (props == nullptr || type == nullptr)
    {
        return;
    }
    const std::string iface = type;

    if (iface == PW_TYPE_INTERFACE_Metadata)
    {
        if (backend->metadata != nullptr)
        {
            return;
        }
        // 图里有多个 metadata 对象（settings / route-settings / sm-settings / default …），
        // 只有名字是 "default" 的那个存着 default.audio.sink / default.audio.source，
        // 绑错了就永远收不到默认设备变化。
        const char* metadata_name = spa_dict_lookup(props, "metadata.name");
        if (metadata_name == nullptr || std::string(metadata_name) != "default")
        {
            return;
        }
        backend->metadata = static_cast<pw_metadata*>(pw_registry_bind(backend->registry, id, PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0));
        if (backend->metadata != nullptr)
        {
            backend->metadata_events.version = PW_VERSION_METADATA_EVENTS;
            backend->metadata_events.property = OnMetadataProperty;
            pw_metadata_add_listener(backend->metadata, &backend->metadata_hook, &backend->metadata_events, backend);
        }
        return;
    }

    if (iface == PW_TYPE_INTERFACE_Device)
    {
        const char* name = spa_dict_lookup(props, PW_KEY_DEVICE_NAME);
        if (name == nullptr)
        {
            name = spa_dict_lookup(props, PW_KEY_DEVICE_DESCRIPTION);
        }
        std::lock_guard lock(backend->data_mutex);
        if (name != nullptr)
        {
            backend->device_names[id] = Util::String::to_wstring(name);
        }
        return;
    }

    if (iface != PW_TYPE_INTERFACE_Node)

    {
        return;
    }

    const char* media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    if (media_class == nullptr)
    {
        return;
    }
    const std::string klass = media_class;
    PwNodeInfo node;
    if (klass == "Audio/Sink")
    {
        node.aim = SoundIoDeviceAimOutput;
    }
    else if (klass == "Audio/Source")
    {
        node.aim = SoundIoDeviceAimInput;
    }
    else
    {
        return;
    }

    node.id = id;
    const char* device_id = spa_dict_lookup(props, PW_KEY_DEVICE_ID);
    node.device_id = device_id != nullptr ? static_cast<std::uint32_t>(std::strtoul(device_id, nullptr, 10)) : 0;

    const char* node_name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    const char* node_desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
    const char* node_nick = spa_dict_lookup(props, PW_KEY_NODE_NICK);
    node.node_name = Util::String::to_wstring(node_name);
    if (node_desc != nullptr)
    {
        node.description = Util::String::to_wstring(node_desc);
    }
    else if (node_nick != nullptr)
    {
        node.description = Util::String::to_wstring(node_nick);
    }
    else
    {
        node.description = node.node_name;
    }

    ParsePositions(spa_dict_lookup(props, SPA_KEY_AUDIO_POSITION), node);
    const char* channels = spa_dict_lookup(props, SPA_KEY_AUDIO_CHANNELS);
    node.channels = channels != nullptr ? static_cast<int>(std::strtol(channels, nullptr, 10)) : 0;
    if (node.channels <= 0)
    {
        node.channels = static_cast<int>(node.positions.size());
    }
    if (node.channels <= 0)
    {
        node.channels = 2;
    }
    if (node.channels > SOUNDIO_MAX_CHANNELS)
    {
        node.channels = SOUNDIO_MAX_CHANNELS;
    }

    node.period_duration = ParseFraction(spa_dict_lookup(props, PW_KEY_NODE_LATENCY), DEFAULT_LATENCY);

    {
        std::lock_guard lock(backend->data_mutex);
        bool replaced = false;
        for (auto& existing: backend->nodes)
        {
            if (existing.id == node.id)
            {
                existing = node;
                replaced = true;
                break;
            }
        }
        if (!replaced)
        {
            backend->nodes.push_back(node);
        }
    }

    LOGD(L"pipewire node %u '%ls' (%hs)", node.id, node.node_name.c_str(), klass.c_str());

    if (const auto owner = backend->owner.lock())
    {
        QueueDeviceScan(owner);
    }
}

static void OnRegistryGlobalRemove(void* data, uint32_t id)
{
    const auto backend = static_cast<PipeWireBackend*>(data);
    if (backend == nullptr)
    {
        return;
    }
    {
        std::lock_guard lock(backend->data_mutex);
        std::erase_if(backend->nodes, [id](const PwNodeInfo& node) { return node.id == id; });
        backend->device_names.erase(id);
    }
    LOGD(L"pipewire global %u removed", id);

    if (const auto owner = backend->owner.lock())
    {
        QueueDeviceScan(owner);
    }
}

static void OnCoreDone(void* data, uint32_t id, int seq)
{
    const auto backend = static_cast<PipeWireBackend*>(data);
    if (backend == nullptr || id != PW_ID_CORE)
    {
        return;
    }
    {
        std::lock_guard lock(backend->sync_mutex);
        backend->last_done_seq = seq;
    }
    backend->sync_cv.notify_all();
}

//! 发一次 sync 并等它回来，保证 registry 的 global 已经全部到达
static bool BackendSync(const std::shared_ptr<PipeWireBackend>& backend, const int timeout_ms)
{
    if (backend == nullptr || backend->thread_loop == nullptr || backend->core == nullptr)
    {
        return false;
    }

    pw_thread_loop_lock(backend->thread_loop);
    const int seq = pw_core_sync(backend->core, PW_ID_CORE, 0);
    pw_thread_loop_unlock(backend->thread_loop);
    if (seq < 0)
    {
        return false;
    }

    std::unique_lock lock(backend->sync_mutex);
    return backend->sync_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [backend, seq] { return backend->last_done_seq >= seq; });
}

// ============================================================================
// 2. 设备枚举
// ============================================================================

static std::shared_ptr<SoundIoDevicePrivate> BuildDevice(const std::shared_ptr<SoundIoPrivate>& si, const PwNodeInfo& node, const std::unordered_map<std::uint32_t, std::wstring>& device_names)
{
    auto dev = std::make_shared<SoundIoDevicePrivate>();
    dev->soundio = si;
    dev->probe_error = SoundIoErrorNone;
    dev->is_raw = false;
    dev->aim = node.aim;

    dev->id = node.node_name.empty() ? std::to_wstring(node.id) : node.node_name;
    const auto device_name = device_names.find(node.device_id);
    dev->container_id = (device_name != device_names.end() && !device_name->second.empty()) ? device_name->second : dev->id;
    dev->name = node.description.empty() ? dev->id : node.description;

    // 通道布局：优先用 node 上声明的 audio.position
    const int channel_count = std::clamp(node.channels, 1, static_cast<int>(SOUNDIO_MAX_CHANNELS));
    SoundIoChannelLayout layout = {};
    if (static_cast<int>(node.positions.size()) == channel_count)
    {
        layout.channel_count = channel_count;
        for (int i = 0; i < channel_count; i += 1)
        {
            layout.channels[i] = node.positions[i];
        }
        soundio_channel_layout_detect_builtin(&layout);
    }
    else
    {
        const auto* builtin = soundio_channel_layout_get_default(channel_count);
        if (builtin != nullptr)
        {
            layout = *builtin;
        }
        else
        {
            layout.channel_count = channel_count;
            for (int i = 0; i < channel_count; i += 1)
            {
                layout.channels[i] = SoundIoChannelIdInvalid;
            }
        }
    }
    dev->layouts.push_back(layout);
    if (channel_count != 2)
    {
        // 上层（WaveAudio）按 stereo 协商，这里补一个可用的布局
        if (const auto* stereo = soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdStereo))
        {
            dev->layouts.push_back(*stereo);
        }
    }
    dev->current_layout = layout;

    // graph 内部会做格式 / 采样率转换，这里给出一组常用的能力
    dev->formats.push_back(SoundIoFormatFloat32NE);
    dev->formats.push_back(SoundIoFormatS32NE);
    dev->formats.push_back(SoundIoFormatS16NE);
    dev->formats.push_back(SoundIoFormatS24NE);
    dev->formats.push_back(SoundIoFormatFloat64NE);
    dev->formats.push_back(SoundIoFormatU8);
    dev->current_format = SoundIoFormatFloat32NE;

    // SoundIoDevicePrivate 里也有一个同名的 sample_rates（独立成员，会遮蔽基类那份），
    // 上层（soundio_device_nearest_sample_rate 等）读的是 SoundIoDevice::sample_rates。
    static_cast<SoundIoDevice*>(dev.get())->sample_rates.emplace_back(SOUNDIO_MIN_SAMPLE_RATE, SOUNDIO_MAX_SAMPLE_RATE);
    dev->sample_rate_current = 48000;

    dev->software_latency_min = 0.0;
    dev->software_latency_max = 2.0;
    dev->software_latency_current = node.period_duration;

    const auto& dw = dev->backend_data.pipe_wire;
    dw->node_id = node.id;
    dw->node_name = node.node_name;
    dw->period_duration = node.period_duration;

    return dev;
}

/**
 * @brief 重新枚举 PipeWire graph 中的音频节点
 *
 * 从 registry 缓存里取节点列表，构造 SoundIoDevicesInfo 并交给 event 线程。
 *
 * @param si 当前的 SoundIoPrivate
 * @return int 错误码，0 表示成功
 */
static int refresh_devices(const std::shared_ptr<SoundIoPrivate>& si)
{
    auto& siw = si->backend_data->pipe_wire;
    std::shared_ptr<PipeWireBackend> backend = siw.backend;
    if (backend == nullptr)
    {
        return SoundIoErrorInitAudioBackend;
    }

    // 把「等 registry 数据」和「读 registry 数据」分开：sync 完再上锁拷贝
    if (!BackendSync(backend, 5000))
    {
        return SoundIoErrorInitAudioBackend;
    }

    std::vector<PwNodeInfo> nodes;
    std::unordered_map<std::uint32_t, std::wstring> device_names;
    std::wstring default_output_name;
    std::wstring default_input_name;
    {
        std::lock_guard lock(backend->data_mutex);
        nodes = backend->nodes;
        device_names = backend->device_names;
        default_output_name = backend->default_output_name;
        default_input_name = backend->default_input_name;
    }

    auto devices_info = std::make_unique<SoundIoDevicesInfo>();
    devices_info->default_input_index = -1;
    devices_info->default_output_index = -1;

    for (const auto& node: nodes)
    {
        auto dev = BuildDevice(si, node, device_names);
        auto& list = node.aim == SoundIoDeviceAimOutput ? devices_info->output_devices : devices_info->input_devices;

        const std::wstring& default_name = node.aim == SoundIoDeviceAimOutput ? default_output_name : default_input_name;
        if (!default_name.empty() && node.node_name == default_name)
        {
            dev->is_default = true;
            if (node.aim == SoundIoDeviceAimOutput)
            {
                devices_info->default_output_index = static_cast<int>(list.size());
            }
            else
            {
                devices_info->default_input_index = static_cast<int>(list.size());
            }
        }
        list.push_back(dev);
    }

    // metadata 没给默认设备时，退化为第一个
    if (devices_info->default_output_index < 0 && !devices_info->output_devices.empty())
    {
        devices_info->default_output_index = 0;
        devices_info->output_devices.front()->is_default = true;
    }
    if (devices_info->default_input_index < 0 && !devices_info->input_devices.empty())
    {
        devices_info->default_input_index = 0;
        devices_info->input_devices.front()->is_default = true;
    }

    {
        std::unique_lock lock(siw.mutex->get());
        siw.ready_devices_info = std::move(devices_info);
        siw.have_devices_flag = true;
        siw.cond->signal(&lock);
    }
    si->on_events_signal(si);
    return SoundIoErrorNone;
}

// ============================================================================
// 3. Backend 事件 / 生命周期
// ============================================================================

static void shutdown_backend(const std::shared_ptr<SoundIoPrivate>& si, const int err)
{
    auto& siw = si->backend_data->pipe_wire;
    std::unique_lock lock(siw.mutex->get());
    siw.shutdown_err = err;
    siw.cond->signal(&lock);
    si->on_events_signal(si);
}

static void my_flush_events(const std::shared_ptr<SoundIoPrivate>& si, bool wait)
{
    auto& siw = si->backend_data->pipe_wire;

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

static void flush_events_pipewire(const std::shared_ptr<SoundIoPrivate>& si)
{
    my_flush_events(si, false);
}

static void wait_events_pipewire(const std::shared_ptr<SoundIoPrivate>& si)
{
    my_flush_events(si, true);
}

static void wakeup_pipewire(const std::shared_ptr<SoundIoPrivate>& si)
{
    auto& siw = si->backend_data->pipe_wire;
    std::unique_lock lock(siw.mutex->get());
    siw.cond->signal(&lock);
}

static void force_device_scan_pipewire(const std::shared_ptr<SoundIoPrivate>& si)
{
    auto& siw = si->backend_data->pipe_wire;

    {
        std::unique_lock lock(siw.mutex->get());
        siw.have_devices_flag = false;
    }

    std::unique_lock scan_lock(siw.scan_devices_mutex->get());
    siw.device_scan_queued.store(true, std::memory_order_release);
    siw.scan_devices_cond->signal(&scan_lock);
    LOGD(L"force_device_scan_pipewire");
}

static void QueueDeviceScan(const std::shared_ptr<SoundIoPrivate>& si)
{
    if (si != nullptr)
    {
        force_device_scan_pipewire(si);
    }
}

/**
 * @brief 设备变化监听线程
 *
 * 建立 registry / metadata 连接，registry 上有变化时重新枚举设备。
 */
static void device_thread_run(const std::stop_token& token, const std::shared_ptr<void>& arg)
{
    const auto si = std::static_pointer_cast<SoundIoPrivate>(arg);
    auto& siw = si->backend_data->pipe_wire;

    auto backend = std::make_shared<PipeWireBackend>();
    backend->owner = si;
    siw.backend = backend;

    backend->thread_loop = pw_thread_loop_new("soundio-pw-detect", nullptr);
    if (backend->thread_loop == nullptr)
    {
        shutdown_backend(si, SoundIoErrorSystemResources);
        siw.backend = nullptr;
        return;
    }

    bool ok = true;
    pw_thread_loop_lock(backend->thread_loop);
    backend->context = pw_context_new(pw_thread_loop_get_loop(backend->thread_loop), nullptr, 0);
    if (backend->context == nullptr || (backend->core = pw_context_connect(backend->context, nullptr, 0)) == nullptr)
    {
        ok = false;
    }
    if (ok)
    {
        backend->core_events.version = PW_VERSION_CORE_EVENTS;
        backend->core_events.done = OnCoreDone;
        pw_core_add_listener(backend->core, &backend->core_hook, &backend->core_events, backend.get());

        backend->registry = pw_core_get_registry(backend->core, PW_VERSION_REGISTRY, 0);
        if (backend->registry == nullptr)
        {
            ok = false;
        }
        else
        {
            backend->registry_events.version = PW_VERSION_REGISTRY_EVENTS;
            backend->registry_events.global = OnRegistryGlobal;
            backend->registry_events.global_remove = OnRegistryGlobalRemove;
            pw_registry_add_listener(backend->registry, &backend->registry_hook, &backend->registry_events, backend.get());
        }
    }
    pw_thread_loop_unlock(backend->thread_loop);

    if (!ok || pw_thread_loop_start(backend->thread_loop) < 0)
    {
        LOGI(L"pipewire: cannot connect to daemon");
        shutdown_backend(si, SoundIoErrorInitAudioBackend);
    }
    else
    {
        // refresh_devices 里要去等 pw 线程，不能持锁执行，作用域结束锁自动释放。
        while (!token.stop_requested())
        {
            {
                std::unique_lock lock(siw.scan_devices_mutex->get());
                siw.scan_devices_cond->wait(&lock, [&]
                {
                    return token.stop_requested() || siw.device_scan_queued.load(std::memory_order_acquire);
                });

                if (token.stop_requested())
                {
                    break;
                }
                siw.device_scan_queued.store(false, std::memory_order_release);
            }

            if (const int err = refresh_devices(si))
            {
                shutdown_backend(si, err);
                break;
            }

            flush_events_pipewire(si);
        }
    }

    // 拆除 registry / metadata 连接
    if (backend->thread_loop != nullptr)
    {
        pw_thread_loop_stop(backend->thread_loop);
    }
    if (backend->metadata != nullptr)
    {
        spa_hook_remove(&backend->metadata_hook);
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(backend->metadata));
        backend->metadata = nullptr;
    }
    if (backend->registry != nullptr)
    {
        spa_hook_remove(&backend->registry_hook);
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(backend->registry));
        backend->registry = nullptr;
    }
    if (backend->core != nullptr)
    {
        pw_core_disconnect(backend->core);
        backend->core = nullptr;
    }
    if (backend->context != nullptr)
    {
        pw_context_destroy(backend->context);
        backend->context = nullptr;
    }
    if (backend->thread_loop != nullptr)
    {
        pw_thread_loop_destroy(backend->thread_loop);
        backend->thread_loop = nullptr;
    }

    siw.backend = nullptr;
}

// ============================================================================
// 4. 每条流共用的 PipeWire 资源
// ============================================================================

struct PipeWireStream
{
    bool is_output = false;

    pw_thread_loop* thread_loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_stream* stream = nullptr;
    pw_stream_events events = {};
    spa_hook stream_hook = {};
    //! pw_thread_loop_start() 成功过才允许 stop / destroy，否则属于对未启动的 loop 操作
    bool loop_started = false;
    //! 开始拆流：实时回调直接返回，不再回调用户代码
    std::atomic_bool stopping{false};

    std::shared_ptr<SoundIoRingBuffer> ring_buffer;
    int bytes_per_frame = 0;
    int frames_per_period = 0;
    std::string node_name;

    // 打开过程中的状态同步（state_changed 在 pw 线程里跑）
    std::mutex state_mutex;
    std::condition_variable state_cv;
    pw_stream_state state = PW_STREAM_STATE_UNCONNECTED;
    bool state_error = false;

    // 回调期间保证 stream 对象存活
    std::shared_ptr<SoundIoOutStreamPrivate> os;
    std::shared_ptr<SoundIoInStreamPrivate> is;
};

//! 每条流一个唯一序号，避免同名节点被 PipeWire 改名字
static std::atomic<unsigned> g_stream_serial = 0;

static void OnStreamStateChanged(void* data, pw_stream_state old, pw_stream_state state, const char* error)
{
    const auto impl = static_cast<PipeWireStream*>(data);
    {
        std::lock_guard lock(impl->state_mutex);
        impl->state = state;
        if (state == PW_STREAM_STATE_ERROR)
        {
            impl->state_error = true;
        }
    }
    impl->state_cv.notify_all();

    if (state == PW_STREAM_STATE_ERROR)
    {
        LOGI(L"pipewire: stream error : %hs", error != nullptr ? error : "unknown");
    }
}

//! 等 pw_stream 连上（PAUSED / STREAMING 都算连上）
static bool WaitStreamReady(PipeWireStream* impl, const int timeout_ms)
{
    std::unique_lock lock(impl->state_mutex);
    return impl->state_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [impl] { return impl->state == PW_STREAM_STATE_PAUSED || impl->state == PW_STREAM_STATE_STREAMING || impl->state == PW_STREAM_STATE_ERROR; }) &&
           !impl->state_error;
}

/**
 * @brief 建立一条流的 PipeWire 资源
 *
 * 创建 thread loop / context / core / stream 并按 libsoundio 的 layout 协商格式。
 *
 * @param impl 流资源容器
 * @param direction 输出 / 输入
 * @param target_id 目标 node（0 表示交给 PipeWire 自动连到默认设备）
 * @param os 输出流（采集时为 nullptr）
 * @param is 采集流（输出时为 nullptr）
 * @return int 错误码，0 表示成功
 */
static int StreamOpen(PipeWireStream* impl, const pw_direction direction, const std::uint32_t target_id, const std::shared_ptr<SoundIoOutStreamPrivate>& os, const std::shared_ptr<SoundIoInStreamPrivate>& is)
{
    impl->os = os;
    impl->is = is;

    const int channel_count = os != nullptr ? os->layout.channel_count : is->layout.channel_count;
    const int sample_rate = os != nullptr ? os->sample_rate : is->sample_rate;
    const SoundIoFormat format = os != nullptr ? os->format : is->format;
    const int bytes_per_frame = os != nullptr ? os->bytes_per_frame : is->bytes_per_frame;
    const std::wstring& stream_name = os != nullptr ? os->name : is->name;

    impl->bytes_per_frame = bytes_per_frame;
    const auto node_description_utf8 = SanitizeNodeName(stream_name);
    // node.name 要唯一，否则同名节点会被 PipeWire 改名；可读名字放进 node.description
    impl->node_name = node_description_utf8 + "." + std::to_string(++g_stream_serial);

    // 期望一个 graph 周期的帧数
    double latency = os != nullptr ? os->software_latency : is->software_latency;
    if (latency <= 0.0)
    {
        latency = 0.02;
    }
    int frames = static_cast<int>(std::lround(latency * sample_rate));
    frames = std::clamp(frames, 64, 8192);
    impl->frames_per_period = frames;

    // ring buffer 留 4 个周期的余量
    impl->ring_buffer = std::make_shared<SoundIoRingBuffer>();
    if (const int err = impl->ring_buffer->init(frames * bytes_per_frame * 4))
    {
        return err;
    }

    impl->thread_loop = pw_thread_loop_new(impl->is_output ? "soundio-pw-out" : "soundio-pw-in", nullptr);
    if (impl->thread_loop == nullptr)
    {
        return SoundIoErrorNoMem;
    }

    pw_thread_loop_lock(impl->thread_loop);
    impl->context = pw_context_new(pw_thread_loop_get_loop(impl->thread_loop), nullptr, 0);
    if (impl->context == nullptr)
    {
        pw_thread_loop_unlock(impl->thread_loop);
        return SoundIoErrorNoMem;
    }
    impl->core = pw_context_connect(impl->context, nullptr, 0);
    if (impl->core == nullptr)
    {
        pw_thread_loop_unlock(impl->thread_loop);
        return SoundIoErrorOpeningDevice;
    }

    char latency_text[64];
    std::snprintf(latency_text, sizeof(latency_text), "%d/%d", frames, sample_rate);

    // clang-format off
    pw_properties* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, impl->is_output ? "Playback" : "Capture",
        PW_KEY_MEDIA_ROLE, "Production",
        PW_KEY_NODE_NAME, impl->node_name.c_str(),
        PW_KEY_NODE_DESCRIPTION, node_description_utf8.c_str(),
        PW_KEY_NODE_LATENCY, latency_text,
        nullptr);
    // clang-format on

    if (props == nullptr)
    {
        pw_thread_loop_unlock(impl->thread_loop);
        return SoundIoErrorNoMem;
    }

    impl->stream = pw_stream_new(impl->core, impl->node_name.c_str(), props);
    if (impl->stream == nullptr)
    {
        pw_thread_loop_unlock(impl->thread_loop);
        return SoundIoErrorNoMem;
    }

    impl->events.version = PW_VERSION_STREAM_EVENTS;
    impl->events.state_changed = OnStreamStateChanged;
    // impl->events.process 由调用方（outstream_do_open / instream_do_open）预先设置
    pw_stream_add_listener(impl->stream, &impl->stream_hook, &impl->events, impl);

    // 协商格式
    std::uint8_t pod_buffer[1024] = {};
    struct spa_pod_builder builder = {};
    builder.data = pod_buffer;
    builder.size = static_cast<std::uint32_t>(sizeof(pod_buffer));
    struct spa_audio_info_raw info = {};
    info.format = SpaFormatFromSoundIo(format);
    info.rate = static_cast<std::uint32_t>(sample_rate);
    info.channels = static_cast<std::uint32_t>(channel_count);
    for (int ch = 0; ch < channel_count && ch < static_cast<int>(SPA_AUDIO_MAX_CHANNELS); ch += 1)
    {
        const SoundIoChannelId id = os != nullptr ? os->layout.channels[ch] : is->layout.channels[ch];
        info.position[ch] = SpaChannelFromId(id);
    }

    const struct spa_pod* params[1] = {};
    params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);
    if (params[0] == nullptr)
    {
        pw_thread_loop_unlock(impl->thread_loop);
        return SoundIoErrorIncompatibleDevice;
    }

    const std::uint32_t target = target_id != 0 ? target_id : static_cast<std::uint32_t>(PW_ID_ANY);
    const auto flags = static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_INACTIVE | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS);
    const int err = pw_stream_connect(impl->stream, direction, target, flags, params, 1);
    pw_thread_loop_unlock(impl->thread_loop);
    if (err < 0)
    {
        LOGI(L"pipewire: connect '%ls' to node %u failed: %d", stream_name.c_str(), target, err);
        return SoundIoErrorOpeningDevice;
    }

    if (pw_thread_loop_start(impl->thread_loop) < 0)
    {
        return SoundIoErrorOpeningDevice;
    }
    impl->loop_started = true;

    if (!WaitStreamReady(impl, 5000))
    {
        return SoundIoErrorOpeningDevice;
    }

    return SoundIoErrorNone;
}

//! 流是否已激活（会 start / stop 数据流动）
static void StreamSetActive(const PipeWireStream* impl, const bool active)
{
    if (impl == nullptr || impl->thread_loop == nullptr || impl->stream == nullptr)
    {
        return;
    }
    pw_thread_loop_lock(impl->thread_loop);
    pw_stream_set_active(impl->stream, active);
    pw_thread_loop_unlock(impl->thread_loop);
}

static void StreamClose(PipeWireStream* impl)
{
    if (impl == nullptr)
    {
        return;
    }
    // 先让实时回调闭嘴，再停 loop / 拆 stream
    impl->stopping.store(true, std::memory_order_release);
    if (impl->thread_loop != nullptr && impl->stream != nullptr)
    {
        pw_thread_loop_lock(impl->thread_loop);
        pw_stream_set_active(impl->stream, false);
        pw_thread_loop_unlock(impl->thread_loop);
    }
    if (impl->thread_loop != nullptr && impl->loop_started)
    {
        pw_thread_loop_stop(impl->thread_loop);
        impl->loop_started = false;
    }
    if (impl->stream != nullptr)
    {
        spa_hook_remove(&impl->stream_hook);
        pw_stream_destroy(impl->stream);
        impl->stream = nullptr;
    }
    if (impl->core != nullptr)
    {
        pw_core_disconnect(impl->core);
        impl->core = nullptr;
    }
    if (impl->context != nullptr)
    {
        pw_context_destroy(impl->context);
        impl->context = nullptr;
    }
    if (impl->thread_loop != nullptr)
    {
        pw_thread_loop_destroy(impl->thread_loop);
        impl->thread_loop = nullptr;
    }
    impl->ring_buffer = nullptr;
    impl->os = nullptr;
    impl->is = nullptr;
}

static void StreamDestroy(PipeWireStream*& impl)
{
    if (impl == nullptr)
    {
        return;
    }
    StreamClose(impl);
    delete impl;
    impl = nullptr;
}

//! pw 时间快照，单位换算成秒
static bool StreamQueryTime(PipeWireStream* impl, double* out_time, double* out_latency)
{
    if (impl == nullptr || impl->stream == nullptr)
    {
        return false;
    }
    struct pw_time time = {};
    if (pw_stream_get_time_n(impl->stream, &time, sizeof(time)) < 0)
    {
        return false;
    }
    if (time.rate.denom == 0)
    {
        return false;
    }
    const double seconds_per_tick = static_cast<double>(time.rate.num) / static_cast<double>(time.rate.denom);
    if (out_time != nullptr)
    {
        *out_time = static_cast<double>(time.ticks) * seconds_per_tick;
    }
    if (out_latency != nullptr)
    {
        const auto delay = time.delay > 0 ? static_cast<std::uint64_t>(time.delay) : 0;
        *out_latency = static_cast<double>(time.queued + time.buffered + delay) * seconds_per_tick;
    }
    return true;
}

// ============================================================================
// 5. OutStream
// ============================================================================

//! 实时回调：向用户要数据，再从 ring buffer 搬进 PipeWire buffer
static void outstream_process(void* data)
{
    const auto impl = static_cast<PipeWireStream*>(data);
    const auto os = impl->os;
    if (os == nullptr || impl->stopping.load(std::memory_order_acquire))
    {
        return;
    }

    pw_buffer* buffer = pw_stream_dequeue_buffer(impl->stream);
    if (buffer == nullptr)
    {
        return;
    }

    const spa_buffer* spa_buf = buffer->buffer;
    const spa_data* p_spa_data = (spa_buf != nullptr && spa_buf->n_datas > 0) ? &spa_buf->datas[0] : nullptr;
    const int bytes_per_frame = impl->bytes_per_frame;
    if (p_spa_data == nullptr || p_spa_data->data == nullptr || bytes_per_frame <= 0)
    {
        pw_stream_queue_buffer(impl->stream, buffer);
        return;
    }

    auto max_frames = static_cast<int>(p_spa_data->maxsize / bytes_per_frame);
    std::int32_t wanted = buffer->requested > 0 ? static_cast<int>(buffer->requested) : max_frames;
    wanted = std::min(wanted, max_frames);
    if (wanted <= 0)
    {
        pw_stream_queue_buffer(impl->stream, buffer);
        return;
    }

    os->backend_data.pipe_wire.writable_frame_count = wanted;
    if (os->write_callback != nullptr)
    {
        os->write_callback(os, wanted, wanted);
    }

    auto* destination = static_cast<char*>(p_spa_data->data);
    int frames = 0;
    if (impl->ring_buffer != nullptr)
    {
        frames = std::min(static_cast<std::int32_t>(impl->ring_buffer->fill_count() / bytes_per_frame), wanted);
        if (frames > 0)
        {
            std::memcpy(destination, impl->ring_buffer->read_ptr(), static_cast<std::size_t>(frames) * bytes_per_frame);
            impl->ring_buffer->advance_read_ptr(frames * bytes_per_frame);
        }
    }
    if (frames < wanted)
    {
        std::memset(destination + static_cast<std::size_t>(frames) * bytes_per_frame, 0, static_cast<std::size_t>(wanted - frames) * bytes_per_frame);
        if (os->underflow_callback != nullptr)
        {
            os->underflow_callback(os);
        }
    }

    if (p_spa_data->chunk != nullptr)
    {
        p_spa_data->chunk->offset = 0;
        p_spa_data->chunk->stride = bytes_per_frame;
        p_spa_data->chunk->size = static_cast<std::uint32_t>(wanted) * bytes_per_frame;
    }
    buffer->size = static_cast<std::uint64_t>(wanted);

    pw_stream_queue_buffer(impl->stream, buffer);
}

/**
 * @brief 输出流的打开逻辑（在流线程里执行）
 */
static int outstream_do_open(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os)
{
    auto& osw = os->backend_data.pipe_wire;
    const auto device = std::dynamic_pointer_cast<SoundIoDevicePrivate>(os->device);
    if (device == nullptr)
    {
        return SoundIoErrorInvalid;
    }

    if (os->software_latency <= 0.0)
    {
        os->software_latency = device->backend_data.pipe_wire != nullptr ? device->backend_data.pipe_wire->period_duration : 0.02;
    }
    if (os->software_latency <= 0.0)
    {
        os->software_latency = 0.02;
    }
    const double min_latency = std::max(device->software_latency_min, 0.001);
    const double max_latency = device->software_latency_max > min_latency ? device->software_latency_max : 2.0;
    os->software_latency = std::clamp(os->software_latency, min_latency, max_latency);

    auto* impl = new PipeWireStream();
    impl->is_output = true;
    impl->events.version = PW_VERSION_STREAM_EVENTS;
    impl->events.process = outstream_process;
    osw.stream = impl;

    const std::uint32_t target_id = device->backend_data.pipe_wire != nullptr ? device->backend_data.pipe_wire->node_id : 0;
    const int err = StreamOpen(impl, PW_DIRECTION_OUTPUT, target_id, os, nullptr);
    if (err)
    {
        return err;
    }

    os->software_latency = static_cast<double>(impl->frames_per_period) / os->sample_rate;
    return SoundIoErrorNone;
}

static void outstream_thread_run(const std::stop_token& token, const std::shared_ptr<void>& arg)
{
    const auto os = std::static_pointer_cast<SoundIoOutStreamPrivate>(arg);
    auto& osw = os->backend_data.pipe_wire;
    const auto si = std::dynamic_pointer_cast<SoundIoPrivate>(os->device->soundio.lock());

    int err = outstream_do_open(si, os);

    std::unique_lock lock(osw.mutex->get());
    osw.open_complete = true;
    osw.open_err = err;
    osw.cond->signal(&lock);
    if (err)
    {
        lock.unlock();
        LOGI(L"pipewire: outstream '%ls' open failed: %d", os->name.c_str(), err);
        return;
    }

    for (;;)
    {
        if (token.stop_requested())
        {
            lock.unlock();
            return;
        }
        if (osw.started)
        {
            break;
        }
        osw.start_cond->wait(&lock);
    }
    lock.unlock();

    StreamSetActive(osw.stream, true);

    std::unique_lock wait_lock(osw.mutex->get());
    while (!token.stop_requested())
    {
        osw.cond->wait(&wait_lock);
    }
    wait_lock.unlock();

    StreamSetActive(osw.stream, false);
}

static void outstream_destroy_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os)
{
    LOGI(L"destroy pipewire outstream: %ls", os->name.c_str());
    auto& osw = os->backend_data.pipe_wire;

    if (osw.thread)
    {
        osw.thread->request_stop();
        {
            std::unique_lock lock(osw.mutex->get());
            osw.cond->signal(&lock);
            osw.start_cond->signal(&lock);
        }
        osw.thread = nullptr;
    }

    StreamDestroy(osw.stream);

    osw.stream_name = std::wstring();
    osw.cond = nullptr;
    osw.start_cond = nullptr;
    osw.mutex = nullptr;
}

static int outstream_open_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os)
{
    auto& osw = os->backend_data.pipe_wire;

    osw.stream_name = os->name;
    osw.cond = soundio_os_cond_create();
    if (osw.cond == nullptr)
    {
        outstream_destroy_pipewire(si, os);
        return SoundIoErrorNoMem;
    }
    osw.start_cond = soundio_os_cond_create();
    if (osw.start_cond == nullptr)
    {
        outstream_destroy_pipewire(si, os);
        return SoundIoErrorNoMem;
    }
    osw.mutex = soundio_os_mutex_create();
    if (osw.mutex == nullptr)
    {
        outstream_destroy_pipewire(si, os);
        return SoundIoErrorNoMem;
    }

    osw.open_complete = false;
    osw.open_err = 0;
    osw.started = false;
    osw.is_paused = false;
    osw.thread = SoundIoOsThread::create(outstream_thread_run, os);
    if (!osw.thread)
    {
        outstream_destroy_pipewire(si, os);
        return SoundIoErrorNoMem;
    }

    std::unique_lock lock(osw.mutex->get());
    while (!osw.open_complete)
    {
        osw.cond->wait(&lock);
    }
    lock.unlock();

    if (osw.open_err)
    {
        outstream_destroy_pipewire(si, os);
        return osw.open_err;
    }

    os->paused = false;
    return SoundIoErrorNone;
}

static int outstream_start_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os)
{
    auto& osw = os->backend_data.pipe_wire;
    std::unique_lock lock(osw.mutex->get());
    osw.started = true;
    osw.start_cond->signal(&lock);
    return SoundIoErrorNone;
}

static int outstream_begin_write_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os, SoundIoChannelArea** out_areas, const int frame_count)
{
    auto& osw = os->backend_data.pipe_wire;
    auto* impl = osw.stream;
    if (impl == nullptr || impl->ring_buffer == nullptr)
    {
        return SoundIoErrorStreaming;
    }

    const int bytes_per_frame = impl->bytes_per_frame;
    const int capacity_frames = static_cast<std::int32_t>(impl->ring_buffer->capacity()) / bytes_per_frame;
    int frames = frame_count;
    int free_frames = static_cast<std::int32_t>(impl->ring_buffer->free_count()) / bytes_per_frame;
    if (frames > free_frames)
    {
        // 空间不够就丢掉尚未播放的数据，宁可丢帧也不要卡死
        impl->ring_buffer->clear();
        free_frames = capacity_frames;
        frames = std::min(frames, free_frames);
    }
    osw.write_frame_count = frames;

    char* base = impl->ring_buffer->write_ptr();
    for (int ch = 0; ch < os->layout.channel_count; ch += 1)
    {
        osw.areas[ch].ptr = base + ch * os->bytes_per_sample;
        osw.areas[ch].step = bytes_per_frame;
    }
    *out_areas = osw.areas;
    return SoundIoErrorNone;
}

static int outstream_end_write_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os)
{
    auto& osw = os->backend_data.pipe_wire;
    auto* impl = osw.stream;
    if (impl == nullptr || impl->ring_buffer == nullptr)
    {
        return SoundIoErrorStreaming;
    }
    impl->ring_buffer->advance_write_ptr(osw.write_frame_count * impl->bytes_per_frame);
    return SoundIoErrorNone;
}

static int outstream_clear_buffer_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os)
{
    auto& osw = os->backend_data.pipe_wire;
    if (osw.stream == nullptr || osw.stream->ring_buffer == nullptr)
    {
        return SoundIoErrorStreaming;
    }
    osw.stream->ring_buffer->clear();
    return SoundIoErrorNone;
}

static int outstream_pause_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os, const bool pause)
{
    auto& osw = os->backend_data.pipe_wire;
    os->paused = pause;
    if (osw.is_paused == pause)
    {
        return SoundIoErrorNone;
    }
    osw.is_paused = pause;
    if (!pause && osw.stream != nullptr && osw.stream->ring_buffer != nullptr)
    {
        // 恢复播放时丢掉暂停前剩下的数据，否则会先听到一段旧音频
        osw.stream->ring_buffer->clear();
    }
    StreamSetActive(osw.stream, !pause);
    return SoundIoErrorNone;
}

static int outstream_get_latency_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os, double* out_latency)
{
    const auto& osw = os->backend_data.pipe_wire;
    if (StreamQueryTime(osw.stream, nullptr, out_latency))
    {
        return SoundIoErrorNone;
    }
    *out_latency = os->software_latency;
    return SoundIoErrorNone;
}

static int outstream_get_time_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os, double* out_time)
{
    const auto& osw = os->backend_data.pipe_wire;
    if (StreamQueryTime(osw.stream, out_time, nullptr))
    {
        return SoundIoErrorNone;
    }
    *out_time = 0.0;
    return -1;
}

static int outstream_set_volume_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoOutStreamPrivate>& os, const float volume)
{
    // PipeWire 侧不做增益，交给上层混音自己乘
    os->volume.store(volume, std::memory_order_release);
    return SoundIoErrorNone;
}

// ============================================================================
// 6. InStream
// ============================================================================

//! 实时回调：把 graph 采集到的数据放进 ring buffer，再让用户取走
static void instream_process(void* data)
{
    const auto impl = static_cast<PipeWireStream*>(data);
    const auto is = impl->is;
    if (is == nullptr)
    {
        return;
    }
    if (impl->stopping.load(std::memory_order_acquire))
    {
        return;
    }

    pw_buffer* buffer = pw_stream_dequeue_buffer(impl->stream);
    if (buffer == nullptr)
    {
        return;
    }

    spa_buffer* spa_buf = buffer->buffer;
    spa_data* spa_data0 = (spa_buf != nullptr && spa_buf->n_datas > 0) ? &spa_buf->datas[0] : nullptr;
    const int bytes_per_frame = impl->bytes_per_frame;

    bool overflow = false;
    if (spa_data0 != nullptr && spa_data0->data != nullptr && spa_data0->chunk != nullptr && bytes_per_frame > 0 && impl->ring_buffer != nullptr)
    {
        const int stride = spa_data0->chunk->stride > 0 ? spa_data0->chunk->stride : bytes_per_frame;
        int frames = static_cast<int>(spa_data0->chunk->size / static_cast<std::uint32_t>(stride));
        const auto* source = static_cast<const char*>(spa_data0->data) + spa_data0->chunk->offset;

        auto& ring_buffer = *impl->ring_buffer;
        const int space_frames = static_cast<std::int32_t>(ring_buffer.free_count() / bytes_per_frame);
        if (frames > space_frames)
        {
            overflow = true;
            frames = space_frames;
        }
        if (frames > 0)
        {
            char* destination = ring_buffer.write_ptr();
            if (stride == bytes_per_frame)
            {
                std::memcpy(destination, source, static_cast<std::size_t>(frames) * bytes_per_frame);
            }
            else
            {
                for (int i = 0; i < frames; i += 1)
                {
                    std::memcpy(destination + static_cast<std::size_t>(i) * bytes_per_frame, source + static_cast<std::size_t>(i) * stride, bytes_per_frame);
                }
            }
            ring_buffer.advance_write_ptr(frames * bytes_per_frame);
        }
    }

    pw_stream_queue_buffer(impl->stream, buffer);

    if (overflow && is->overflow_callback != nullptr)
    {
        is->overflow_callback(is);
    }

    if (impl->ring_buffer != nullptr && is->read_callback != nullptr)
    {
        const int available = static_cast<std::int32_t>(impl->ring_buffer->fill_count() / bytes_per_frame);
        is->backend_data.pipe_wire.readable_frame_count = available;
        if (available > 0)
        {
            is->read_callback(is, 0, available);
        }
    }
}

static int instream_do_open(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is)
{
    auto& isw = is->backend_data.pipe_wire;
    const auto device = std::dynamic_pointer_cast<SoundIoDevicePrivate>(is->device);
    if (device == nullptr)
    {
        return SoundIoErrorInvalid;
    }

    if (is->software_latency <= 0.0)
    {
        is->software_latency = device->backend_data.pipe_wire != nullptr ? device->backend_data.pipe_wire->period_duration : 0.02;
    }
    if (is->software_latency <= 0.0)
    {
        is->software_latency = 0.02;
    }
    const double min_latency = std::max(device->software_latency_min, 0.001);
    const double max_latency = device->software_latency_max > min_latency ? device->software_latency_max : 2.0;
    is->software_latency = std::clamp(is->software_latency, min_latency, max_latency);

    auto* impl = new PipeWireStream();
    impl->is_output = false;
    impl->events.version = PW_VERSION_STREAM_EVENTS;
    impl->events.process = instream_process;
    isw.stream = impl;

    const std::uint32_t target_id = device->backend_data.pipe_wire != nullptr ? device->backend_data.pipe_wire->node_id : 0;
    const int err = StreamOpen(impl, PW_DIRECTION_INPUT, target_id, nullptr, is);
    if (err)
    {
        return err;
    }

    is->software_latency = static_cast<double>(impl->frames_per_period) / is->sample_rate;
    return SoundIoErrorNone;
}

static void instream_thread_run(const std::stop_token& token, const std::shared_ptr<void>& arg)
{
    const auto is = std::static_pointer_cast<SoundIoInStreamPrivate>(arg);
    auto& isw = is->backend_data.pipe_wire;
    const auto si = std::dynamic_pointer_cast<SoundIoPrivate>(is->device->soundio.lock());

    const int err = instream_do_open(si, is);

    std::unique_lock lock(isw.mutex->get());
    isw.open_complete = true;
    isw.open_err = err;
    isw.cond->signal(&lock);
    if (err)
    {
        lock.unlock();
        LOGI(L"pipewire: instream '%ls' open failed: %d", is->name.c_str(), err);
        return;
    }

    for (;;)
    {
        if (token.stop_requested())
        {
            lock.unlock();
            return;
        }
        if (isw.started)
        {
            break;
        }
        isw.start_cond->wait(&lock);
    }
    lock.unlock();

    StreamSetActive(isw.stream, true);

    std::unique_lock wait_lock(isw.mutex->get());
    while (!token.stop_requested())
    {
        isw.cond->wait(&wait_lock);
    }
    wait_lock.unlock();

    StreamSetActive(isw.stream, false);
}

static void instream_destroy_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is)
{
    LOGI(L"destroy pipewire instream: %ls", is->name.c_str());
    auto& isw = is->backend_data.pipe_wire;

    if (isw.thread)
    {
        isw.thread->request_stop();
        {
            std::unique_lock lock(isw.mutex->get());
            isw.cond->signal(&lock);
            isw.start_cond->signal(&lock);
        }
        isw.thread = nullptr;
    }

    StreamDestroy(isw.stream);

    isw.stream_name = std::wstring();
    isw.cond = nullptr;
    isw.start_cond = nullptr;
    isw.mutex = nullptr;
}

static int instream_open_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is)
{
    auto& isw = is->backend_data.pipe_wire;

    isw.stream_name = is->name;
    isw.cond = soundio_os_cond_create();
    if (isw.cond == nullptr)
    {
        instream_destroy_pipewire(si, is);
        return SoundIoErrorNoMem;
    }
    isw.start_cond = soundio_os_cond_create();
    if (isw.start_cond == nullptr)
    {
        instream_destroy_pipewire(si, is);
        return SoundIoErrorNoMem;
    }
    isw.mutex = soundio_os_mutex_create();
    if (isw.mutex == nullptr)
    {
        instream_destroy_pipewire(si, is);
        return SoundIoErrorNoMem;
    }

    isw.open_complete = false;
    isw.open_err = 0;
    isw.started = false;
    isw.is_paused = false;
    isw.thread = SoundIoOsThread::create(instream_thread_run, is);
    if (!isw.thread)
    {
        instream_destroy_pipewire(si, is);
        return SoundIoErrorNoMem;
    }

    std::unique_lock lock(isw.mutex->get());
    while (!isw.open_complete)
    {
        isw.cond->wait(&lock);
    }
    lock.unlock();

    if (isw.open_err)
    {
        instream_destroy_pipewire(si, is);
        return isw.open_err;
    }

    return SoundIoErrorNone;
}

static int instream_start_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is)
{
    auto& isw = is->backend_data.pipe_wire;
    std::unique_lock lock(isw.mutex->get());
    isw.started = true;
    isw.start_cond->signal(&lock);
    return SoundIoErrorNone;
}

static int instream_begin_read_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is, SoundIoChannelArea** out_areas, int* frame_count)
{
    auto& isw = is->backend_data.pipe_wire;
    auto* impl = isw.stream;
    if (impl == nullptr || impl->ring_buffer == nullptr)
    {
        return SoundIoErrorStreaming;
    }

    const int bytes_per_frame = impl->bytes_per_frame;
    const int available = static_cast<std::int32_t>(impl->ring_buffer->fill_count() / bytes_per_frame);
    const int frames = std::min(*frame_count, available);
    isw.read_frame_count = frames;
    *frame_count = frames;

    if (frames <= 0)
    {
        *out_areas = nullptr;
        return SoundIoErrorNone;
    }

    char* base = impl->ring_buffer->read_ptr();
    for (int ch = 0; ch < is->layout.channel_count; ch += 1)
    {
        isw.areas[ch].ptr = base + ch * is->bytes_per_sample;
        isw.areas[ch].step = bytes_per_frame;
    }
    *out_areas = isw.areas;
    return SoundIoErrorNone;
}

static int instream_end_read_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is)
{
    auto& isw = is->backend_data.pipe_wire;
    auto* impl = isw.stream;
    if (impl == nullptr || impl->ring_buffer == nullptr)
    {
        return SoundIoErrorStreaming;
    }
    impl->ring_buffer->advance_read_ptr(isw.read_frame_count * impl->bytes_per_frame);
    return SoundIoErrorNone;
}

static int instream_pause_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is, const bool pause)
{
    auto& isw = is->backend_data.pipe_wire;
    if (isw.is_paused == pause)
    {
        return SoundIoErrorNone;
    }
    isw.is_paused = pause;
    StreamSetActive(isw.stream, !pause);
    return SoundIoErrorNone;
}

static int instream_get_latency_pipewire(const std::shared_ptr<SoundIoPrivate>& si, const std::shared_ptr<SoundIoInStreamPrivate>& is, double* out_latency)
{
    const auto& isw = is->backend_data.pipe_wire;
    if (StreamQueryTime(isw.stream, nullptr, out_latency))
    {
        return SoundIoErrorNone;
    }
    *out_latency = is->software_latency;
    return SoundIoErrorNone;
}

// ============================================================================
// 7. Backend 初始化 / 销毁
// ============================================================================

static void destroy_pipewire(const std::shared_ptr<SoundIoPrivate>& si)
{
    LOGI(L"destroy pipewire");
    auto& siw = si->backend_data->pipe_wire;

    if (siw.thread)
    {
        // request_stop() 只置位，不会唤醒在 cond 上睡的线程，所以必须再 signal；
        // 顺序不能反：先置位再唤醒，否则线程醒来后检查到的还是「不该退出」。
        siw.thread->request_stop();
        {
            std::unique_lock lock(siw.scan_devices_mutex->get());
            siw.scan_devices_cond->signal(&lock);
        }
        siw.thread = nullptr; // 析构 → join，等设备线程把 pw loop 拆完
    }

    siw.cond = nullptr;
    siw.scan_devices_cond = nullptr;
    siw.scan_devices_mutex = nullptr;
    siw.mutex = nullptr;

    for (const auto& os: si->outstreams | std::ranges::views::values)
    {
        outstream_destroy_pipewire(si, std::dynamic_pointer_cast<SoundIoOutStreamPrivate>(os));
    }

    si->ClearOutStreams();
}

int soundio_pipewire_init(const std::shared_ptr<SoundIoPrivate>& si)
{
    SoundIoPipeWire& siw = si->backend_data->pipe_wire;

    static std::once_flag init_flag;
    std::call_once(init_flag, [] { pw_init(nullptr, nullptr); });

    siw.device_scan_queued.store(true, std::memory_order_release);
    siw.have_devices_flag = false;
    siw.shutdown_err = 0;
    siw.emitted_shutdown_cb = false;

    siw.mutex = soundio_os_mutex_create();
    if (!siw.mutex)
    {
        destroy_pipewire(si);
        return SoundIoErrorNoMem;
    }

    siw.scan_devices_mutex = soundio_os_mutex_create();
    if (!siw.scan_devices_mutex)
    {
        destroy_pipewire(si);
        return SoundIoErrorNoMem;
    }

    siw.cond = soundio_os_cond_create();
    if (!siw.cond)
    {
        destroy_pipewire(si);
        return SoundIoErrorNoMem;
    }

    siw.scan_devices_cond = soundio_os_cond_create();
    if (!siw.scan_devices_cond)
    {
        destroy_pipewire(si);
        return SoundIoErrorNoMem;
    }

    siw.thread = SoundIoOsThread::create(device_thread_run, si);
    if (!siw.thread)
    {
        destroy_pipewire(si);
        return SoundIoErrorNoMem;
    }

    si->destroy = destroy_pipewire;
    si->flush_events = flush_events_pipewire;
    si->wait_events = wait_events_pipewire;
    si->wakeup = wakeup_pipewire;
    si->force_device_scan = force_device_scan_pipewire;

    si->outstream_open = outstream_open_pipewire;
    si->outstream_destroy = outstream_destroy_pipewire;
    si->outstream_start = outstream_start_pipewire;
    si->outstream_begin_write = outstream_begin_write_pipewire;
    si->outstream_end_write = outstream_end_write_pipewire;
    si->outstream_clear_buffer = outstream_clear_buffer_pipewire;
    si->outstream_pause = outstream_pause_pipewire;
    si->outstream_get_latency = outstream_get_latency_pipewire;
    si->outstream_set_volume = outstream_set_volume_pipewire;
    si->outstream_get_time = outstream_get_time_pipewire;

    si->instream_open = instream_open_pipewire;
    si->instream_destroy = instream_destroy_pipewire;
    si->instream_start = instream_start_pipewire;
    si->instream_begin_read = instream_begin_read_pipewire;
    si->instream_end_read = instream_end_read_pipewire;
    si->instream_pause = instream_pause_pipewire;
    si->instream_get_latency = instream_get_latency_pipewire;

    // 等第一次枚举完成：拿不到就说明 daemon 不可用，让 soundio_connect 换后端
    {
        std::unique_lock lock(siw.mutex->get());
        const double deadline = soundio_os_get_time() + 5.0;
        while (!siw.have_devices_flag && !siw.shutdown_err)
        {
            if (soundio_os_get_time() >= deadline)
            {
                lock.unlock();
                LOGE(L"pipewire: timeout waiting for device list");
                destroy_pipewire(si);
                return SoundIoErrorInitAudioBackend;
            }
            siw.cond->timed_wait(&lock, 0.05);
        }
        if (siw.shutdown_err)
        {
            lock.unlock();
            destroy_pipewire(si);
            return SoundIoErrorInitAudioBackend;
        }
    }

    return SoundIoErrorNone;
}
