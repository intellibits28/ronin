#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "ronin_types.hpp"
#include "capability_request.h"

namespace Ronin::Kernel {

/**
 * v7.0 Layer 7: Represents a single instance of an agentic task.
 */
class AgentSession {
public:
    enum class Error { NONE, TIMEOUT, CYCLE_DETECTED, DEPTH_EXCEEDED, QUEUE_FULL, SYSTEM_FAULT };

    AgentSession(const std::string& session_id, const std::string& intent);

    std::string getSessionId() const { return m_session_id; }
    AgentState getState() const { return m_state; }
    void setState(AgentState state) { m_state = state; }

    std::string getIntent() const { return m_intent; }
    
    // Parameter management
    void setParameter(const std::string& key, const std::string& value);
    std::string getParameter(const std::string& key) const;
    const std::unordered_map<std::string, std::string>& getParameters() const { return m_parameters; }

    // Plan steps management
    void setPlan(const std::vector<std::string>& steps);
    const std::vector<std::string>& getPlan() const { return m_plan_steps; }
    size_t getCurrentStepIndex() const { return m_current_step_idx; }
    void advanceStep() { m_current_step_idx++; }

    // v10.6: Global execution budget
    bool consumeBudget(uint32_t cost) {
        if (m_total_budget_ms < cost) return false;
        m_total_budget_ms -= cost;
        return true;
    }

    CancellationTokenPtr getToken() { return m_cancel_token; }

    void abortSession(Error err) {
        m_cancel_token->cancel(); // Signals running nodes to stop
        m_fatal_error = err;
        m_state = AgentState::FAILED;
    }
    
    Error getFatalError() const { return m_fatal_error; }

private:
    std::string m_session_id;
    std::string m_intent;
    AgentState m_state = AgentState::CHAT;
    
    std::unordered_map<std::string, std::string> m_parameters;
    std::vector<std::string> m_plan_steps;
    size_t m_current_step_idx = 0;

    CancellationTokenPtr m_cancel_token;
    uint32_t m_total_budget_ms = 15000; // Hard cap: 15s total per session
    Error m_fatal_error = Error::NONE;
};

} // namespace Ronin::Kernel
