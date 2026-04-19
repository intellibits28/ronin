#include "capabilities/hardware_bridge.h"
#include "ronin_log.h"
#include <thread>

#define TAG "RoninHardwareBridge"

namespace Ronin::Kernel::Capability {

JavaVM* HardwareBridge::s_vm = nullptr;
jobject HardwareBridge::s_instance = nullptr;

void HardwareBridge::initialize(JavaVM* vm, jobject instance) {
    s_vm = vm;
    
    // Ensure we have a global reference that persists across JNI calls
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) {
        if (s_instance) {
            env->DeleteGlobalRef(s_instance);
        }
        s_instance = env->NewGlobalRef(instance);
        LOGI(TAG, "HardwareBridge initialized with new GlobalRef.");
    }
}

void HardwareBridge::release(JNIEnv* env) {
    if (s_instance && env) {
        env->DeleteGlobalRef(s_instance);
        s_instance = nullptr;
    }
    s_vm = nullptr;
}

void HardwareBridge::triggerAsync(uint32_t nodeId, bool state) {
    if (!s_vm || !s_instance) {
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

        if (env && s_instance) {
            jclass clazz = env->GetObjectClass(s_instance);
            jmethodID mid = env->GetMethodID(clazz, "triggerHardwareAction", "(IZ)Z");
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
}

} // namespace Ronin::Kernel::Capability
