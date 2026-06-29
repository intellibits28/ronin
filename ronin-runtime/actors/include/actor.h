#pragma once

#include "kernel/include/event_bus.h"
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

namespace Ronin::Kernel::Execution {

/**
 * v1.0 Base Actor implementation utilizing asynchronous message passing threads
 */
class Actor {
public:
    explicit Actor(std::string id) : m_id(std::move(id)), m_running(false) {}
    virtual ~Actor() { stop(); }

    std::string getId() const { return m_id; }

    void postMessage(const Event::Message& msg) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_inbox.push(msg);
        m_cv.notify_one();
    }

    void start() {
        if (m_running.exchange(true)) return;
        m_thread = std::thread(&Actor::actorLoop, this);
    }

    void stop() {
        if (!m_running.exchange(false)) return;
        m_cv.notify_all();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

protected:
    virtual void processMessage(const Event::Message& msg) = 0;
    virtual void onStart() {}
    virtual void onStop() {}

private:
    void actorLoop() {
        onStart();
        while (m_running) {
            Event::Message msg;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this]() {
                    return !m_running || !m_inbox.empty();
                });

                if (!m_running) break;

                msg = m_inbox.front();
                m_inbox.pop();
            }
            try {
                processMessage(msg);
            } catch (...) {
                // Caught exception: Let Supervisor handle recovery
            }
        }
        onStop();
    }

    std::string m_id;
    std::atomic<bool> m_running;
    std::thread m_thread;

    std::queue<Event::Message> m_inbox;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};

} // namespace Ronin::Kernel::Execution
