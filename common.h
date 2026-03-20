//
// Created by Administrator on 2026/3/5.
//

#ifndef AUDIORENDERER_COMMON_H
#define AUDIORENDERER_COMMON_H

#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif

#if defined(AUDIO_RENDERER_STATIC)
#  define AR_API
#elif defined(_WIN32)
#  define AR_API EXTERN_C __declspec(dllexport)
#else
#  define AR_API EXTERN_C __attribute__((visibility("default")))
#endif


#if defined(AUDIO_RENDERER_STATIC)
#  define EXPORT_API
#elif defined(_WIN32)
#  define EXPORT_API __declspec(dllexport)
#else
#  define EXPORT_API __attribute__((visibility("default")))
#endif


#if __ANDROID__ || __APPLE__
#define NO_ATOMIC_SHARED
#endif


#endif //AUDIORENDERER_COMMON_H
