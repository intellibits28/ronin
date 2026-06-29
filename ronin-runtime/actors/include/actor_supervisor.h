#pragma once

#include "actor.h"
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <iostream>

namespace Ronin::Kernel::Execution {

enum class SupervisionStrategy {
    ONE_FOR_ONE,
    ONE_FOR_ALL,
    STOP
};

/**
 * v1.0 Actor Supervisor monitoring lifecycles, crashes and enforcing OTP recovery strategies
 */
class ActorSupervisor {
public:
    static ActorSupervisor& getInstance() {
        static ActorSupervisor instance;
        return instance;
    }

    void registerActor(std::shared_ptr<Actor> actor, SupervisionStrategy strategy = SupervisionStrategy::ONE_FOR_ONE) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_actors[actor->getId()] = actor;
        m_strategies[actor->getId()] = strategy;
        actor->start();
    }

    void handleCrash(const std::string& actor_id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        auto it = m_actors.find(actor_id);
        if (it == m_actors.end()) return;

        SupervisionStrategy strategy = m_strategies[actor_id];
        
        std::cout << "[Supervisor] Actor crashed: " << actor_id << " (Enforcing strategy: " << static_cast<int>(strategy) << ")" << std::endl;

        if (strategy == SupervisionStrategy::ONE_FOR_ONE) {
            // Restart single crashed actor
            it->second->stop();
            it->second->start();
        } else if (strategy == SupervisionStrategy::ONE_FOR_ALL) {
            // Restart all registered actors to clear pipeline locks
            for (auto& [id, actor] : m_actors) {
                actor->stop();
            }
            for (auto& [id, actor] : m_actors) {
                actor->start();
            }
        } else if (strategy == SupervisionStrategy::STOP) {
            // Shut down actor cleanly
            it->second->stop();
            m_actors.erase(it);
        }
    }

    void shutdownAll() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [id, actor] : m_actors) {
            actor->stop();
        }
        m_actors.clear();
        m_strategies.clear();
    }

    ActorSupervisor() = default;
    ~ActorSupervisor() { shutdownAll(); }

private:
    std::unordered_map<std::string, std::shared_ptr<Actor>> m_actors;
    std::unordered_map<std::string, SupervisionStrategy> m_strategies;
    std::mutex m_mutex;
};

} // namespace Ronin::Kernel::Execution
