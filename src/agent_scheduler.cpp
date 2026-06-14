#include "agent_scheduler.h"
#include "graph_executor.h"
#include "ronin_log.h"
#include "capabilities/hardware_bridge.h"
#include "execution_budget.h"
#include "runtime_healing_controller.h"
#include "adaptive_budget_controller.h"
#include "failure_telemetry_bus.h"
#include "jni_gateway.h"
#include <chrono>
#include <nlohmann/json.hpp>
#include <thread>

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

void AgentScheduler::purgeQueue() {
    std::lock_guard<std::mutex> lock(m_mutex);
    while(!m_task_queue.empty()) m_task_queue.pop();
    LOGI(TAG, "Scheduler queue purged for SafeMode.");
}

void AgentScheduler::schedule(std::shared_ptr<AgentSession> session, uint32_t priority) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_task_queue.size() >= 50) {
        LOGE(TAG, "Scheduler queue full. Dropping session %s", session->getSessionId().c_str());
        session->abortSession(AgentSession::Error::QUEUE_FULL);
        return;
    }
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
                
                auto exec_ctx = current_session->getExecutionContext();
                if (exec_ctx) {
                    Execution::ExecutionTelemetryBus::getInstance().logNodeStart(exec_ctx->session_id, exec_ctx->execution_id, step);
                }
                
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
                         s_lower.find("query_fact") != std::string::npos || s_lower.find("query_vault") != std::string::npos || 
                         s_lower.find("lookup") != std::string::npos || s_lower.find("memory") != std::string::npos || 
                         s_lower.find("မှတ်") != std::string::npos || s_lower.find("vault") != std::string::npos || 
                         s_lower.find("fact") != std::string::npos || s_lower.find("get_vault_content") != std::string::npos ||
                         s_lower.find("return_result") != std::string::npos || s_lower.find("return_content") != std::string::npos || s_lower.find("search_database") != std::string::npos)
                    type = CapabilityType::MEMORY;
                else if (s_lower.find("alarm") != std::string::npos || s_lower.find("wake") != std::string::npos || s_lower.find("နှိုး") != std::string::npos)
                    type = CapabilityType::ALARM;
                else if (s_lower.find("calendar") != std::string::npos || s_lower.find("event") != std::string::npos || s_lower.find("meeting") != std::string::npos)
                    type = CapabilityType::CALENDAR;
                else if (s_lower.find("file") != std::string::npos || s_lower.find("search_file") != std::string::npos || s_lower.find("find_file") != std::string::npos || s_lower.find("ဖိုင်") != std::string::npos)
                    type = CapabilityType::FILES;
                else if (s_lower.find("sensor") != std::string::npos || s_lower.find("vibration") != std::string::npos || s_lower.find("resonance") != std::string::npos || s_lower.find("တုန်ခါမှု") != std::string::npos || s_lower.find("read_sensor_data") != std::string::npos || s_lower.find("read_vibration_data") != std::string::npos)
                    type = CapabilityType::SENSOR;
                else if (s_lower.find("mock_test") != std::string::npos)
                    type = CapabilityType::TEST;
                
                if (type != CapabilityType::NONE) {
                    LOGI(TAG, "L8 Scheduler: Dispatching Capability %d to L10 Optimizer...", static_cast<int>(type));

                    Capability::HardwareBridge::updateDevHUD("EXECUTING", current_session->getIntent(), 1.0f, step);

                    // v9.2: Pass all session parameters to every step
                    nlohmann::json jParams;
                    for (const auto& [k, v] : current_session->getParameters()) jParams[k] = v;
                    jParams["action"] = step;
                    
                    auto exec_ctx = current_session->getExecutionContext();
                    std::string exec_id = exec_ctx ? exec_ctx->execution_id : "legacy";
                    
                    // v1.5: Adaptive Budget Allocation
                    uint32_t allocated_budget = Execution::AdaptiveBudgetController::getInstance().getAdaptedBudget(exec_id, step);
                    if (exec_ctx) {
                        Execution::ExecutionBudgetController::getInstance().allocateBudget(exec_id, allocated_budget);
                    }

                    int retry_count = 0;
                    const int MAX_RETRIES = 3;
                    bool step_success = false;

                    while (retry_count <= MAX_RETRIES) {
                        auto start_time = std::chrono::steady_clock::now();
                        auto future = m_executor->optimizeAndDispatch(type, jParams.dump(), exec_ctx);
                        
                        try {
                            LOGI(TAG, "L8 Scheduler: Waiting for Driver response (Attempt %d)...", retry_count + 1);
                            bool finished = false;
                            
                            while (!finished) {
                                if (future.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
                                    step_success = future.get();
                                    finished = true;
                                    break;
                                }
                                
                                if (current_session->getToken() && current_session->getToken()->isCancelled()) {
                                    LOGE(TAG, "L8 Scheduler: Session was cancelled during wait.");
                                    all_steps_success = false;
                                    finished = true;
                                    break;
                                }
                                
                                auto now = std::chrono::steady_clock::now();
                                uint32_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
                                if (elapsed >= 45000) { // 45s hard timeout per step
                                    LOGE(TAG, "L8 Scheduler: Step '%s' timed out (Watchdog).", step.c_str());
                                    Execution::FailureTelemetryBus::getInstance().logFailure(exec_id, step, FailureType::TIMEOUT, "ABORT_TIMEOUT");
                                    Capability::HardwareBridge::pushMessage("[AGENT] Step timed out: " + step);
                                    current_session->abortSession(AgentSession::Error::TIMEOUT);
                                    all_steps_success = false;
                                    finished = true;
                                    break;
                                }
                            }
                            
                            if (!finished || !all_steps_success) break;

                            auto end_time = std::chrono::steady_clock::now();
                            uint32_t cost = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
                            
                            if (exec_ctx) {
                                if (!Execution::ExecutionBudgetController::getInstance().consumeBudget(exec_id, cost)) {
                                    LOGE(TAG, "L8 Scheduler: Execution Budget Exceeded! %s", exec_ctx->logPrefix().c_str());
                                    Execution::FailureTelemetryBus::getInstance().logFailure(exec_id, step, FailureType::BUDGET_EXCEEDED, "ABORT_BUDGET");
                                    current_session->abortSession(AgentSession::Error::DEPTH_EXCEEDED);
                                    all_steps_success = false;
                                    break;
                                }
                                Execution::ExecutionTelemetryBus::getInstance().logNodeEnd(exec_ctx->session_id, exec_id, step, cost, step_success ? "SUCCESS" : "FAILURE", cost);
                            }

                            // v1.5 Adaptive feedback
                            Execution::AdaptiveBudgetController::getInstance().reportExecution(step, cost, step_success);

                            if (step_success) break; // Success! exit retry loop

                            // v1.5 Retry logic with exponential backoff
                            retry_count++;
                            if (retry_count <= MAX_RETRIES) {
                                int backoff_ms = (retry_count == 1) ? 100 : (retry_count == 2 ? 500 : 1500);
                                LOGW(TAG, "L8 Scheduler: Step '%s' failed. Retrying in %d ms...", step.c_str(), backoff_ms);
                                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                            } else {
                                LOGE(TAG, "L8 Scheduler: Step '%s' failed after %d retries.", step.c_str(), MAX_RETRIES);
                                Execution::FailureTelemetryBus::getInstance().logFailure(exec_id, step, FailureType::UNKNOWN, "MAX_RETRIES_EXCEEDED");
                                Execution::RuntimeHealingController::getInstance().detectInstability();
                                Capability::HardwareBridge::pushMessage("[AGENT] Step failed: " + step);
                                all_steps_success = false;
                            }
                        } catch (const std::exception& e) {
                            LOGE(TAG, "L8 Scheduler: Exception during step '%s': %s", step.c_str(), e.what());
                            Execution::FailureTelemetryBus::getInstance().logFailure(exec_id, step, FailureType::JNI_EXCEPTION, e.what());
                            Execution::RuntimeHealingController::getInstance().detectInstability();
                            all_steps_success = false;
                            break;
                        }
                    } // end retry loop

                    if (!all_steps_success) break; // exit step loop
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

            // v1.5 Clean up JNI Gateway registration
            if (auto exec_ctx = current_session->getExecutionContext()) {
                JNI::JniExecutionGateway::getInstance().unregisterExecution(exec_ctx->execution_id);
            }
        }
    }
}

} // namespace Ronin::Kernel
