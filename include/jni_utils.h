#pragma once

#include <jni.h>
#include "ronin_log.h"

namespace Ronin::Kernel::JNI {

/**
 * RAII-based JNI Environment Manager.
 * Ensures strict AttachCurrentThread/DetachCurrentThread pairing.
 */
class ScopedJniEnv {
public:
    ScopedJniEnv(JavaVM* vm, const char* threadName = "RoninNativeThread") : m_vm(vm) {
        if (vm->GetEnv((void**)&m_env, JNI_VERSION_1_6) == JNI_EDETACHED) {
            JavaVMAttachArgs args = { JNI_VERSION_1_6, const_cast<char*>(threadName), nullptr };
            if (vm->AttachCurrentThread(&m_env, &args) != 0) {
                LOGE("ScopedJniEnv", "Failed to attach thread: %s", threadName);
                m_env = nullptr;
            } else {
                m_attached = true;
            }
        }
    }

    ~ScopedJniEnv() {
        if (m_attached && m_vm) {
            m_vm->DetachCurrentThread();
        }
    }

    JNIEnv* env() const { return m_env; }
    bool isValid() const { return m_env != nullptr; }

private:
    JavaVM* m_vm;
    JNIEnv* m_env = nullptr;
    bool m_attached = false;
};

} // namespace Ronin::Kernel::JNI
