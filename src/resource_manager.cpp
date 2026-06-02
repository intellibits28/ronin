#include "resource_manager.h"

namespace Ronin::Kernel {

ResourceManager& ResourceManager::getInstance() {
    static ResourceManager instance;
    return instance;
}

bool ResourceManager::acquireResource(CapabilityType resource, const std::string& session_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_locks.find(resource);
    if (it == m_locks.end() || it->second == session_id) {
        m_locks[resource] = session_id;
        return true;
    }
    return false;
}

void ResourceManager::releaseResource(CapabilityType resource, const std::string& session_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_locks.find(resource);
    if (it != m_locks.end() && it->second == session_id) {
        m_locks.erase(it);
    }
}

bool ResourceManager::isAvailable(CapabilityType resource) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_locks.find(resource) == m_locks.end();
}

} // namespace Ronin::Kernel
