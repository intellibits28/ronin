#pragma once

#ifdef ANDROID
#include <android/log.h>
#define LOGD(tag, ...) __android_log_print(ANDROID_LOG_DEBUG, tag, __VA_ARGS__)
#define LOGI(tag, ...) __android_log_print(ANDROID_LOG_INFO, tag, __VA_ARGS__)
#define LOGW(tag, ...) __android_log_print(ANDROID_LOG_WARN, tag, __VA_ARGS__)
#define LOGE(tag, ...) __android_log_print(ANDROID_LOG_ERROR, tag, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(tag, ...) printf("[%s] INFO: ", tag); printf(__VA_ARGS__); printf("\n")
#define LOGW(tag, ...) printf("[%s] WARN: ", tag); printf(__VA_ARGS__); printf("\n")
#define LOGE(tag, ...) fprintf(stderr, "[%s] ERROR: ", tag); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n")
#endif
