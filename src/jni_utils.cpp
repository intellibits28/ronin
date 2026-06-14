#include "jni_utils.h"
#include "ronin_log.h"
#include <string>

namespace Ronin::Kernel::JNI {

// v10.6: Hardened JNI Exception Handler
bool CheckJniException(JNIEnv* env) {
#ifdef __ANDROID__
    if (!env) return false;
    if (env->ExceptionCheck()) {
        jthrowable ex = env->ExceptionOccurred();
        env->ExceptionDescribe(); // Log to logcat
        env->ExceptionClear();
        
        LOGE("RoninJNI", "JVM Exception caught at JNI boundary. State cleared to prevent native crash.");
        
        // Let caller decide if they want to enter SafeMode based on return value
        return true;
    }
#endif
    return false;
}

std::string ConvertJStringToString(JNIEnv* env, jstring jstr) {
#ifdef __ANDROID__
    if (!jstr || !env) return "";
    
    // Check pending exceptions before calling GetStringUTFChars
    if (CheckJniException(env)) return "";

    const char* chars = env->GetStringUTFChars(jstr, nullptr);
    if (!chars) {
        LOGE("RoninJNI", "Critical: GetStringUTFChars returned null (OOM).");
        return "";
    }
    
    // Check if GetStringUTFChars caused an exception (e.g. OOM)
    if (CheckJniException(env)) {
        env->ReleaseStringUTFChars(jstr, chars);
        return "";
    }

    std::string str(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return str;
#else
    return "";
#endif
}

jstring ConvertStringToJString(JNIEnv* env, const std::string& str) {
#ifdef __ANDROID__
    if (!env) return nullptr;
    return env->NewStringUTF(str.c_str());
#else
    return nullptr;
#endif
}

} // namespace Ronin::Kernel::JNI
