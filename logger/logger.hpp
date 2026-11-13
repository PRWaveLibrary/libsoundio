//
// Created by Administrator on 2026/3/17.
//

#ifndef WAVEAUDIO_LOG_H
#define WAVEAUDIO_LOG_H

#include <string>
#include <string_view>
#include <cstdio>
#include <cstdarg>

// 相对于本文件的路径：logger 在 libsoundio 下，而 string_util 在仓库的 src/common 下
#include "../../src/common/string_util.h"

#define LOG_TAG        L"WaveAudio"

// 宽字符版 __FILE__（MSVC 自带 __FILEW__，GCC/Clang 用拼接得到）
#ifdef __FILEW__
#define WA_FILE_W __FILEW__
#else
#define WA_WIDEN_IMPL(x) L##x
#define WA_WIDEN(x) WA_WIDEN_IMPL(x)
#define WA_FILE_W WA_WIDEN(__FILE__)
#endif

typedef void (*unity_log_ptr)(int, const char16_t*);

inline unity_log_ptr g_unity_log = nullptr;

enum LogLevel
{
    Debug = 0,
    Info,
    Warn,
    Error
};

static inline void LogUnity(LogLevel level, const std::wstring& s)
{
    if (g_unity_log == nullptr)
    {
        return;
    }
    g_unity_log(level, Util::String::to_u16string(s).c_str());
}

// =======================================================
// 底层格式化实现核心
// =======================================================

inline const wchar_t* GetLogLevelString(LogLevel level)
{
    switch (level)
    {
        case Debug:
            return L"DEBUG";
        case Info:
            return L"INFO";
        case Warn:
            return L"WARN";
        case Error:
            return L"ERROR";
        default:
            return L"UNKNOWN";
    }
}

// 接受已格式化好的消息字符串，拼接完整日志行
inline std::wstring FormatLog(LogLevel level, const wchar_t* file, int line, const std::wstring& msg)
{
    // 获取文件名 (使用 std::string_view 零拷贝，仅在拼接时转宽)
    std::wstring_view filePath(file);
    auto lastSlash = filePath.find_last_of(L"/\\");
    std::wstring_view fileName = (lastSlash == std::string_view::npos) ? filePath : filePath.substr(lastSlash + 1);

    // 拼接最终的控制台输出格式
    std::wstring out;
    out.reserve(fileName.size() + msg.size() + 32);
    out += L"[";
    out += LOG_TAG;
    out += L"][";
    out += GetLogLevelString(level);
    out += L"][";
    out += fileName;
    out += L':';
    out += std::to_wstring(line);
    out += L"] ";
    out += msg;
    out += L'\n';
    return out;
}

// 用 vswprintf 格式化可变参数（printf 风格，宽字符）
inline std::wstring FormatArgsV(const wchar_t* fmt, va_list args)
{
    // vswprintf 无法像 vsnprintf 那样先探测长度，只能逐步扩容重试
    std::wstring result(256, L'\0');
    for (;;)
    {
        va_list args_copy;
        va_copy(args_copy, args);
        const int len = std::vswprintf(result.data(), result.size(), fmt, args_copy);
        va_end(args_copy);

        if (len >= 0 && static_cast<size_t>(len) < result.size())
        {
            result.resize(static_cast<size_t>(len));
            return result;
        }
        if (result.size() >= 64 * 1024)
        {
            return {}; // 格式化失败或超长，放弃
        }
        // 缓冲不足（MSVC / glibc 都返回 -1），扩容后重试
        result.resize(result.size() * 4);
    }
}

inline std::wstring FormatArgs(const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::wstring result = FormatArgsV(fmt, args);
    va_end(args);
    return result;
}


#ifdef _WIN32
#include <windows.h>
#include <cstdio>

inline void PlatformLogImpl(LogLevel level, const wchar_t* file, int line, const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::wstring msg = FormatArgsV(fmt, args);
    va_end(args);

    auto s = FormatLog(level, file, line, msg);

    if (IsDebuggerPresent())
    {
        // 先把之前缓冲的窄字符输出刷出去，保证日志顺序
        std::fflush(stdout);

        // 直接输出 UTF-16 到控制台，绕过代码页转换（否则中文会变成 '?'）
        HANDLE h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (h_stdout != nullptr && h_stdout != INVALID_HANDLE_VALUE && GetConsoleMode(h_stdout, &mode))
        {
            DWORD written = 0;
            WriteConsoleW(h_stdout, s.c_str(), static_cast<DWORD>(s.size()), &written, nullptr);
        }
        else
        {
            // stdout 被重定向到文件/管道时，退化为 UTF-8 字节输出
            const auto utf8 = Util::String::to_utf8(s);
            std::fwrite(utf8.data(), 1, utf8.size(), stdout);
        }
        std::fflush(stdout);
    }
    else
    {
        OutputDebugStringW(s.c_str());
    }
    LogUnity(level, s);
}

#elif defined(__ANDROID__)
#include <android/log.h>
#include <cstdio>
#include <cstring>

inline void PlatformLogImpl(LogLevel level, const wchar_t* file, int line, const wchar_t* fmt, ...)
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
    std::wstring msg = FormatArgsV(fmt, args);
    va_end(args);

    auto s = FormatLog(level, file, line, msg);
    const auto utf8 = Util::String::to_utf8(s);
    __android_log_print(priority, LOG_TAG, "%s", utf8.c_str());
    LogUnity(level, s);
}

#elif __MACH__
#include <cstdio>

inline void PlatformLogImpl(LogLevel level, const wchar_t* file, int line, const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::wstring msg = FormatArgsV(fmt, args);
    va_end(args);

    auto s = FormatLog(level, file, line, msg);
    const auto utf8 = Util::String::to_utf8(s);
    FILE* stream = level >= Error ? stderr : stdout;
    std::fputs(utf8.c_str(), stream);
    std::fflush(stream);
    LogUnity(level, s);
}
#endif

// =======================================================
// 对外暴露的业务宏
// =======================================================
// 格式串为宽字符：L"xxx %d"；窄字符串参数用 %hs，宽字符串参数用 %ls
// ##__VA_ARGS__ 为 GCC/Clang 扩展，用于消除无参时的尾随逗号
#define LOGD(fmt, ...) PlatformLogImpl(LogLevel::Debug, WA_FILE_W, __LINE__, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) PlatformLogImpl(LogLevel::Info,  WA_FILE_W, __LINE__, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) PlatformLogImpl(LogLevel::Warn,  WA_FILE_W, __LINE__, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) PlatformLogImpl(LogLevel::Error, WA_FILE_W, __LINE__, fmt, ##__VA_ARGS__)

static void set_unity_log(unity_log_ptr ptr)
{
    g_unity_log = ptr;
}

#endif //WAVEAUDIO_LOG_H
