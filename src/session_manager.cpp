#include "session_manager.h"
#include <random>
#include <sstream>
#include <iomanip>

namespace Ronin::Kernel {

static std::string generate_uuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 8; i++) ss << dis(gen);
    return ss.str();
}

SessionManager& SessionManager::getInstance() {
    static SessionManager instance;
    return instance;
}

std::shared_ptr<AgentSession> SessionManager::createSession(const std::string& intent) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string session_id = generate_uuid();
    auto session = std::make_shared<AgentSession>(session_id, intent);
    m_sessions[session_id] = session;
    return session;
}

std::shared_ptr<AgentSession> SessionManager::getSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sessions.find(session_id);
    return (it != m_sessions.end()) ? it->second : nullptr;
}

void SessionManager::terminateSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessions.erase(session_id);
}

bool SessionManager::hasActiveSessions() const {
    return !m_sessions.empty();
}

} // namespace Ronin::Kernel
