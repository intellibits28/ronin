#ifndef RONIN_JNI_UTILS_H_
#define RONIN_JNI_UTILS_H_

#include <jni.h>
#include <string>
#include <vector>
#include "ronin_log.h"

namespace ronin {
namespace jni {

/**
 * LiteRT-LM Compatible JNI Utilities.
 * Implements thread-safe JNI environment management and string conversion.
 */

// Retrieve the JNIEnv for the current thread
JNIEnv* GetJNIEnv(JavaVM* vm);

// Convert jstring to std::string
std::string JStringToStdString(JNIEnv* env, jstring jstr);

// Convert std::string to jstring
jstring StdStringToJString(JNIEnv* env, const std::string& str);

// Throw a Java RuntimeException
void ThrowJavaException(JNIEnv* env, const char* message);

/**
 * ScopedJniEnv: RAII wrapper to ensure Attach/Detach pairing.
 * Mandatory for C++ threads calling back into Kotlin (Rule 2).
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
} // namespace ronin

#endif // RONIN_JNI_UTILS_H_
