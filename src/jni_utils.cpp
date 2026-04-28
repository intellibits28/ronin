#include "jni_utils.h"
#include <pthread.h>

namespace ronin {
namespace jni {

JNIEnv* GetJNIEnv(JavaVM* vm) {
    JNIEnv* env;
    jint res = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (res == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != 0) {
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

} // namespace jni
} // namespace ronin
