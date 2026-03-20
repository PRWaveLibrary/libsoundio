/*
 * oc_method_swizzling.mm
 * libsoundio/src/
 *
 * Prevents Unity (Microphone.Start / UnityGetAudioSessionCategory) from
 * clobbering our AVAudioSession configuration via Method Swizzling.
 *
 * Strategy
 * --------
 * 1. On wav_audio_session_guard_install() we use the ObjC runtime to replace
 *    the IMP of three setCategory… selectors with our own interceptors.
 *    The original IMP pointers are saved so we can call through to Apple's
 *    implementation when the guard is NOT active.
 *
 * 2. When the guard lock-count > 0 any incoming setCategory call is silently
 *    dropped. After our interceptor returns we call restore_locked_session()
 *    which calls back to the *original* IMP to reinstate the category/options/
 *    mode that were active at the time lock() was first called.
 *
 * 3. The swizzle installation is idempotent (dispatch_once) and the
 *    lock/unlock counter is atomic, making it safe to call from any thread.
 *
 * Lock / unlock pairing expected by coreaudio_ios.mm
 * ---------------------------------------------------
 *   outstream_open_ca  (success) → wav_audio_session_guard_lock()
 *   outstream_destroy_ca         → wav_audio_session_guard_unlock()
 *
 *   instream_start_ca  (success) → wav_audio_session_guard_lock()
 *   instream_destroy_ca          → wav_audio_session_guard_unlock()
 *
 * The count-based approach means it is safe to have both an output and an
 * input stream open simultaneously – the session stays locked until both
 * streams have been destroyed.
 */

#import <AVFoundation/AVFoundation.h>
#import <objc/runtime.h>
#import <atomic>

#include "oc_method_swizzling.h"
#include "logger/logger.hpp"

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

/// 引用计数（支持嵌套 lock）
inline static std::atomic<int>  s_lock_count = 0;
/// swizzle 是否已安装
inline static std::atomic<bool> s_installed  = false;

/// lock() 时快照的 session 配置
inline static AVAudioSessionCategory        s_locked_category = nil;
inline static AVAudioSessionCategoryOptions s_locked_options  = 0;
inline static AVAudioSessionMode            s_locked_mode     = nil;

NSString * const WaveAudioErrorDomain = @"com.prwave.waveaudio.error";

// ---------------------------------------------------------------------------
// 原始 IMP 的函数指针别名（方便 cast）
// ---------------------------------------------------------------------------
typedef BOOL (*SetCategoryErrorIMP)(id, SEL, AVAudioSessionCategory, NSError**);
typedef BOOL (*SetCategoryOptionsErrorIMP)(id, SEL, AVAudioSessionCategory, AVAudioSessionCategoryOptions, NSError**);
typedef BOOL (*SetCategoryModeOptionsErrorIMP)(id, SEL, AVAudioSessionCategory, AVAudioSessionMode, AVAudioSessionCategoryOptions, NSError**);

static SetCategoryErrorIMP            s_orig_setCategory_error                 = NULL;
static SetCategoryOptionsErrorIMP     s_orig_setCategory_options_error          = NULL;
static SetCategoryModeOptionsErrorIMP s_orig_setCategory_mode_options_error     = NULL;

// ---------------------------------------------------------------------------
// 还原快照 — 直接调用原始 IMP，绕开我们自己的拦截
// ---------------------------------------------------------------------------
static void restore_locked_session(void)
{
    if (!s_locked_category) {
        return;
    }

    AVAudioSession* session = AVAudioSession.sharedInstance;
    NSError* err = nil;

    if (s_orig_setCategory_mode_options_error) {
        s_orig_setCategory_mode_options_error(
            session,
            @selector(setCategory:mode:options:error:),
            s_locked_category,
            s_locked_mode ?: AVAudioSessionModeDefault,
            s_locked_options,
            &err
        );
    } else if (s_orig_setCategory_options_error) {
        s_orig_setCategory_options_error(
            session,
            @selector(setCategory:withOptions:error:),
            s_locked_category,
            s_locked_options,
            &err
        );
    } else if (s_orig_setCategory_error) {
        s_orig_setCategory_error(
            session,
            @selector(setCategory:error:),
            s_locked_category,
            &err
        );
    }

    if (err) {
        LOGE("AVAudioSessionGuard restore failed: {}", err.localizedDescription.UTF8String);
    }
}

// ---------------------------------------------------------------------------
// Swizzled implementations
// ---------------------------------------------------------------------------

/// -[AVAudioSession setCategory:error:]
static BOOL swizzled_setCategory_error(id self, SEL _cmd, AVAudioSessionCategory category, NSError** error)
{
    if (atomic_load(&s_lock_count) > 0) {
        LOGI("AVAudioSessionGuard: intercepted setCategory:{} — restoring locked config", category.UTF8String);
        if (error){
            NSDictionary *userInfo = @{NSLocalizedDescriptionKey: @"category locked."};
            *error = [NSError errorWithDomain:WaveAudioErrorDomain code:1000 userInfo:userInfo];
        }
        return NO;
    }
    return s_orig_setCategory_error(self, _cmd, category, error);
}

/// -[AVAudioSession setCategory:withOptions:error:]
static BOOL swizzled_setCategory_options_error(id self, SEL _cmd,
                                                    AVAudioSessionCategory category,
                                                    AVAudioSessionCategoryOptions options,
                                                    NSError** error)
{
    if (atomic_load(&s_lock_count) > 0) {
        LOGI("AVAudioSessionGuard: intercepted setCategory:%@ options:%lu — restoring locked config", category.UTF8String, (unsigned long)options);
        if (error){
            NSDictionary *userInfo = @{NSLocalizedDescriptionKey: @"category locked."};
            *error = [NSError errorWithDomain:WaveAudioErrorDomain code:1000 userInfo:userInfo];
        }
    }
    return s_orig_setCategory_options_error(self, _cmd, category, options, error);
}

