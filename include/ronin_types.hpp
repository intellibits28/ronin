#ifndef RONIN_TYPES_HPP
#define RONIN_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <memory>
#include <stdexcept>

namespace Ronin::Kernel {

// v10.6: Cooperative Cancellation Primitive
class CancellationToken {
public:
    void cancel() { m_cancelled.store(true, std::memory_order_release); }
    bool isCancelled() const { return m_cancelled.load(std::memory_order_acquire); }
private:
    std::atomic<bool> m_cancelled{false};
};

using CancellationTokenPtr = std::shared_ptr<CancellationToken>;

// v10.6: Hardened, Exception-Free Result Type
template<typename T>
class KernelResult {
public:
    static KernelResult Success(T val) { return KernelResult(val, 0); }
    static KernelResult Error(int code, std::string msg) { return KernelResult(T(), code, msg); }

    bool isOk() const { return m_code == 0; }
    
    // Caller MUST check isOk() before accessing. No throw, safe for NDK.
    T value() const { return m_value; }
    std::string error() const { return m_msg; }

private:
    T m_value;
    int m_code;
    std::string m_msg;
    KernelResult(T v, int c, std::string m = "") : m_value(v), m_code(c), m_msg(m) {}
};

/**
 * Dynamic capability manifest entry.
 */
struct CapabilityEntry {
    uint32_t id;
    std::string name;
    std::vector<std::string> subjects;
    std::vector<std::string> actions;
    float confidence_threshold = 0.5f;
    // v1.0 Production Manifest Specification Fields
    uint32_t schema_version = 1;
    std::string capability_version = "1.0.0";
    std::vector<std::string> dependencies;
    std::vector<std::string> permissions;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::string estimated_power_cost = "LOW";
    bool deterministic = true;
    bool streaming = false;
};

/**
 * Minimalist input container with a fixed-size buffer to prevent heap
 * fragmentation.
 */
struct Input {
  char data[512];
  size_t length;
};

/**
 * Adaptive Intent Categories for v6.0 Agent Mode.
 */
enum class IntentCategory : uint32_t {
    UNKNOWN = 0,
    CHAT_QUERY = 1,     // General conversation (Gemma 4 only)
    TOOL_QUERY = 2,     // Hardware/System tool execution
    MEMORY_QUERY = 3,   // Recall from Long-term memory
    FACT_QUERY = 4,     // World knowledge/fact checking
    AGENT_PLAN = 5      // Multi-step complex task
};

/**
 * Represent a discrete user intent derived from the reasoning spine.
 */
struct CognitiveIntent {
  uint32_t id;
  float confidence;
  bool intent_param; 
  IntentCategory category; // Added for v6.0 adaptive routing
};

/**
 * v7.0 Blackboard: Shared context for tool-to-tool communication.
 */
struct ToolContext {
    std::unordered_map<std::string, std::string> storage;
    
    void write(const std::string& key, const std::string& val) { storage[key] = val; }
    std::string read(const std::string& key) const { 
        auto it = storage.find(key);
        return (it != storage.end()) ? it->second : "";
    }
};

/**
 * Result of a capability node execution.
 */
struct Result {
  bool success;
  int32_t statusCode;
  std::string payload; // Added for tool chain output
};

// --- v7.0 Agent Mode Data Structures ---

/**
 * v7.0 Execution states for the dynamic Task Planner.
 */
enum class AgentState : uint32_t {
    CHAT = 0,
    PLANNING = 1,
    ASK_CONFIRMATION = 2,
    WAITING_PERMISSION = 3,
    EXECUTE = 4,
    COMPLETED = 5,
    FAILED = 6
};

// v10.7: Failure Classifications for Self-Healing
enum class FailureType {
    NONE = 0,
    TIMEOUT = 1,
    JNI_EXCEPTION = 2,
    BUDGET_EXCEEDED = 3,
    CYCLE_DETECTED = 4,
    NATIVE_CRASH = 5,
    UNKNOWN = 6,
    
    // v1.6 Behavioral Evolution (Semantic Failures)
    USER_REJECTED = 10,     // User explicitly canceled or disliked the result
    HITL_DENIED = 11,       // User denied permission during confirmation dialog
    CONTEXT_MISMATCH = 12,  // Tool execution succeeded but result was contextually wrong
    LOGICAL_MISMATCH = 13   // Agent reasoning path was flawed according to user
};

struct FailureRecord {
    std::string node_id;
    FailureType type;
    uint64_t timestamp;
    int retry_count;
    std::string resolution;
};

/**
 * Represents a required data point or condition for a task.
 */
struct Dependency {
    std::string name;
    bool is_fulfilled;
    std::string value;
};

/**
 * v7.2 Advanced LLM-driven execution plan.
 * Separates tools, permissions, and sequential steps for robust orchestration.
 */
struct AgentPlan {
    std::string intent_name;
    std::vector<std::string> required_tools;
    std::vector<std::string> required_permissions;
    std::vector<std::string> plan_steps;
    std::unordered_map<std::string, std::string> parameters;
    std::string raw_json;
};

/**
 * Encapsulates the internal state of the kernel for a single tick cycle.
 */
struct CognitiveState {
  CognitiveIntent currentIntent;
  uint32_t activeNodeId;
  bool requiresAction;
  int iterations;
  
  // v7.0 Agent Mode Fields
  AgentState agent_state = AgentState::CHAT;
  AgentPlan current_plan;
};

/**
 * v13.0 World State: Represents the current physical environment understanding.
 */
struct WorldState {
    float battery_percent;
    float ram_available_mb;
    bool gps_available;
    bool network_available;
    bool charging;
    uint64_t timestamp;
    
    // v1.6 Phase 4: Context-Aware Execution
    int hour_of_day; // 0-23
    std::string location_context; // e.g., "HOME", "WORK", "TRANSIT", "UNKNOWN"
};

/**
 * v13.0 Belief: Represents current confidence-weighted assumptions.
 */
struct Belief {
    std::string key;
    std::string value;
    float confidence;
    uint64_t updated_at;
};

} // namespace Ronin::Kernel

#endif // RONIN_TYPES_HPP
