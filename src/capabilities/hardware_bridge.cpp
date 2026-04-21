#include "capabilities/hardware_bridge.h"
#include "ronin_log.h"
#include <thread>

#define TAG "RoninHardwareBridge"

namespace Ronin::Kernel::Capability {

JavaVM* HardwareBridge::s_vm = nullptr;
jobject HardwareBridge::s_instance = nullptr;
jclass HardwareBridge::s_clazz = nullptr;

float HardwareBridge::s_last_temp = 0.0f;
float HardwareBridge::s_last_ram_used = 0.0f;
float HardwareBridge::s_last_ram_total = 0.0f;

void HardwareBridge::initialize(JavaVM* vm, jobject instance) {
#ifdef __ANDROID__
    s_vm = vm;
    
    // Ensure we have a global reference that persists across JNI calls
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) {
        if (s_instance) {
            env->DeleteGlobalRef(s_instance);
        }
        if (s_clazz) {
            env->DeleteGlobalRef(s_clazz);
        }
        s_instance = env->NewGlobalRef(instance);
        
        jclass localClazz = env->GetObjectClass(instance);
        s_clazz = (jclass)env->NewGlobalRef(localClazz);
        env->DeleteLocalRef(localClazz);

        LOGI(TAG, "HardwareBridge initialized with new GlobalRef and ClassRef.");
    }
#endif
}

void HardwareBridge::release(JNIEnv* env) {
#ifdef __ANDROID__
    if (s_instance && env) {
        env->DeleteGlobalRef(s_instance);
        s_instance = nullptr;
    }
    if (s_clazz && env) {
        env->DeleteGlobalRef(s_clazz);
        s_clazz = nullptr;
    }
    s_vm = nullptr;
#endif
}

void HardwareBridge::reportSystemHealth(float temperature, float ramUsedGB, float ramTotalGB) {
    s_last_temp = temperature;
    s_last_ram_used = ramUsedGB;
    s_last_ram_total = ramTotalGB;

#ifdef __ANDROID__
    if (!s_vm || !s_instance || !s_clazz) return;

    JNIEnv* env = nullptr;
    bool attached = false;
    if (s_vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (s_vm->AttachCurrentThread(&env, nullptr) != 0) return;
        attached = true;
    }

    if (env) {
        jmethodID mid = env->GetMethodID(s_clazz, "updateSystemTiers", "(FFF)V");
        if (mid) {
            env->CallVoidMethod(s_instance, mid, temperature, ramUsedGB, ramTotalGB);
        }
    }

    if (attached) s_vm->DetachCurrentThread();
#endif
}

std::string HardwareBridge::getCloudApiKey(const std::string& provider) {
#ifdef __ANDROID__
    if (!s_vm || !s_instance || !s_clazz) return "";

    JNIEnv* env = nullptr;
    bool attached = false;
    if (s_vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (s_vm->AttachCurrentThread(&env, nullptr) != 0) return "";
        attached = true;
    }

    std::string result = "";
    if (env) {
        jmethodID mid = env->GetMethodID(s_clazz, "getSecureApiKey", "(Ljava/lang/String;)Ljava/lang/String;");
        if (mid) {
            jstring jprovider = env->NewStringUTF(provider.c_str());
            jstring jkey = (jstring)env->CallObjectMethod(s_instance, mid, jprovider);
            if (jkey) {
                const char* cstr = env->GetStringUTFChars(jkey, nullptr);
                if (cstr) {
                    result = std::string(cstr);
                    env->ReleaseStringUTFChars(jkey, cstr);
                }
                env->DeleteLocalRef(jkey);
            }
            env->DeleteLocalRef(jprovider);
        }
    }

    if (attached) s_vm->DetachCurrentThread();
    return result;
#else
    return "mock_key_host_build";
#endif
}

void HardwareBridge::pushMessage(const std::string& message) {
#ifdef __ANDROID__
    if (!s_vm || !s_instance || !s_clazz) return;

    JNIEnv* env = nullptr;
    bool attached = false;
    if (s_vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (s_vm->AttachCurrentThread(&env, nullptr) != 0) return;
        attached = true;
    }

    if (env) {
        jmethodID mid = env->GetMethodID(s_clazz, "pushKernelMessage", "(Ljava/lang/String;)V");
        if (mid) {
            jstring jmsg = env->NewStringUTF(message.c_str());
            env->CallVoidMethod(s_instance, mid, jmsg);
            env->DeleteLocalRef(jmsg);
        }
    }

    if (attached) s_vm->DetachCurrentThread();
#endif
}

