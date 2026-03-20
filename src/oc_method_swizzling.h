/*
 * av_audio_session_guard.h
 *
 * iOS-only helper: prevents Unity (or any third-party code) from overriding
 * the AVAudioSession category while WaveAudio streams are active.
 *
 * Usage (Objective-C / C++ / ObjC++):
 *   wav_audio_session_guard_install();    // 可选：提前安装，lock() 内部也会自动安装
 *   wav_audio_session_guard_lock();       // 在 WaveAudio outstream/instream open 后调用
 *   wav_audio_session_guard_unlock();     // 在所有 WaveAudio stream destroy 后调用
 *   wav_audio_session_guard_uninstall();  // 可选：应用退出时清理
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 安装 AVAudioSession setCategory swizzle 拦截器（幂等，多次调用安全）。
 */
void wav_audio_session_guard_install(void);

/**
 * @brief 激活保护锁（引用计数式，支持嵌套）。
 *
 * 激活后，外部对以下方法的调用会被拦截并自动还原为锁定时的 session 配置：
 *   -[AVAudioSession setCategory:error:]
 *   -[AVAudioSession setCategory:withOptions:error:]
 *   -[AVAudioSession setCategory:mode:options:error:]
 */
void wav_audio_session_guard_lock(void);

/**
 * @brief 释放一次保护锁引用。计数降为 0 时保护失效。
 */
void wav_audio_session_guard_unlock(void);

/**
 * @brief 卸载 swizzle，恢复原始实现。
 */
void wav_audio_session_guard_uninstall(void);

/**
 * @brief 查询当前是否处于保护状态。
 * @return 1 = 保护中，0 = 未保护
 */
int wav_audio_session_guard_is_locked(void);

#ifdef __cplusplus
}
#endif
