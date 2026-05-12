//
// Created by Administrator on 2026/3/17.
//

#ifndef WAVEAUDIO_LOG_H
#define WAVEAUDIO_LOG_H

#include <string>
#include <string_view>
#include <cstdio>
#include <cstdarg>
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

// C++17 实现：接受已格式化好的消息字符串，拼接完整日志行
inline std::string FormatLog(LogLevel level, const char* file, int line, const std::string& msg)
{
    // 获取文件名 (使用 std::string_view 零拷贝，更高效)
    std::string_view filePath(file);
    auto lastSlash = filePath.find_last_of("/\\");
    std::string_view fileName = (lastSlash == std::string_view::npos) ? filePath : filePath.substr(lastSlash + 1);

    // 拼接最终的控制台输出格式
    char header[256];
    std::snprintf(header, sizeof(header), "[%s][%s][%.*s:%d] ",
                  LOG_TAG,
                  GetLogLevelString(level),
                  static_cast<int>(fileName.size()), fileName.data(),
                  line);

    return std::string(header) + msg + "\n";
}

// C++17 实现：用 vsnprintf 格式化可变参数
inline std::string FormatArgs(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    // 先计算所需长度
    va_list args_copy;
    va_copy(args_copy, args);
    int len = std::vsnprintf(nullptr, 0, fmt, args_copy);
    va_end(args_copy);

    std::string result;
    if (len > 0)
    {
        result.resize(static_cast<size_t>(len));
        std::vsnprintf(result.data(), static_cast<size_t>(len) + 1, fmt, args);
    }

    va_end(args);
    return result;
}


#ifdef _WIN32
#include <windows.h>
#include <cstdio>

inline void PlatformLogImpl(LogLevel level, const char* file, int line, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    va_list args_copy;
    va_copy(args_copy, args);
    int len = std::vsnprintf(nullptr, 0, fmt, args_copy);
    va_end(args_copy);

    std::string msg;
    if (len > 0)
    {
        msg.resize(static_cast<size_t>(len));
        std::vsnprintf(msg.data(), static_cast<size_t>(len) + 1, fmt, args);
    }
    va_end(args);

    auto s = FormatLog(level, file, line, msg);

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

inline void PlatformLogImpl(LogLevel level, const char* file, int line, const char* fmt, ...)
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

    va_list args;
    va_start(args, fmt);

    va_list args_copy;
    va_copy(args_copy, args);
    int len = std::vsnprintf(nullptr, 0, fmt, args_copy);
    va_end(args_copy);

    std::string msg;
    if (len > 0)
    {
        msg.resize(static_cast<size_t>(len));
        std::vsnprintf(msg.data(), static_cast<size_t>(len) + 1, fmt, args);
    }
    va_end(args);

    auto s = FormatLog(level, file, line, msg);
    __android_log_print(priority, LOG_TAG, "%s", s.c_str());
    LogUnity(level, s);
}

#elif __MACH__
#include <cstdio>

inline void PlatformLogImpl(LogLevel level, const char* file, int line, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    va_list args_copy;
    va_copy(args_copy, args);
    int len = std::vsnprintf(nullptr, 0, fmt, args_copy);
    va_end(args_copy);

    std::string msg;
    if (len > 0)
    {
        msg.resize(static_cast<size_t>(len));
        std::vsnprintf(msg.data(), static_cast<size_t>(len) + 1, fmt, args);
    }
    va_end(args);

    auto s = FormatLog(level, file, line, msg);
    FILE* stream = level >= Error ? stderr : stdout;
    std::fputs(s.c_str(), stream);
    std::fflush(stream);
    LogUnity(level, s);
}
#endif

// =======================================================
// 对外暴露的业务宏
// =======================================================
// C++17：使用 ##__VA_ARGS__ GCC/Clang 扩展消除尾随逗号（主流编译器均支持）
#define LOGD(fmt, ...) PlatformLogImpl(LogLevel::Debug, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) PlatformLogImpl(LogLevel::Info,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) PlatformLogImpl(LogLevel::Warn,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) PlatformLogImpl(LogLevel::Error, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

static void set_unity_log(unity_log_ptr ptr)
{
    g_unity_log = ptr;
}

#endif //WAVEAUDIO_LOG_H
