#pragma once

#include <string>
#include <vector>
#include <variant>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>

namespace Ronin::Kernel::Event {

enum class EventPriority {
    CRITICAL = 0,
    HIGH = 1,
    NORMAL = 2,
    LOW = 3
};

// Strongly Typed Payload Structs
struct FFTRequest { std::vector<float> signal; };
struct LocationRequest { bool high_accuracy; };
struct AudioChunk { std::vector<float> samples; };
struct SensorEvent { std::string type; float value; };

using MessagePayload = std::variant<
    FFTRequest,
    LocationRequest,
    AudioChunk,
    SensorEvent,
    std::string
>;

struct Message {
    std::string trace_id;
    std::string sender_id;
    std::string recipient_id;
    EventPriority priority = EventPriority::NORMAL;
    MessagePayload payload;
};

class EventBus {
public:
    static EventBus& getInstance() {
        static EventBus instance;
        return instance;
    }

    using SubscriberCallback = std::function<void(const Message&)>;

    void subscribe(const std::string& event_type, SubscriberCallback callback) {
        std::lock_guard<std::mutex> lock(m_sub_mutex);
        m_subscribers[event_type].push_back(callback);
    }

    void publish(const Message& msg, const std::string& event_type) {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        
        // Coalescing Rule: Merge consecutive high-frequency sensor updates
        if (event_type == "sensor_update" && !m_queues[static_cast<int>(msg.priority)].empty()) {
            auto& last_msg = m_queues[static_cast<int>(msg.priority)].back();
            if (last_msg.sender_id == msg.sender_id) {
                // Coalesce: Override with latest reading to prevent backpressure bloat
                last_msg = msg;
                return;
            }
        }

        // Backpressure Check: Drop low priority event if queue limit is exceeded
        if (m_queues[static_cast<int>(msg.priority)].size() >= MAX_QUEUE_SIZE) {
            if (msg.priority == EventPriority::LOW) {
                return; // Drop LOW priority event
            }
        }

        m_queues[static_cast<int>(msg.priority)].push(msg);
        m_cv.notify_one();
    }

    void start() {
        if (m_running.exchange(true)) return;
        m_dispatch_thread = std::thread(&EventBus::dispatchLoop, this);
    }

    void stop() {
        if (!m_running.exchange(false)) return;
        m_cv.notify_all();
        if (m_dispatch_thread.joinable()) {
            m_dispatch_thread.join();
        }
    }

private:
    EventBus() : m_running(false) {}
    ~EventBus() { stop(); }

    void dispatchLoop() {
        while (m_running) {
            Message msg;
            std::string event_type = "default";
            
            {
                std::unique_lock<std::mutex> lock(m_queue_mutex);
                m_cv.wait(lock, [this]() {
                    return !m_running || 
                           !m_queues[0].empty() || 
                           !m_queues[1].empty() || 
                           !m_queues[2].empty() || 
                           !m_queues[3].empty();
                });

                if (!m_running) break;

                // Priority Dispatch Routing
                for (int i = 0; i < 4; ++i) {
                    if (!m_queues[i].empty()) {
                        msg = m_queues[i].front();
                        m_queues[i].pop();
                        
                        // Extract category event mapping
                        if (std::holds_alternative<SensorEvent>(msg.payload)) {
                            event_type = "sensor_update";
                        } else if (std::holds_alternative<AudioChunk>(msg.payload)) {
                            event_type = "audio_stream";
                        } else {
                            event_type = "default";
                        }
                        break;
                    }
                }
            }

            // Distribute message to subscribers
            std::vector<SubscriberCallback> targets;
            {
                std::lock_guard<std::mutex> lock(m_sub_mutex);
                auto it = m_subscribers.find(event_type);
                if (it != m_subscribers.end()) {
                    targets = it->second;
                }
            }

            for (const auto& cb : targets) {
                cb(msg);
            }
        }
    }

    static constexpr size_t MAX_QUEUE_SIZE = 1000;
    std::atomic<bool> m_running;
    std::thread m_dispatch_thread;

    std::queue<Message> m_queues[4]; // 0: CRITICAL, 1: HIGH, 2: NORMAL, 3: LOW
    std::mutex m_queue_mutex;
    std::condition_variable m_cv;

    std::unordered_map<std::string, std::vector<SubscriberCallback>> m_subscribers;
    std::mutex m_sub_mutex;
};

} // namespace Ronin::Kernel::Event
