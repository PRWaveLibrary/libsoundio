//
// Created by Administrator on 2026/3/17.
//

#ifndef WAVEAUDIO_LOG_H
#define WAVEAUDIO_LOG_H

#define LOG_TAG "WaveAudio"

#if defined(__GNUC__) || defined(__clang__)
// 告诉编译器：这和 printf 是一样的，请检查对应位置的参数！
#define WAVE_PRINTF_CHECK(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define WAVE_PRINTF_CHECK(fmt_idx, arg_idx)
#endif

enum LogLevel
{
    Debug = 0,
    Info,
    Warn,
    Error
};

// =======================================================
// 底层格式化实现核心
// =======================================================
#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <cstring>

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

template<typename... Args>
WAVE_PRINTF_CHECK (4, 5)
inline void PlatformLog(LogLevel level, const char* file, int line, const char* format, Args... args)
{
    // 截取简短文件名 (去掉 C:\xxx\src 这种冗长的绝对路径)
    const char* fileName = strrchr(file, '/');
    if (!fileName)
        fileName = strrchr(file, '\\');
    fileName = fileName ? fileName + 1 : file;

    // 格式化真实的业务数据
    char msgBuffer[2048];
    snprintf(msgBuffer, sizeof(msgBuffer), format, args...);

    // 终极拼接：[LOG_TAG][LEVEL][文件名:行号] 业务消息
    char finalBuffer[2560];
    snprintf(finalBuffer, sizeof(finalBuffer), "[%s][%s][%s:%d] %s\n", LOG_TAG, GetLogLevelString(level), fileName, line, msgBuffer);

    if (IsDebuggerPresent())
    {
        printf("%s", finalBuffer);
        fflush(stdout); // 修复 2：防止 GDB 缓存导致日志不立即显示
    }
    else
    {
        OutputDebugStringA(finalBuffer);
    }
}

#elif defined(__ANDROID__) // 修复 3：Android 平台标准的编译器宏
#include <android/log.h>
#include <cstdio>
#include <cstring>

template<typename... Args>
WAVE_PRINTF_CHECK(4, 5)
inline void PlatformLog(LogLevel level, const char* file, int line, const char* format, Args... args)
{
    const char* fileName = strrchr(file, '/');
    if (!fileName)
        fileName = strrchr(file, '\\');
    fileName = fileName ? fileName + 1 : file;

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

    // 先格式化业务数据
    char msgBuffer[2048];
    snprintf(msgBuffer, sizeof(msgBuffer), format, args...);

    // 将文件名和行号拼接到格式化好的消息前面
    char finalBuffer[2560];
    snprintf(finalBuffer, sizeof(finalBuffer), "[%s:%d] %s", fileName, line, msgBuffer);

    // 修复 4：极其安全的最终输出，绝对不崩溃
    __android_log_print(priority, LOG_TAG, "%s", finalBuffer);
}

#endif

// =======================================================
// 对外暴露的业务宏 (完美劫持文件名和行号，并修复悬空逗号)
// =======================================================
// 注意前面的 ##：当你不传参数只传 format 时，它会自动吞掉多余的逗号
#define LOGD(format, ...) PlatformLog(LogLevel::Debug, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOGI(format, ...) PlatformLog(LogLevel::Info,  __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOGW(format, ...) PlatformLog(LogLevel::Warn,  __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOGE(format, ...) PlatformLog(LogLevel::Error, __FILE__, __LINE__, format, ##__VA_ARGS__)

#endif //WAVEAUDIO_LOG_H
