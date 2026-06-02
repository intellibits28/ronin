#include "android_bridge.h"
#include "capabilities/hardware_bridge.h"
#include "ronin_log.h"
#include "jni_utils.h"

#define TAG "RoninAndroidBridge"

namespace Ronin::Kernel {

#include "capabilities/hardware_bridge.h"
#include "ronin_log.h"
#include "jni_utils.h"
#include <jni.h>

// Forward declaration of HardwareBridge members if needed, or use a helper
// For now, let's assume we can access g_vm and g_instance from ronin_jni context
// In a real implementation, we'd need a robust way to get the JNIEnv
extern JavaVM* g_vm;
extern jobject g_instance; // This was thiz in initializeKernel

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
    if (g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) {
        jclass cls = env->GetObjectClass(g_instance);
        jmethodID method = env->GetMethodID(cls, "onCapabilityRequest", "(Ljava/lang/String;)Ljava/lang/String;");
        if (method) {
            jstring jStr = env->NewStringUTF(json_str.c_str());
            jstring jRes = (jstring)env->CallObjectMethod(g_instance, method, jStr);
            // Handle response via CapabilityDispatcher::onResponse
            // (In a real async flow, this would be on a background thread)
            env->DeleteLocalRef(jStr);
            if (jRes) env->DeleteLocalRef(jRes);
        }
    }
}

} // namespace Ronin::Kernel
