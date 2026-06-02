#pragma once

#include <string>
#include <vector>
#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include "agent_session.h"

namespace Ronin::Kernel {

/**
 * v7.0 Layer 8: Orchestrates task execution based on priority.
 */
struct AgentTask {
    uint32_t priority;
    std::shared_ptr<AgentSession> session;

    // For priority queue (higher priority first)
    bool operator<(const AgentTask& other) const {
        return priority < other.priority;
    }
};

namespace Reasoning {
    class GraphExecutor;
}

class AgentScheduler {
public:
    static AgentScheduler& getInstance();

    // Sets the workflow optimizer (Layer 10)
    void setExecutor(Reasoning::GraphExecutor* executor) { m_executor = executor; }

    // Adds a session to the execution queue
    void schedule(std::shared_ptr<AgentSession> session, uint32_t priority = 5);

    // Starts the background worker thread
    void start();

    // Stops the background worker thread
    void stop();

private:
    AgentScheduler();
    ~AgentScheduler();

    void workerLoop();

    std::priority_queue<AgentTask> m_task_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    
    std::thread m_worker;
    std::atomic<bool> m_running{false};
    Reasoning::GraphExecutor* m_executor = nullptr;
};

} // namespace Ronin::Kernel