std::string HardwareBridge::requestData(uint32_t nodeId) {
#ifdef __ANDROID__
    if (!s_vm || !s_instance || !s_clazz) return "Error: HardwareBridge not initialized.";

    JNIEnv* env = nullptr;
    bool attached = false;
    if (s_vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (s_vm->AttachCurrentThread(&env, nullptr) != 0) return "Error: Thread attachment failed.";
        attached = true;
    }

    std::string result = "Error: Method invocation failed.";
    if (env) {
        jmethodID mid = env->GetMethodID(s_clazz, "requestHardwareData", "(I)Ljava/lang/String;");
        if (mid) {
            jstring jstr = (jstring)env->CallObjectMethod(s_instance, mid, static_cast<jint>(nodeId));
            if (jstr) {
                const char* cstr = env->GetStringUTFChars(jstr, nullptr);
                if (cstr) {
                    result = std::string(cstr);
                    env->ReleaseStringUTFChars(jstr, cstr);
                }
                env->DeleteLocalRef(jstr);
            }
        } else {
            result = "Error: Method requestHardwareData not found.";
        }
    }

    if (attached) s_vm->DetachCurrentThread();
    return result;
#else
    return "Host Build: Hardware data retrieval mocked for Node " + std::to_string(nodeId);
#endif
}

std::string HardwareBridge::fetchCloudResponse(const std::string& input, const std::string& provider) {
#ifdef __ANDROID__
    if (!s_vm || !s_instance || !s_clazz) return "Error: HardwareBridge not initialized.";

    JNIEnv* env = nullptr;
    bool attached = false;
    if (s_vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (s_vm->AttachCurrentThread(&env, nullptr) != 0) return "Error: Thread attachment failed.";
        attached = true;
    }

    std::string result = "Error: Method performCloudInference failed.";
    if (env) {
        jmethodID mid = env->GetMethodID(s_clazz, "performCloudInference", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
        if (mid) {
            jstring jinput = env->NewStringUTF(input.c_str());
            jstring jprovider = env->NewStringUTF(provider.c_str());
            jstring jstr = (jstring)env->CallObjectMethod(s_instance, mid, jinput, jprovider);
            
            if (jstr) {
                const char* cstr = env->GetStringUTFChars(jstr, nullptr);
                if (cstr) {
                    result = std::string(cstr);
                    env->ReleaseStringUTFChars(jstr, cstr);
                }
                env->DeleteLocalRef(jstr);
            }
            env->DeleteLocalRef(jinput);
            env->DeleteLocalRef(jprovider);
        }
    }

    if (attached) s_vm->DetachCurrentThread();
    return result;
#else
    return "Host Build: Cloud response mocked for " + provider;
#endif
}

bool HardwareBridge::triggerSync(uint32_t nodeId, bool state) {
#ifdef __ANDROID__
    if (!s_vm || !s_instance || !s_clazz) return state;

    JNIEnv* env = nullptr;
    bool attached = false;
    if (s_vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (s_vm->AttachCurrentThread(&env, nullptr) != 0) return state;
        attached = true;
    }

    bool actual_state = state;
    if (env) {
        jmethodID mid = env->GetMethodID(s_clazz, "triggerHardwareAction", "(IZ)Z");
        if (mid) {
            actual_state = (bool)env->CallBooleanMethod(s_instance, mid, static_cast<jint>(nodeId), static_cast<jboolean>(state));
        }
    }

    if (attached) s_vm->DetachCurrentThread();
    return actual_state;
#else
    LOGI(TAG, "Host Build: Bypassing synchronous hardware trigger for Node %u", nodeId);
    return state;
#endif
}

void HardwareBridge::triggerAsync(uint32_t nodeId, bool state) {
#ifdef __ANDROID__
    if (!s_vm || !s_instance || !s_clazz) {
        LOGE(TAG, "HardwareBridge NOT initialized. Skipping trigger for Node %u", nodeId);
        return;
    }

    // Launch a detached thread to ensure the Reasoning Spine never blocks.
    // This satisfies the "C++ execution threads MUST NEVER block" mandate.
    std::thread([nodeId, state]() {
        JNIEnv* env = nullptr;
        bool attached = false;

        jint res = s_vm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (res == JNI_EDETACHED) {
            JavaVMAttachArgs args;
            args.version = JNI_VERSION_1_6;
            args.name = "RoninHardwareThread";
            args.group = nullptr;

            if (s_vm->AttachCurrentThread(&env, &args) != 0) {
                LOGE(TAG, "Failed to attach hardware thread to JVM.");
                return;
            }
            attached = true;
        }

        if (env && s_instance && s_clazz) {
            jmethodID mid = env->GetMethodID(s_clazz, "triggerHardwareAction", "(IZ)Z");
            if (mid) {
                LOGI(TAG, "Dispatching async hardware action: Node %u, State %d", nodeId, state);
                env->CallBooleanMethod(s_instance, mid, static_cast<jint>(nodeId), static_cast<jboolean>(state));
            } else {
                LOGE(TAG, "Could not find triggerHardwareAction methodID.");
            }
        }

        if (attached) {
            s_vm->DetachCurrentThread();
        }
    }).detach();
#else
    LOGI(TAG, "Host Build: Bypassing hardware trigger for Node %u", nodeId);
#endif
}

} // namespace Ronin::Kernel::Capability
