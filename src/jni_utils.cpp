#include "jni_utils.h"
#include <string>

namespace Ronin::Kernel::JNI {

std::string ConvertJStringToString(JNIEnv* env, jstring jstr) {
    if (!jstr) return "";
    const char* chars = env->GetStringUTFChars(jstr, nullptr);
    if (!chars) return "";
    std::string str(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return str;
}

jstring ConvertStringToJString(JNIEnv* env, const std::string& str) {
    return env->NewStringUTF(str.c_str());
}

} // namespace Ronin::Kernel::JNI
