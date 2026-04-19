#pragma once

#include <jni.h>
#include <string>
#include <future>

namespace Ronin::Kernel::Capability {

/**
 * Phase 4.0: Unified Hardware Bridge.
 * Ensures JNI thread safety and asynchronicity for hardware calls.
 */
class HardwareBridge {
public:
    static void initialize(JavaVM* vm, jobject instance);
    static void release(JNIEnv* env);

    /**
     * Dispatches a hardware action to the Kotlin side.
     * Guaranteed to be non-blocking for the calling C++ thread.
     */
    static void triggerAsync(uint32_t nodeId, bool state);

private:
    static JavaVM* s_vm;
    static jobject s_instance;
};

} // namespace Ronin::Kernel::Capability
