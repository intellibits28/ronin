#include "android_bridge.h"
#include "ronin_log.h"
#include <nlohmann/json.hpp>

#ifdef __ANDROID__
#include <jni.h>
#include "jni_utils.h"

#define TAG "RoninAndroidBridge"

extern JavaVM* g_vm;
extern jobject g_instance;

namespace Ronin::Kernel {

void AndroidBridge::sendRequest(const CapabilityRequest& req) {
    nlohmann::json j;
    j["request_id"] = req.request_id;
    j["session_id"] = req.session_id;
    j["capability"] = (req.capability == CapabilityType::LOCATION) ? "LOCATION" : "SMS";
    j["payload"] = req.payload_json;

    std::string json_str = j.dump();
    LOGI(TAG, "Sending JSON request to Kotlin: %s", json_str.c_str());

    JNIEnv* env = nullptr;
    if (g_vm && g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) {
        jclass cls = env->GetObjectClass(g_instance);
        jmethodID method = env->GetMethodID(cls, "onCapabilityRequest", "(Ljava/lang/String;)Ljava/lang/String;");
        if (method) {
            jstring jStr = env->NewStringUTF(json_str.c_str());
            jstring jRes = (jstring)env->CallObjectMethod(g_instance, method, jStr);
            // Handle response logic here later
            env->DeleteLocalRef(jStr);
            if (jRes) env->DeleteLocalRef(jRes);
        }
    }
}

} // namespace Ronin::Kernel

#else

// No-op implementation for host testing
namespace Ronin::Kernel {
void AndroidBridge::sendRequest(const CapabilityRequest& req) {
    // Host mock: Just log to console
}
}

#endif
