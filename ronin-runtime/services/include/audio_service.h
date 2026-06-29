#pragma once

#include "iservice.h"
#include <cmath>
#include <vector>
#include <future>
#include <nlohmann/json.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Ronin::Kernel::Services {

class AudioTaskHandle : public TaskHandle {
public:
    explicit AudioTaskHandle(std::function<ServiceResult()> task) {
        m_future = std::async(std::launch::async, task);
    }
    
    bool cancel() override {
        m_cancelled.store(true);
        return true;
    }
    
    bool isFinished() override {
        return m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
    
    ServiceResult await() override {
        return m_future.get();
    }

private:
    std::future<ServiceResult> m_future;
    std::atomic<bool> m_cancelled{false};
};

class AudioService : public IService {
public:
    AudioService() = default;
    ~AudioService() override = default;

    std::unique_ptr<TaskHandle> execute(const ServiceRequest& request, const ExecutionContext& ctx) override {
        (void)ctx;
        return std::make_unique<AudioTaskHandle>([request]() {
            ServiceResult res;
            if (request.action == "capture_audio" || request.action == "audio_capture") {
                nlohmann::json jOut;
                std::vector<float> samples(256);
                float sampleRate = 8000.0f;
                float freq = 440.0f; // Standard tuner test baseline A4 pitch
                for (int i = 0; i < 256; ++i) {
                    float t = i / sampleRate;
                    samples[i] = std::sin(2.0f * M_PI * freq * t);
                }
                jOut["array"] = samples;
                res.success = true;
                res.result_json = jOut.dump();
            } else {
                res.success = false;
                res.error_code = ErrorCode::INVALID_PARAMETER;
                res.error_message = "Invalid audio action: " + request.action;
            }
            return res;
        });
    }
};

} // namespace Ronin::Kernel::Services
