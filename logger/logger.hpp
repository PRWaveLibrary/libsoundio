//
// Created by Administrator on 2026/3/17.
//

#ifndef WAVEAUDIO_LOG_H
#define WAVEAUDIO_LOG_H

#include <string>
#include <string_view>
#include <format>
#include "../libsoundio/common.h"

#define LOG_TAG "WaveAudio"

typedef void (*unity_log_ptr)(int, const char*);

inline unity_log_ptr g_unity_log = nullptr;

enum LogLevel
{
    Debug = 0,
    Info,
    Warn,
    Error
};

static inline void LogUnity(LogLevel level, std::string& s)
{
    if (g_unity_log == nullptr)
    {
        return;
    }
    g_unity_log(level, s.c_str());
}

// =======================================================
// 底层格式化实现核心
// =======================================================

inline const char* GetLogLevelString(LogLevel level)
{
    switch (level)
    {
        case Debug:
            return "DEBUG";
        case Info:
            return "INFO";
        case Warn:
            return "WARN";
        case Error:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

// 🌟 修复 2：将 const char* fmt 替换为 std::format_string<Args...>
inline std::string FormatLog(LogLevel level, const char* file, int line, std::string_view fmt, std::format_args args)
{
    // 1. 获取文件名 (使用 std::string_view 零拷贝，更高效)
    std::string_view filePath(file);
    auto lastSlash = filePath.find_last_of("/\\");
    std::string_view fileName = (lastSlash == std::string_view::npos) ? filePath : filePath.substr(lastSlash + 1);

    // 3. 格式化真实的业务数据 (使用 std::forward 完美转发参数，性能更好)
    std::string msgBuffer = std::vformat(fmt, args);

    // 4. 拼接最终的控制台输出格式
    return std::format("[{}][{}][{}:{}] {}\n", LOG_TAG, GetLogLevelString(level), fileName, line, msgBuffer);
}


#ifdef _WIN32
#include <windows.h>
#include <cstdio>

template<typename... Args>
inline void PlatformLog(LogLevel level, const char* file, int line, std::format_string<Args...> fmt, Args&&... args)
{
    auto s = FormatLog(level, file, line, fmt.get(), std::make_format_args(args...));

    if (IsDebuggerPresent())
    {
        printf("%s", s.c_str());
        fflush(stdout);
    }
    else
    {
        OutputDebugStringA(s.c_str());
    }
    LogUnity(level, s);
}

#elif defined(__ANDROID__)
#include <android/log.h>
#include <cstdio>
#include <cstring>

template<typename... Args>
inline void PlatformLog(LogLevel level, const char* file, int line, std::format_string<Args...> fmt, Args&&... args)
{
    // 映射 Android 底层优先级
    android_LogPriority priority = ANDROID_LOG_DEFAULT;
    switch (level)
    {
        case Debug:
            priority = ANDROID_LOG_DEBUG;
            break;
        case Info:
            priority = ANDROID_LOG_INFO;
            break;
        case Warn:
            priority = ANDROID_LOG_WARN;
            break;
        case Error:
            priority = ANDROID_LOG_ERROR;
            break;
    }

    auto s = FormatLog(level, file, line, fmt.get(), std::make_format_args(args...));
    __android_log_print(priority, LOG_TAG, "%s", s.c_str());
    LogUnity(level, s);
}

#elif __MACH__
#include <cstdio>

template<typename... Args>
inline void PlatformLog(LogLevel level, const char* file, int line, std::format_string<Args...> fmt, Args&&... args)
{
    auto s = FormatLog(level, file, line, fmt.get(), std::make_format_args(args...));
    FILE* stream = level >= Error ? stderr : stdout;
    std::fputs(s.c_str(), stream);
    std::fflush(stream);
    LogUnity(level, s);
}
#endif

// =======================================================
// 对外暴露的业务宏
// =======================================================
// 🌟 修复 4：使用 C++20 标准的 __VA_OPT__(,) 替代非标准的 ##__VA_ARGS__
// 这样当只有 fmt 没有 args 时，逗号会被完美消除，跨平台兼容性 100%
#define LOGD(fmt, ...) PlatformLog(LogLevel::Debug, __FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOGI(fmt, ...) PlatformLog(LogLevel::Info,  __FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOGW(fmt, ...) PlatformLog(LogLevel::Warn,  __FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)
#define LOGE(fmt, ...) PlatformLog(LogLevel::Error, __FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)

static void set_unity_log(unity_log_ptr ptr)
{
    g_unity_log = ptr;
}

#endif //WAVEAUDIO_LOG_H
