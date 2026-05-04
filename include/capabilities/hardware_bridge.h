#pragma once

#include "ronin_jni_stubs.h"
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

    static float getTemperature() { return s_last_temp; }
    static float getRamUsed() { return s_last_ram_used; }
    static float getRamTotal() { return s_last_ram_total; }
    static float getFreeRamGB() { return s_last_ram_total - s_last_ram_used; }

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

    /**
     * Phase 4.4.6: Cloud Bridge Activation
     * Performs a synchronous (but off-main-thread) network request via Kotlin.
     */
    static std::string fetchCloudResponse(const std::string& input, const std::string& provider);

    /**
     * Phase 6.0: Hybrid Neural Bridge
     * Calls back into Kotlin for local LiteRT-LM reasoning.
     */
    static std::string runNeuralReasoning(const std::string& input);

private:
    static JavaVM* s_vm;
    static jobject s_instance;
    static jclass s_clazz;

    static float s_last_temp;
    static float s_last_ram_used;
    static float s_last_ram_total;
};

} // namespace Ronin::Kernel::Capability
