#include "agent_scheduler.h"
#include "graph_executor.h"
#include "ronin_log.h"
#include "capabilities/hardware_bridge.h"
#include <chrono>

#define TAG "RoninScheduler"

namespace Ronin::Kernel {

AgentScheduler& AgentScheduler::getInstance() {
    static AgentScheduler instance;
    return instance;
}

AgentScheduler::AgentScheduler() {
    start();
}

AgentScheduler::~AgentScheduler() {
    stop();
}

void AgentScheduler::schedule(std::shared_ptr<AgentSession> session, uint32_t priority) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_task_queue.push({priority, session});
    LOGI(TAG, "Task scheduled for session: %s (Priority: %u)", session->getSessionId().c_str(), priority);
    m_cv.notify_one();
}

void AgentScheduler::start() {
    if (m_running) return;
    m_running = true;
    m_worker = std::thread(&AgentScheduler::workerLoop, this);
    LOGI(TAG, "Scheduler worker thread started.");
}

void AgentScheduler::stop() {
    m_running = false;
    m_cv.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
    LOGI(TAG, "Scheduler worker thread stopped.");
}

void AgentScheduler::workerLoop() {
    while (m_running) {
        std::shared_ptr<AgentSession> current_session;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_running || !m_task_queue.empty(); });
            
            if (!m_running) break;
            
            current_session = m_task_queue.top().session;
            m_task_queue.pop();
        }

        if (current_session && m_executor) {
            LOGI(TAG, "L8 Scheduler: Worker thread active. Session: %s, Intent: %s", 
                 current_session->getSessionId().c_str(), current_session->getIntent().c_str());
            
            current_session->setState(AgentState::EXECUTE);
            Capability::HardwareBridge::pushMessage("[AGENT] Starting: " + current_session->getIntent());

            // v7.0 Layer 10 Integration: Execute each step in the plan
            for (const auto& step : current_session->getPlan()) {
                LOGI(TAG, "L8 Scheduler: Executing Step -> '%s'", step.c_str());
                Capability::HardwareBridge::pushMessage("[AGENT] Step: " + step);
                
                // v7.7 Robust Step-to-Capability Mapping
                CapabilityType type = CapabilityType::NONE;
                std::string s_lower = step;
                std::transform(s_lower.begin(), s_lower.end(), s_lower.begin(), ::tolower);

                if (s_lower.find("location") != std::string::npos || s_lower.find("တည်နေရာ") != std::string::npos || s_lower.find("နေရာ") != std::string::npos || s_lower.find("mock_location") != std::string::npos) 
                    type = CapabilityType::LOCATION;
                else if (s_lower.find("sms") != std::string::npos || s_lower.find("message") != std::string::npos || s_lower.find("ပို့") != std::string::npos || s_lower.find("send") != std::string::npos || s_lower.find("mock_sms") != std::string::npos) 
                    type = CapabilityType::SMS;
                else if (s_lower.find("map") != std::string::npos || s_lower.find("မြေပုံ") != std::string::npos)
                    type = CapabilityType::MAP;
                else if (s_lower.find("mock_test") != std::string::npos)
                    type = CapabilityType::TEST;
                
                if (type != CapabilityType::NONE) {
                    LOGI(TAG, "L8 Scheduler: Dispatching Capability %d to L10 Optimizer...", static_cast<int>(type));
                    // Call Layer 10 Optimizer and WAIT for completion (v7.4)
                    auto future = m_executor->optimizeAndDispatch(type, current_session->getSessionId(), "");
                    
                    try {
                        LOGI(TAG, "L8 Scheduler: Waiting for Driver response (future.get)...");
                        bool step_success = future.get(); // Synchronous block in worker thread
                        LOGI(TAG, "L8 Scheduler: Driver responded with Success: %d", step_success);
                        if (!step_success) {
                            LOGE(TAG, "L8 Scheduler: Step '%s' failed. Stopping chain.", step.c_str());
                            Capability::HardwareBridge::pushMessage("[AGENT] Step failed: " + step);
                            current_session->setState(AgentState::FAILED);
                            break;
                        }
                    } catch (const std::exception& e) {
                        LOGE(TAG, "L8 Scheduler: Exception during step '%s': %s", step.c_str(), e.what());
                        break;
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            
            current_session->setState(AgentState::COMPLETED);
            Capability::HardwareBridge::pushMessage("[AGENT] Task completed successfully.");
            LOGI(TAG, "Worker session %s execution chain completed.", current_session->getSessionId().c_str());
        }
    }
}

} // namespace Ronin::Kernel
