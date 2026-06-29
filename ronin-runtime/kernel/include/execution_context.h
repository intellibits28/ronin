#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <shared_mutex>
#include <nlohmann/json.hpp>

namespace Ronin::Kernel::Execution {

/**
 * v1.0 Strongly Typed Blackboard Value Variant
 */
using BlackboardValue = std::variant<
    int,
    float,
    bool,
    std::string,
    std::vector<float>,
    nlohmann::json
>;

/**
 * v1.0 Execution Context carrying transaction credentials and state constraints
 */
struct ExecutionContext {
    std::string goal_id;
    std::string session_id;
    std::string trace_id;
    uint64_t deadline_timestamp = 0;
    float battery_budget = 1.0f;
    std::vector<std::string> authorized_permissions;
    std::string reasoning_engine_id;
    uint32_t max_retry_count = 3;
    uint32_t current_retry_count = 0;
    std::string parent_goal_id;
};

/**
 * v1.0 Blackboard Memory mapping strongly typed values with strict write ownership rules
 */
class Blackboard {
public:
    Blackboard() = default;
    ~Blackboard() = default;

    // Disallow copy
    Blackboard(const Blackboard&) = delete;
    Blackboard& operator=(const Blackboard&) = delete;

    void write(const std::string& key, const BlackboardValue& val, const std::string& actor_id) {
        std::unique_lock<std::shared_mutex> lock(m_rw_mutex);
        
        // Ownership enforcement rule
        auto it = m_ownership.find(key);
        if (it != m_ownership.end() && it->second != actor_id) {
            // Write rejected: Actor does not own this key
            return;
        }
        
        if (it == m_ownership.end()) {
            m_ownership[key] = actor_id; // Set initial owner
        }
        
        m_board[key] = val;
    }

    BlackboardValue read(const std::string& key) const {
        std::shared_lock<std::shared_mutex> lock(m_rw_mutex);
        auto it = m_board.find(key);
        if (it != m_board.end()) {
            return it->second;
        }
        return std::string(""); // default fallback
    }

    bool contains(const std::string& key) const {
        std::shared_lock<std::shared_mutex> lock(m_rw_mutex);
        return m_board.find(key) != m_board.end();
    }

    void clear() {
        std::unique_lock<std::shared_mutex> lock(m_rw_mutex);
        m_board.clear();
        m_ownership.clear();
    }

private:
    std::unordered_map<std::string, BlackboardValue> m_board;
    std::unordered_map<std::string, std::string> m_ownership; // key -> owner_actor_id
    mutable std::shared_mutex m_rw_mutex;
};

} // namespace Ronin::Kernel::Execution
