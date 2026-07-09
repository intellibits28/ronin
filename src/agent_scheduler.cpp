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
            bool is_pitch_analysis = (current_session->getIntent() == "PITCH_ANALYSIS");
            bool is_vibration_analysis = (current_session->getIntent() == "ANALYZE_VIBRATION" || current_session->getIntent() == "SENSOR");
            for (const auto& st : current_session->getPlan()) {
                if (st == "audio_capture" || st == "fft" || st == "detect_peaks" || st == "note_mapper") is_pitch_analysis = true;
                if (st == "analyze_vibration" || st == "read_vibration_data" || st == "read_sensor_data" || st == "sensor") is_vibration_analysis = true;
            }
            int tuning_iteration = 0;
            const int MAX_TUNING_ITERATIONS = 100;
            const int MAX_SHM_ITERATIONS = 20;
            int max_iters = is_pitch_analysis ? MAX_TUNING_ITERATIONS : (is_vibration_analysis ? MAX_SHM_ITERATIONS : 1);

            while (tuning_iteration < max_iters) {
                tuning_iteration++;
                if ((is_pitch_analysis || is_vibration_analysis) && tuning_iteration > 1) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 10Hz update rate for iterative analysis / SHM settling
                    if (current_session->getToken() && current_session->getToken()->isCancelled()) {
                        LOGW(TAG, "L8 Scheduler: Analysis session cancelled.");
                        all_steps_success = false;
                        break;
                    }
                }

                all_steps_success = true;
                for (const auto& step : current_session->getPlan()) {
                    LOGI(TAG, "L8 Scheduler: Executing Step -> '%s'", step.c_str());
                    bool is_sensor_step = (step == "analyze_vibration" || step == "read_vibration_data" || step == "read_sensor_data" || step == "sensor" ||
                                           step == "audio_capture" || step == "fft" || step == "detect_peaks" || step == "note_mapper");
                    if ((is_vibration_analysis || is_pitch_analysis) && !is_sensor_step) {
                        bool sensor_ready = false;
                        if (is_vibration_analysis && m_executor) {
                            std::string vib_res = m_executor->getBlackboardValue("result_SENSOR");
                            if (vib_res.empty()) vib_res = m_executor->getBlackboardValue("result_analyze_vibration");
                            if (vib_res.empty()) vib_res = m_executor->getBlackboardValue("result_read_vibration_data");
                            if (!vib_res.empty()) {
                                try {
                                    auto jRes = nlohmann::json::parse(vib_res);
                                    std::string summary = jRes.value("summary", "");
                                    if (summary.find("INSUFFICIENT_DATA") == std::string::npos) {
                                        sensor_ready = true;
                                    }
                                } catch (...) {}
                            }
                        }
                        if (is_pitch_analysis && m_executor) {
                            std::string note_res = m_executor->getBlackboardValue("result_note_mapper");
                            if (!note_res.empty() && note_res.find("IN_TUNE") != std::string::npos) {
                                sensor_ready = true;
                            }
                        }
                        if (!sensor_ready && tuning_iteration < max_iters) {
                            LOGI(TAG, "L8 Scheduler: Skipping downstream step '%s' while sensor analysis is settling (iteration %d/%d)...", step.c_str(), tuning_iteration, max_iters);
                            continue;
                        }
                    }
                    if (tuning_iteration == 1 || step == "audio_capture" || step == "fft" || step == "detect_peaks" || step == "note_mapper") {
                        if (tuning_iteration == 1) Capability::HardwareBridge::pushMessage("[AGENT] Step: " + step);
                    }
                
                    auto exec_ctx = current_session->getExecutionContext();
                
                    // v7.7 Robust Step-to-Capability Mapping
                    CapabilityType type = CapabilityType::NONE;
                    std::string s_lower = step;
                    std::transform(s_lower.begin(), s_lower.end(), s_lower.begin(), ::tolower);
                
                    std::string i_lower = current_session->getIntent();
                    std::transform(i_lower.begin(), i_lower.end(), i_lower.begin(), ::tolower);

                    // v10.2.17: Force MAP trigger if intent is MAP
                    if (s_lower.find("audio") != std::string::npos || s_lower.find("fft") != std::string::npos || 
                        s_lower.find("peak") != std::string::npos || s_lower.find("note_mapper") != std::string::npos || 
                        s_lower.find("zero_crossing") != std::string::npos || s_lower.find("rms") != std::string::npos || 
                        s_lower.find("lowpass") != std::string::npos) {
                        type = CapabilityType::AUDIO;
                    }
                    else if (i_lower.find("map") != std::string::npos && (s_lower.find("location") != std::string::npos || s_lower.find("get") != std::string::npos)) {
                        type = CapabilityType::MAP;
                    }
                    else if (s_lower.find("map") != std::string::npos || s_lower.find("မြေပုံ") != std::string::npos)
                        type = CapabilityType::MAP;
                    else if (s_lower.find("mail") != std::string::npos || s_lower.find("email") != std::string::npos)
                        type = CapabilityType::MAIL;
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
                             s_lower.find("return_result") != std::string::npos || s_lower.find("return_content") != std::string::npos || 
                             s_lower.find("search_database") != std::string::npos || s_lower.find("check_vault") != std::string::npos)
                        type = CapabilityType::MEMORY;
                    else if (s_lower.find("alarm") != std::string::npos || s_lower.find("wake") != std::string::npos || s_lower.find("နှိုး") != std::string::npos)
                        type = CapabilityType::ALARM;
                    else if (s_lower.find("calendar") != std::string::npos || s_lower.find("event") != std::string::npos || s_lower.find("meeting") != std::string::npos || s_lower.find("check_calendar") != std::string::npos)
                        type = CapabilityType::CALENDAR;
                    else if (s_lower.find("file") != std::string::npos || s_lower.find("search_file") != std::string::npos || s_lower.find("find_file") != std::string::npos || s_lower.find("ဖိုင်") != std::string::npos)
                        type = CapabilityType::FILES;
                    else if (s_lower.find("sensor") != std::string::npos || s_lower.find("vibration") != std::string::npos || s_lower.find("resonance") != std::string::npos || s_lower.find("တုန်ခါမှု") != std::string::npos || 
                             s_lower.find("read_sensor_data") != std::string::npos || s_lower.find("read_vibration_data") != std::string::npos ||
                             s_lower.find("analyze_vibration") != std::string::npos)
                        type = CapabilityType::SENSOR;
                    else if (s_lower.find("mock_test") != std::string::npos)
                        type = CapabilityType::TEST;
                
                    std::string cap_str = Ronin::Kernel::CapabilityTypeToString(type);
                    if (exec_ctx) {
                        Execution::ExecutionTelemetryBus::getInstance().logNodeStart(exec_ctx->session_id, exec_ctx->execution_id, step, cap_str, exec_ctx->correlation_id);
                    }
                
                    if (type != CapabilityType::NONE) {
                        LOGI(TAG, "L8 Scheduler: Dispatching Capability %d to L10 Optimizer...", static_cast<int>(type));

                        Capability::HardwareBridge::updateDevHUD("EXECUTING", current_session->getIntent(), 1.0f, step);

                        // v9.2: Pass all session parameters to every step
                        nlohmann::json jParams;
                        for (const auto& [k, v] : current_session->getParameters()) jParams[k] = v;
                        jParams["action"] = step;
                        jParams["intent"] = current_session->getIntent(); // Pass intent context to Kotlin
                    
                        exec_ctx = current_session->getExecutionContext();
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
                                    Execution::ExecutionTelemetryBus::getInstance().logNodeEnd(
                                        exec_ctx->session_id, 
                                        exec_id, 
                                        step, 
                                        cap_str,
                                        cost, 
                                        step_success ? "SUCCESS" : "FAILURE", 
                                        cost, 
                                        exec_ctx->correlation_id, 
                                        step_success ? 0 : 500
                                    );
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

                if (!all_steps_success) break;

                if (is_pitch_analysis) {
                    std::string note_mapper_res = m_executor ? m_executor->getBlackboardValue("result_note_mapper") : "";
                    if (!note_mapper_res.empty()) {
                        try {
                            auto jRes = nlohmann::json::parse(note_mapper_res);
                            std::string status = jRes.value("status", "");
                            std::string nearest_str = jRes.value("nearest_string", "");
                            double freq = jRes.value("frequency_hz", 0.0);
                            double cents = jRes.value("nearest_cents", 0.0);
                            
                            if (status == "IN_TUNE") {
                                char buf[128];
                                snprintf(buf, sizeof(buf), "[AGENT] 🎸 Tuning Success! String %s (%.1fHz) is perfectly IN TUNE! 🎶 [BEEP]", nearest_str.c_str(), freq);
                                Capability::HardwareBridge::pushMessage(buf);
                                break; // Exit tuning loop!
                            } else {
                                char buf[128];
                                snprintf(buf, sizeof(buf), "[AGENT] Tuning %s: %.1fHz is %s (%.1f cents). Adjust peg...", nearest_str.c_str(), freq, status.c_str(), cents);
                                Capability::HardwareBridge::pushMessage(buf);
                            }
                        } catch (...) {}
                    }
                }

                if (is_vibration_analysis && m_executor) {
                    std::string vib_res = m_executor->getBlackboardValue("result_SENSOR");
                    if (vib_res.empty()) vib_res = m_executor->getBlackboardValue("result_analyze_vibration");
                    if (vib_res.empty()) vib_res = m_executor->getBlackboardValue("result_read_vibration_data");
                    if (!vib_res.empty()) {
                        try {
                            auto jRes = nlohmann::json::parse(vib_res);
                            double freq = jRes.value("resonance_freq_hz", 0.0);
                            double psd = jRes.value("psd_peak_db", -100.0);
                            bool anomaly = jRes.value("anomaly_detected", false);
                            std::string summary = jRes.value("summary", "");
                            bool is_insufficient = (summary.find("INSUFFICIENT_DATA") != std::string::npos);

                            double filtered_freq = jRes.value("filtered_resonance_freq_hz", freq);
                            double health_idx = jRes.value("health_index_pct", 100.0);
                            std::string risk_level = jRes.value("risk_level", "UNKNOWN");
                            bool structural_shift = jRes.value("structural_shift_detected", false);
                            double shift_delta = jRes.value("shift_delta_hz", 0.0);

                            if (!is_insufficient) {
                                m_executor->getBeliefState().updateBelief("world.env.resonance_freq_hz", std::to_string(freq), 0.95f);
                                m_executor->getBeliefState().updateBelief("world.env.psd_peak_db", std::to_string(psd), 0.90f);
                                m_executor->getBeliefState().updateBelief("world.env.anomaly_detected", anomaly ? "true" : "false", 1.0f);
                                m_executor->getBeliefState().updateBelief("world.shm.filtered_resonance_freq_hz", std::to_string(filtered_freq), 0.98f);
                                m_executor->getBeliefState().updateBelief("world.shm.health_index_pct", std::to_string(health_idx), 0.99f);
                                m_executor->getBeliefState().updateBelief("world.shm.risk_level", risk_level, 1.0f);
                                m_executor->getBeliefState().updateBelief("world.shm.structural_shift_detected", structural_shift ? "true" : "false", 1.0f);
                            }

                            char buf[300];
                            if (is_insufficient) {
                                std::string state = jRes.value("state", "STARTUP");
                                uint32_t samples = jRes.value("samples_processed", 0);
                                double std_dev = jRes.value("moving_std_dev", 0.0);
                                snprintf(buf, sizeof(buf), "[AGENT] ⏳ Sensor settling (%s). Collecting sample window %u/400 (std_dev=%.4f). Autonomous collection iteration #%d in progress...",
                                         state.c_str(), samples, std_dev, tuning_iteration);
                                Capability::HardwareBridge::pushMessage(buf);
                            } else {
                                if (structural_shift || risk_level == "CRITICAL") {
                                    snprintf(buf, sizeof(buf), "[AGENT] 🚨 CRITICAL STRUCTURAL SHIFT DETECTED! Risk: %s (Health Index: %.1f%%). Shift Delta: %.4fHz. Immediate inspection required!",
                                             risk_level.c_str(), health_idx, shift_delta);
                                } else if (risk_level == "DEGRADED" || anomaly) {
                                    snprintf(buf, sizeof(buf), "[AGENT] ⚠️ Vibration/SHM Warning Detected! Risk: %s (Health Index: %.1f%%). Filtered F0: %.2fHz (PSD: %.1fdB).",
                                             risk_level.c_str(), health_idx, filtered_freq, psd);
                                } else {
                                    snprintf(buf, sizeof(buf), "[AGENT] ✅ Structural Health Normal. Risk: %s (Health Index: %.1f%%). Filtered F0: %.4fHz (PSD: %.1fdB). Goal achieved.",
                                             risk_level.c_str(), health_idx, filtered_freq, psd);
                                }
                                Capability::HardwareBridge::pushMessage(buf);
                                m_executor->getReflectionEngine().reflectOnRecentTasks();
                                break; // Exit SHM collection loop upon stable data evaluation!
                            }
                        } catch (...) {}
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
                    
                    // Phase 6: Correlation context
                    auto exec_ctx = current_session->getExecutionContext();
                    if (exec_ctx) {
                        jPayload["session_id"] = exec_ctx->session_id;
                        jPayload["exec_id"] = exec_ctx->execution_id;
                        jPayload["corr_id"] = exec_ctx->correlation_id;
                    }

                    // v1.6 Phase 5: Embed execution sequence for Macro-Skill mining
                    nlohmann::json stepsArray = nlohmann::json::array();
                    for (const auto& s : current_session->getPlan()) stepsArray.push_back(s);
                    jPayload["executed_steps"] = stepsArray;
                    
                    std::string summary = "Successfully executed " + current_session->getIntent();
                    m_executor->recordEpisode(current_session->getIntent(), summary, jPayload.dump(), true);
                }
            } else {
                Capability::HardwareBridge::updateDevHUD("FAILED", current_session->getIntent(), 0.0f, "");
                if (m_executor) {
                    nlohmann::json jPayload;
                    auto exec_ctx = current_session->getExecutionContext();
                    if (exec_ctx) {
                        jPayload["session_id"] = exec_ctx->session_id;
                        jPayload["exec_id"] = exec_ctx->execution_id;
                        jPayload["corr_id"] = exec_ctx->correlation_id;
                    }
                    m_executor->recordEpisode(current_session->getIntent(), "Failed task: " + current_session->getIntent(), jPayload.dump(), false);
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
