#pragma once

#ifdef __ANDROID__
#include <jni.h>
#else
// Stub definitions for JNI types to allow host compilation of unit tests
typedef void* JNIEnv;
typedef void* jobject;
typedef void* jclass;
typedef void* JavaVM;
#endif

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
     * Triggers JNI callback to report memory and thermal tiers to the UI.
     */
    static void reportSystemHealth(float temperature, float ramUsedGB, float ramTotalGB);

    /**
     * Retrieves encrypted API keys from AndroidKeyStore via JNI.
     */
    static std::string getCloudApiKey(const std::string& provider);

    /**
     * Pushes a reasoning/decision log message to the Kotlin UI Reasoning Console.
     */
    static void pushMessage(const std::string& message);

    /**
     * Synchronously fetches hardware data from the Kotlin side.
     */
    static std::string requestData(uint32_t nodeId);

    /**
     * Synchronously dispatches a hardware action and returns the actual state.
     */
    static bool triggerSync(uint32_t nodeId, bool state);

    /**
     * Dispatches a hardware action to the Kotlin side.
     * Guaranteed to be non-blocking for the calling C++ thread.
     */
    static void triggerAsync(uint32_t nodeId, bool state);

private:
    static JavaVM* s_vm;
    static jobject s_instance;
    static jclass s_clazz;
};

} // namespace Ronin::Kernel::Capability
