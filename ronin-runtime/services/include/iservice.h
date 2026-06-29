#pragma once

#include "kernel/include/execution_context.h"
#include <string>
#include <memory>
#include <future>

namespace Ronin::Kernel::Services {

enum class ErrorCode {
    NONE = 0,
    TIMEOUT = 1,
    PERMISSION_DENIED = 2,
    SENSOR_UNAVAILABLE = 3,
    CANCELLED = 4,
    MEMORY_FULL = 5,
    NETWORK_LOST = 6,
    INTERNAL_ERROR = 7,
    INVALID_PARAMETER = 8
};

struct ServiceRequest {
    std::string action;
    std::string payload_json;
};

struct ServiceResult {
    bool success = false;
    std::string result_json;
    ErrorCode error_code = ErrorCode::NONE;
    std::string error_message;
};

class TaskHandle {
public:
    virtual ~TaskHandle() = default;
    virtual bool cancel() = 0;
    virtual bool isFinished() = 0;
    virtual ServiceResult await() = 0;
};

class IService {
public:
    virtual ~IService() = default;
    virtual std::unique_ptr<TaskHandle> execute(const ServiceRequest& request, const ExecutionContext& ctx) = 0;
};

} // namespace Ronin::Kernel::Services
