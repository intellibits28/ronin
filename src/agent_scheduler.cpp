#include "agent_scheduler.h"
#include "graph_executor.h"
#include "ronin_log.h"
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
            LOGI(TAG, "Worker executing multi-step plan for session: %s", current_session->getSessionId().c_str());
            current_session->setState(AgentState::EXECUTING);
            
            // v7.0 Layer 10 Integration: Execute each step in the plan
            for (const auto& step : current_session->getPlan()) {
                LOGI(TAG, "L8 Scheduler: Orchestrating step '%s'...", step.c_str());
                
                // v7.0 Layer 5: Map plan string to CapabilityType (Simplified)
                CapabilityType type = CapabilityType::NONE;
                if (step.find("location") != std::string::npos) type = CapabilityType::LOCATION;
                else if (step.find("sms") != std::string::npos) type = CapabilityType::SMS;
                else if (step.find("sensor") != std::string::npos) type = CapabilityType::SENSOR;
                
                if (type != CapabilityType::NONE) {
                    // Call Layer 10 Optimizer
                    m_executor->optimizeAndDispatch(type, current_session->getSessionId(), "");
                }
                
                // Small delay between steps to simulate network/system latency
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
            
            current_session->setState(AgentState::COMPLETED);
            LOGI(TAG, "Worker session %s execution chain completed.", current_session->getSessionId().c_str());
        }
    }
}

} // namespace Ronin::Kernel