/// -[AVAudioSession setCategory:mode:options:error:]
static BOOL swizzled_setCategory_mode_options_error(id self, SEL _cmd, AVAudioSessionCategory category, AVAudioSessionMode mode, AVAudioSessionCategoryOptions options, NSError** error)
{
    if (atomic_load(&s_lock_count) > 0) {
        LOGI("AVAudioSessionGuard: intercepted setCategory:{} mode:{} options:{} — restoring locked config", category.UTF8String, mode.UTF8String, (unsigned long)options);
        if (error){
            NSDictionary *userInfo = @{NSLocalizedDescriptionKey: @"category locked."};
            *error = [NSError errorWithDomain:WaveAudioErrorDomain code:1000 userInfo:userInfo];
        }
    }
    return s_orig_setCategory_mode_options_error(self, _cmd, category, mode, options, error);
}

// ---------------------------------------------------------------------------
// 安装（dispatch_once 保证单次执行）
// ---------------------------------------------------------------------------
static dispatch_once_t s_install_once = 0;

static void do_install(void)
{
    dispatch_once(&s_install_once, ^{
        Class cls = [AVAudioSession class];

        // ---- setCategory:error: ----
        {
            SEL sel = @selector(setCategory:error:);
            Method m = class_getInstanceMethod(cls, sel);
            if (m) {
                IMP orig = method_setImplementation(m, (IMP)swizzled_setCategory_error);
                s_orig_setCategory_error = (SetCategoryErrorIMP)orig;
            }
        }

        // ---- setCategory:withOptions:error: ----
        {
            SEL sel = @selector(setCategory:withOptions:error:);
            Method m = class_getInstanceMethod(cls, sel);
            if (m) {
                IMP orig = method_setImplementation(m, (IMP)swizzled_setCategory_options_error);
                s_orig_setCategory_options_error = (SetCategoryOptionsErrorIMP)orig;
            }
        }

        // ---- setCategory:mode:options:error: ----
        {
            SEL sel = @selector(setCategory:mode:options:error:);
            Method m = class_getInstanceMethod(cls, sel);
            if (m) {
                IMP orig = method_setImplementation(m, (IMP)swizzled_setCategory_mode_options_error);
                s_orig_setCategory_mode_options_error = (SetCategoryModeOptionsErrorIMP)orig;
            }
        }

        atomic_store(&s_installed, true);
        LOGI("AVAudioSessionGuard: swizzle installed.");
    });
}

void audio_session_guard_install(void)
{
    do_install();
}

void audio_session_guard_lock(void)
{
    do_install();  // 保证 swizzle 已安装

    int prev = atomic_fetch_add(&s_lock_count, 1);
    if (prev == 0) {
        // 首次 lock：快照当前 AVAudioSession 配置
        AVAudioSession* session = AVAudioSession.sharedInstance;
        s_locked_category = session.category;
        s_locked_options  = session.categoryOptions;
        s_locked_mode     = session.mode;
        LOGI("AVAudioSessionGuard: LOCKED. snapshot: category={} mode={} options={}", s_locked_category.UTF8String, s_locked_mode.UTF8String, (unsigned long)s_locked_options);
    } else {
        LOGI("AVAudioSessionGuard: lock refcount -> {}", prev + 1);
    }
}

void audio_session_guard_unlock(void)
{
    int prev = atomic_fetch_sub(&s_lock_count, 1);
    if (prev <= 0) {
        atomic_store(&s_lock_count, 0);
        LOGI("AVAudioSessionGuard: unlock underflow ignored.");
        return;
    }
    if (prev == 1) {
        // 引用归零，清空快照
        s_locked_category = nil;
        s_locked_mode     = nil;
        s_locked_options  = 0;
        LOGI("AVAudioSessionGuard: UNLOCKED. External category changes now allowed.");
    } else {
        LOGI("AVAudioSessionGuard: lock refcount -> {}", prev - 1);
    }
}

void audio_session_guard_uninstall(void)
{
    if (!atomic_load(&s_installed)) return;

    Class cls = [AVAudioSession class];

    if (s_orig_setCategory_error) {
        Method m = class_getInstanceMethod(cls, @selector(setCategory:error:));
        if (m) method_setImplementation(m, (IMP)s_orig_setCategory_error);
        s_orig_setCategory_error = NULL;
    }
    if (s_orig_setCategory_options_error) {
        Method m = class_getInstanceMethod(cls, @selector(setCategory:withOptions:error:));
        if (m) method_setImplementation(m, (IMP)s_orig_setCategory_options_error);
        s_orig_setCategory_options_error = NULL;
    }
    if (s_orig_setCategory_mode_options_error) {
        Method m = class_getInstanceMethod(cls, @selector(setCategory:mode:options:error:));
        if (m) method_setImplementation(m, (IMP)s_orig_setCategory_mode_options_error);
        s_orig_setCategory_mode_options_error = NULL;
    }

    atomic_store(&s_installed, false);
    s_install_once = 0;  // 仅测试场景下允许重新安装

    LOGI("AVAudioSessionGuard: swizzle uninstalled.");
}

BOOL audio_session_guard_is_locked(void)
{
    return atomic_load(&s_lock_count) > 0 ? YES : NO;
}
