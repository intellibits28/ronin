#ifndef RONIN_TYPES_HPP
#define RONIN_TYPES_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Ronin::Kernel {

/**
 * Dynamic capability manifest entry.
 */
struct CapabilityEntry {
    uint32_t id;
    std::string name;
    std::vector<std::string> subjects;
    std::vector<std::string> actions;
    float confidence_threshold;
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
    std::unordered_map<std::string, std::string> data;
    
    void write(const std::string& key, const std::string& val) { data[key] = val; }
    std::string read(const std::string& key) const { 
        auto it = data.find(key);
        return (it != data.end()) ? it->second : "";
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
 * Execution states for the dynamic Task Planner.
 */
enum class AgentState : uint32_t {
    IDLE = 0,
    PLANNING = 1,
    RESOLVING_DEPENDENCIES = 2,
    WAITING_FOR_PERMISSION = 3,
    REQUIRES_CONFIRMATION = 4, // HITL Pause
    EXECUTING = 5,
    COMPLETED = 6,
    FAILED = 7
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
 * LLM-driven execution plan.
 */
struct AgentPlan {
    std::string intent_name;
    std::vector<Dependency> dependencies;
    std::vector<uint32_t> execution_chain;
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
  AgentState agent_state = AgentState::IDLE;
  AgentPlan current_plan;
};

} // namespace Ronin::Kernel

#endif // RONIN_TYPES_HPP
