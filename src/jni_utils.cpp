#include "jni_utils.h"
#include <pthread.h>

namespace ronin {
namespace jni {

JNIEnv* GetJNIEnv(JavaVM* vm) {
    JNIEnv* env;
    jint res = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (res == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != 0) {
            LOGE("JNI_UTILS", "Failed to attach current thread");
            return nullptr;
        }
    }
    return env;
}

std::string JStringToStdString(JNIEnv* env, jstring jstr) {
    if (!jstr) return "";
    const char* chars = env->GetStringUTFChars(jstr, nullptr);
    std::string str(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return str;
}

jstring StdStringToJString(JNIEnv* env, const std::string& str) {
    return env->NewStringUTF(str.c_str());
}

void ThrowJavaException(JNIEnv* env, const char* message) {
    jclass exClass = env->FindClass("java/lang/RuntimeException");
    if (exClass) {
        env->ThrowNew(exClass, message);
    }
}

ScopedJniEnv::ScopedJniEnv(JavaVM* vm) : vm_(vm), env_(nullptr), attached_(false) {
    jint res = vm_->GetEnv((void**)&env_, JNI_VERSION_1_6);
    if (res == JNI_EDETACHED) {
        if (vm_->AttachCurrentThread(&env_, nullptr) == 0) {
            attached_ = true;
        } else {
            LOGE("JNI_UTILS", "ScopedJniEnv: Failed to attach thread");
            env_ = nullptr;
        }
    }
}

ScopedJniEnv::~ScopedJniEnv() {
    if (attached_ && vm_) {
        vm_->DetachCurrentThread();
    }
}

GlobalRef::GlobalRef(JNIEnv* env, jobject obj) : env_(env) {
    if (obj) {
        obj_ = env->NewGlobalRef(obj);
    }
}

GlobalRef::~GlobalRef() {
    if (obj_ && env_) {
        env_->DeleteGlobalRef(obj_);
    }
}

GlobalRef::GlobalRef(GlobalRef&& other) noexcept : env_(other.env_), obj_(other.obj_) {
    other.env_ = nullptr;
    other.obj_ = nullptr;
}

GlobalRef& GlobalRef::operator=(GlobalRef&& other) noexcept {
    if (this != &other) {
        if (obj_ && env_) {
            env_->DeleteGlobalRef(obj_);
        }
        env_ = other.env_;
        obj_ = other.obj_;
        other.env_ = nullptr;
        other.obj_ = nullptr;
    }
    return *this;
}

} // namespace jni
} // namespace ronin
