#ifndef RONIN_JNI_UTILS_H_
#define RONIN_JNI_UTILS_H_

#include <jni.h>
#include <string>
#include <vector>
#include "ronin_log.h"

namespace tflite {
namespace jni {

/**
 * LiteRT-LM (Production) JNI Utilities.
 * Replicated from official LiteRT/TFLite sources.
 */

// Retrieve the JNIEnv for the current thread
JNIEnv* GetJNIEnv(JavaVM* vm);

// Convert jstring to std::string
std::string ConvertJStringToString(JNIEnv* env, jstring jstr);

// Convert std::string to jstring
jstring ConvertStringToJString(JNIEnv* env, const std::string& str);

// Throw a Java RuntimeException
void ThrowException(JNIEnv* env, const char* message);

/**
 * ScopedJniEnv: RAII wrapper to ensure Attach/Detach pairing.
 */
class ScopedJniEnv {
public:
    explicit ScopedJniEnv(JavaVM* vm);
    ~ScopedJniEnv();

    JNIEnv* operator->() const { return env_; }
    JNIEnv* get() const { return env_; }
    bool isValid() const { return env_ != nullptr; }

private:
    JavaVM* vm_;
    JNIEnv* env_;
    bool attached_;
};

/**
 * GlobalRef: RAII wrapper for global references.
 */
class GlobalRef {
public:
    GlobalRef(JNIEnv* env, jobject obj);
    ~GlobalRef();
    
    GlobalRef(const GlobalRef&) = delete;
    GlobalRef& operator=(const GlobalRef&) = delete;
    GlobalRef(GlobalRef&& other) noexcept;
    GlobalRef& operator=(GlobalRef&& other) noexcept;

    jobject get() const { return obj_; }
    operator jobject() const { return obj_; }

private:
    JNIEnv* env_ = nullptr;
    jobject obj_ = nullptr;
};

} // namespace jni
} // namespace tflite

// Compatibility alias for Ronin
namespace ronin::jni = tflite::jni;

#endif // RONIN_JNI_UTILS_H_
