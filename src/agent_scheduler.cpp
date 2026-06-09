#include "agent_scheduler.h"
#include "graph_executor.h"
#include "ronin_log.h"
#include "capabilities/hardware_bridge.h"
#include <chrono>
#include <nlohmann/json.hpp>

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
            bool all_steps_success = true;
            for (const auto& step : current_session->getPlan()) {
                LOGI(TAG, "L8 Scheduler: Executing Step -> '%s'", step.c_str());
                Capability::HardwareBridge::pushMessage("[AGENT] Step: " + step);
                
                // v7.7 Robust Step-to-Capability Mapping
                CapabilityType type = CapabilityType::NONE;
                std::string s_lower = step;
                std::transform(s_lower.begin(), s_lower.end(), s_lower.begin(), ::tolower);

                // v8.5: Prioritize MAP and SMS over generic LOCATION
                if (s_lower.find("map") != std::string::npos || s_lower.find("မြေပုံ") != std::string::npos)
                    type = CapabilityType::MAP;
                else if (s_lower.find("sms") != std::string::npos || s_lower.find("message") != std::string::npos || s_lower.find("ပို့") != std::string::npos || s_lower.find("send") != std::string::npos || s_lower.find("mock_sms") != std::string::npos) 
                    type = CapabilityType::SMS;
                else if (s_lower.find("location") != std::string::npos || s_lower.find("တည်နေရာ") != std::string::npos || s_lower.find("နေရာ") != std::string::npos || s_lower.find("mock_location") != std::string::npos) 
                    type = CapabilityType::LOCATION;
                else if (s_lower.find("contact") != std::string::npos || s_lower.find("resolve") != std::string::npos || s_lower.find("ရှာ") != std::string::npos)
                    type = CapabilityType::CONTACTS;
                else if (s_lower.find("save") != std::string::npos || s_lower.find("store") != std::string::npos || 
                         s_lower.find("add_fact") != std::string::npos || s_lower.find("record") != std::string::npos || 
                         s_lower.find("query") != std::string::npos || s_lower.find("lookup") != std::string::npos || 
                         s_lower.find("memory") != std::string::npos || s_lower.find("မှတ်") != std::string::npos)
                    type = CapabilityType::MEMORY;
                else if (s_lower.find("alarm") != std::string::npos || s_lower.find("wake") != std::string::npos || s_lower.find("နှိုး") != std::string::npos)
                    type = CapabilityType::ALARM;
                else if (s_lower.find("calendar") != std::string::npos || s_lower.find("event") != std::string::npos || s_lower.find("meeting") != std::string::npos)
                    type = CapabilityType::CALENDAR;
                else if (s_lower.find("file") != std::string::npos || s_lower.find("search") != std::string::npos || s_lower.find("find") != std::string::npos || s_lower.find("ရှာ") != std::string::npos)
                    type = CapabilityType::FILES;
                else if (s_lower.find("mock_test") != std::string::npos)
                    type = CapabilityType::TEST;
                
                if (type != CapabilityType::NONE) {
                    LOGI(TAG, "L8 Scheduler: Dispatching Capability %d to L10 Optimizer...", static_cast<int>(type));

                    Capability::HardwareBridge::updateDevHUD("EXECUTING", current_session->getIntent(), 1.0f, step);

                    // v9.2: Pass all session parameters to every step
                    nlohmann::json jParams;
                    for (const auto& [k, v] : current_session->getParameters()) jParams[k] = v;
                    
                    // v10.1.22: Inject current step as 'action' for accurate Kotlin tool routing
                    jParams["action"] = step;
                    
                    // Call Layer 10 Optimizer and WAIT for completion (v7.4)
                    auto future = m_executor->optimizeAndDispatch(type, current_session->getSessionId(), jParams.dump());
                    
                    try {
                        LOGI(TAG, "L8 Scheduler: Waiting for Driver response (future.get)...");
                        bool step_success = future.get(); // Synchronous block in worker thread
                        LOGI(TAG, "L8 Scheduler: Driver responded with Success: %d", step_success);
                        if (!step_success) {
                            LOGE(TAG, "L8 Scheduler: Step '%s' failed. Stopping chain.", step.c_str());
                            Capability::HardwareBridge::pushMessage("[AGENT] Step failed: " + step + ". Check console for details.");
                            current_session->setState(AgentState::FAILED);
                            all_steps_success = false;
                            break;
                        }
                    } catch (const std::exception& e) {
                        LOGE(TAG, "L8 Scheduler: Exception during step '%s': %s", step.c_str(), e.what());
                        all_steps_success = false;
                        break;
                    }
                }
            }
            
            if (all_steps_success) {
                current_session->setState(AgentState::COMPLETED);
                Capability::HardwareBridge::pushMessage("[AGENT] Task completed successfully.");
                Capability::HardwareBridge::updateDevHUD("COMPLETED", current_session->getIntent(), 1.0f, "");
                LOGI(TAG, "Worker session %s execution chain completed.", current_session->getSessionId().c_str());

                // v11.3: Automatic Episodic Memory Logging
                if (m_executor) {
                    nlohmann::json jPayload;
                    for (const auto& [k, v] : current_session->getParameters()) jPayload[k] = v;
                    std::string summary = "Successfully executed " + current_session->getIntent();
                    m_executor->recordEpisode(current_session->getIntent(), summary, jPayload.dump(), true);
                }
            } else {
                Capability::HardwareBridge::updateDevHUD("FAILED", current_session->getIntent(), 0.0f, "");
                if (m_executor) {
                    m_executor->recordEpisode(current_session->getIntent(), "Failed task: " + current_session->getIntent(), "{}", false);
                }
            }
        }
    }
}

} // namespace Ronin::Kernel
