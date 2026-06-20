#include "android_bridge.h"
#include "capability_response.h"
#include "capability_dispatcher.h"
#include "ronin_log.h"
#include <nlohmann/json.hpp>

#ifdef __ANDROID__
#include <jni.h>
#include "jni_utils.h"
#include "jni/ronin_jni_context.h"

#define TAG "RoninAndroidBridge"

namespace Ronin::Kernel {

void AndroidBridge::sendRequest(const CapabilityRequest& req) {
    nlohmann::json j;
    j["request_id"] = req.request_id;
    j["session_id"] = req.session_id;
    j["capability"] = CapabilityTypeToString(req.capability);
    j["payload"] = req.payload_json;

    std::string json_str = j.dump();
    LOGI(TAG, "Sending JSON request to Kotlin: %s", json_str.c_str());

    JNIEnv* env = nullptr;
    bool attached = false;
    auto& runtime = JNI::runtimeContext();
    if (runtime.vm && runtime.instance) {
        jint get_env_res = runtime.vm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (get_env_res == JNI_EDETACHED) {
            if (runtime.vm->AttachCurrentThread(&env, nullptr) != 0) {
                LOGE(TAG, "Failed to attach bridge thread to JVM.");
                return;
            }
            attached = true;
        }

        if (env) {
            jclass cls = env->GetObjectClass(runtime.instance);
            jmethodID method = env->GetMethodID(cls, "onCapabilityRequest", "(Ljava/lang/String;)Ljava/lang/String;");
            if (method) {
                jstring jStr = env->NewStringUTF(json_str.c_str());
                jstring jRes = (jstring)env->CallObjectMethod(runtime.instance, method, jStr);
                
                if (jRes) {
                    const char* cstr = env->GetStringUTFChars(jRes, nullptr);
                    std::string res_json(cstr);
                    
                    // v8.1: Feedback loop to unblock future.get()
                    CapabilityResponse response;
                    response.request_id = req.request_id;
                    
                    try {
                        auto res_obj = nlohmann::json::parse(res_json);
                        
                        // Phase 4.x: Support asynchronous HITL responses
                        if (res_obj.value("status", "") == "PENDING") {
                            LOGI(TAG, "Request %s is PENDING async human confirmation.", req.request_id.c_str());
                            env->ReleaseStringUTFChars(jRes, cstr);
                            env->DeleteLocalRef(jRes);
                            env->DeleteLocalRef(jStr);
                            if (attached) runtime.vm->DetachCurrentThread();
                            return; // Do not call onResponse here; wait for submitCapabilityResponseNative
                        }
                        
                        response.success = res_obj.value("success", false);
                        response.payload_json = res_json;
                    } catch(...) {
                        response.success = false;
                        response.error = "Invalid JSON response from Kotlin";
                    }
                    
                    CapabilityDispatcher::getInstance().onResponse(response);
                    
                    env->ReleaseStringUTFChars(jRes, cstr);
                    env->DeleteLocalRef(jRes);
                }
                env->DeleteLocalRef(jStr);
            }
            env->DeleteLocalRef(cls);
        }

        if (attached) {
            runtime.vm->DetachCurrentThread();
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
