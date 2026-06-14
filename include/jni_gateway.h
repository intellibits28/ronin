#pragma once
#include "ronin_jni_stubs.h"
#include <string>
#include <mutex>
#include <unordered_map>
#include "execution_context.h"
#include "ronin_types.hpp"

namespace Ronin::Kernel::JNI {

class JniExecutionGateway {
public:
    static JniExecutionGateway& getInstance();

    // Validates context, attaches token, checks SafeMode
    KernelResult<Execution::ExecutionContextPtr> createAndValidateContext(
        JNIEnv* env, jstring jSessionId, jstring jExecId, jstring jCorrelationId);

    // Propagates Kotlin cancellation to C++
    void propagateCancellation(const std::string& exec_id);

    // v10.6 SafeMode global trigger
    void triggerSafeMode();

    // Lifecycle
    void registerExecution(Execution::ExecutionContextPtr ctx);
    void unregisterExecution(const std::string& exec_id);

    Execution::ExecutionContextPtr getContext(const std::string& exec_id);

private:
    JniExecutionGateway() = default;
    std::mutex m_mutex;
    std::unordered_map<std::string, Execution::ExecutionContextPtr> m_active_executions;
};

} // namespace Ronin::Kernel::JNI
