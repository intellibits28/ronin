#pragma once
#include <string>
#include <memory>
#include <atomic>
#include <chrono>
#include "ronin_types.hpp"

namespace Ronin::Kernel::Execution {

class ExecutionTelemetryBus {
public:
    static ExecutionTelemetryBus& getInstance();
    void logNodeStart(const std::string& session_id, const std::string& exec_id, const std::string& node_id);
    void logNodeEnd(const std::string& session_id, const std::string& exec_id, const std::string& node_id, int64_t latency_ms, const std::string& result, int budget_consumed);
    void logCancellation(const std::string& session_id, const std::string& exec_id, const std::string& reason);
    void logGatewayHop(const std::string& session_id, const std::string& exec_id, const std::string& direction);
private:
    ExecutionTelemetryBus() = default;
};

struct ExecutionContext {
    std::string session_id;
    std::string execution_id;
    std::string correlation_id;
    CancellationTokenPtr cancel_token;
    
    ExecutionContext() : cancel_token(std::make_shared<CancellationToken>()) {}
    
    std::string logPrefix() const {
        return "[" + session_id + " | " + execution_id + " | " + correlation_id + "]";
    }
};

using ExecutionContextPtr = std::shared_ptr<ExecutionContext>;

} // namespace Ronin::Kernel::Execution
