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
    AgentSession(const std::string& session_id, const std::string& intent);

    std::string getSessionId() const { return m_session_id; }
    AgentState getState() const { return m_state; }
    void setState(AgentState state) { m_state = state; }

    std::string getIntent() const { return m_intent; }
    
    // Parameter management
    void setParameter(const std::string& key, const std::string& value);
    std::string getParameter(const std::string& key) const;

    // Plan steps management
    void setPlan(const std::vector<std::string>& steps);
    const std::vector<std::string>& getPlan() const { return m_plan_steps; }
    size_t getCurrentStepIndex() const { return m_current_step_idx; }
    void advanceStep() { m_current_step_idx++; }

private:
    std::string m_session_id;
    std::string m_intent;
    AgentState m_state = AgentState::IDLE;
    
    std::unordered_map<std::string, std::string> m_parameters;
    std::vector<std::string> m_plan_steps;
    size_t m_current_step_idx = 0;
};

} // namespace Ronin::Kernel
