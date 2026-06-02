#pragma once

#include "agent_session.h"
#include <unordered_map>
#include <memory>
#include <mutex>

namespace Ronin::Kernel {

/**
 * v7.0 Layer 7: Central registry and life-cycle manager for agent sessions.
 */
class SessionManager {
public:
    static SessionManager& getInstance();

    // Creates a new session for a user intent
    std::shared_ptr<AgentSession> createSession(const std::string& intent);

    // Retrieves an existing session
    std::shared_ptr<AgentSession> getSession(const std::string& session_id);

    // Terminates and removes a session
    void terminateSession(const std::string& session_id);

    // Checks for active sessions
    bool hasActiveSessions() const;

private:
    SessionManager() = default;
    
    std::mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<AgentSession>> m_sessions;
};

} // namespace Ronin::Kernel
