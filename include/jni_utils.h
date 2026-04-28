#ifndef RONIN_JNI_UTILS_H_
#define RONIN_JNI_UTILS_H_

#include <jni.h>
#include <string>
#include <vector>
#include "ronin_log.h"

namespace ronin {
namespace jni {

// Retrieve the JNIEnv for the current thread
JNIEnv* GetJNIEnv(JavaVM* vm);

// Convert jstring to std::string
std::string JStringToStdString(JNIEnv* env, jstring jstr);

// Convert std::string to jstring
jstring StdStringToJString(JNIEnv* env, const std::string& str);

// Throw a Java RuntimeException
void ThrowJavaException(JNIEnv* env, const char* message);

// RAII wrapper for global references
class GlobalRef {
public:
    GlobalRef(JNIEnv* env, jobject obj) : env_(env) {
        obj_ = env->NewGlobalRef(obj);
    }
    ~GlobalRef() {
        if (obj_) env_->DeleteGlobalRef(obj_);
    }
    jobject get() const { return obj_; }
private:
    JNIEnv* env_;
    jobject obj_;
};

} // namespace jni
} // namespace ronin

#endif // RONIN_JNI_UTILS_H_
