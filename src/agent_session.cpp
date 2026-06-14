#include "agent_session.h"

namespace Ronin::Kernel {

AgentSession::AgentSession(const std::string& session_id, const std::string& intent)
    : m_session_id(session_id), m_intent(intent), m_state(AgentState::PLANNING), m_cancel_token(std::make_shared<CancellationToken>()) {}

void AgentSession::setParameter(const std::string& key, const std::string& value) {
    m_parameters[key] = value;
}

std::string AgentSession::getParameter(const std::string& key) const {
    auto it = m_parameters.find(key);
    return (it != m_parameters.end()) ? it->second : "";
}

void AgentSession::setPlan(const std::vector<std::string>& steps) {
    m_plan_steps = steps;
    m_current_step_idx = 0;
}

} // namespace Ronin::Kernel
