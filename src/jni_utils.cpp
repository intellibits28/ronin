#include "jni_utils.h"
#include <string>

namespace Ronin::Kernel::JNI {

std::string ConvertJStringToString(JNIEnv* env, jstring jstr) {
#ifdef __ANDROID__
    if (!jstr || !env) return "";
    const char* chars = env->GetStringUTFChars(jstr, nullptr);
    if (!chars) return "";
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
