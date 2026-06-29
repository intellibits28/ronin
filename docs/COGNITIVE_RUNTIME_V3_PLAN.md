# Ronin Cognitive Runtime v3.0: Implementation Plan

This document outlines the architecture and execution plan to upgrade the Ronin Kernel from a basic tool-calling router into a mature **Cognitive Runtime v3.0** based on the user's architectural suggestions.

---

## 1. Core Architectural Layout (Cognitive Runtime v3.0)

```
                            USER / SENSORS
                                  │
                                  ▼
                            Perception Engine (10Hz)
                                  │
                                  ▼
                         Belief-based World Model
                 (Cached Environment & Device Context)
                                  │
                                  ▼
                        Gemma 4 (Brain Engine)
                                  │
                       Goal / Intent Extraction
                                  │
                                  ▼
                     Capability Discovery Engine
                      (Resolves Capabilities)
                                  │
                                  ▼
                       Execution Graph Builder
                      (Builds topological DAG)
                                  │
                                  ▼
                         Graph Optimizer
                   (Latency, Power & Cache Checks)
                                  │
                                  ▼
                          Graph Executor
                                  │
                                  ▼
                    Structured Observation (JSON)
                                  │
                                  ▼
                      Reflection Loop (Reasoning)
                                  │
                                  ▼
                    Reflection Validation Pipeline
                (Observation -> Hypothesis -> Validation)
                                  │
                                  ▼
                         Long Term Memory (LTM)
                                  │
                                  ▼
                        Capability Compiler
                     (Compiles Macro Capabilities)
```

---

## 2. Refined Architectural Components

### A. Extended Tool Metadata
We enrich the `ToolMetadata` class in C++ with execution profiling fields. This allows the `GraphOptimizer` to rank paths using battery footprint and latency heuristics.

```cpp
struct ToolMetadata {
    std::string name;
    std::string description;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> required_permissions;
    
    // Cognitive Runtime v3.0 Extensions
    float average_latency_ms = 0.0f;
    float success_rate = 1.0f;
    float estimated_power_cost = 0.1f; // Scale 0.0 (negligible) to 1.0 (heavy GPS/CPU)
    bool streaming = false;
    bool deterministic = true;
    bool cacheable = true;
    std::vector<std::string> sensor_dependencies;
    float confidence = 1.0f;
};
```

### B. Graph Builder & Optimizer Separation
We split graph scheduling into dedicated stages:
1. **`CapabilityDiscoveryEngine`**: Matches requirements with candidate tools using semantic cosine/Jaccard similarity.
2. **`ExecutionGraphBuilder`**: Constructs the raw DAG dependency chain.
3. **`GraphOptimizer`**: Re-orders execution nodes, flags cache hits from the `WorldModel`, and prunes high-power components if lower-power paths or cached variables are available.

### C. Belief-Based World Model (Context Cache)
Instead of binary states, the `WorldModel` tracks state distributions with confidence values.

```json
{
  "walking": 0.91,
  "indoor": 0.77,
  "conversation": 0.62,
  "battery_charging": 1.0,
  "location_gps": { "coords": "16.8,96.1", "confidence": 0.95 }
}
```

* **Caching Rule**: The planner checks the World Model first. If the required variable (e.g. location) is cached and its timestamp is fresh, it bypasses GPS hardware execution entirely.

### D. Reflection Validation Pipeline
Prevents hallucinated or transient observations from polluting long-term memory:
1. **Observation**: Raw result parsed (e.g., "Guitar frequency detected").
2. **Reflection**: LLM makes an inference: `"User is playing guitar."`
3. **Hypothesis**: Insight is logged into a temporary `hypotheses` table.
4. **Validation**: The runtime monitors subsequent episodes. If the hypothesis is verified at least **3 times** across separate sessions, it is promoted to a permanent Fact in LTM.

### E. Capability Compiler (Rebranded & Re-engineered)
Promotes successful recurring toolchains into reusable compound Macro-Capabilities (e.g., `PitchAnalysis()`).

* **Promotion Heuristics**:
  - `Support Count >= 5`
  - `Success Rate >= 0.95`
  - `Execution Variance <= 200ms`
  - `User Correction Rate <= 0.05`

---

## 3. Staged Implementation Plan

### Stage 1: Extended Metadata & Graph Separation (C++)
* **Task 1.1**: Update `ToolMetadata` structure in `include/capabilities/tool_registry.h` and register all tools with their respective parameters.
* **Task 1.2**: Create `ExecutionGraphBuilder` and `GraphOptimizer` files. Move DAG construction out of `CapabilityDiscoveryEngine` and implement optimization scoring.

### Stage 2: Belief-based World Model (Kotlin & JNI)
* **Task 2.1**: Implement a structured `WorldModel` class in Kotlin/JNI keeping track of states with floating-point belief/confidence coefficients.
* **Task 2.2**: Integrate World Model queries inside `ChatSkill::execute` to intercept raw sensor calls.

### Stage 3: Reflection Validation Pipeline
* **Task 3.1**: Create the `hypotheses` SQLite schema.
* **Task 3.2**: Modify `ReflectionEngine` to write to hypotheses and validate counts before promoting to LTM.

### Stage 4: Rebranding to Capability Compiler
* **Task 4.1**: Rename `SkillCompiler` to `CapabilityCompiler` in C++ files.
* **Task 4.2**: Replace hardcoded 100-run counts with multi-dimensional heuristics (`Success Rate`, `Variance`).

---

## 4. Class Design & Header Previews

### 1. `GraphOptimizer` (`include/capabilities/graph_optimizer.h`)
```cpp
namespace Ronin::Kernel::Reasoning {

class GraphOptimizer {
public:
    static std::vector<std::string> optimize(
        const std::vector<std::string>& raw_dag, 
        const std::unordered_map<std::string, Capability::ToolMetadata>& registry_meta
    );
};

}
```

### 2. `WorldModel` (`include/capabilities/world_model.h`)
```cpp
namespace Ronin::Kernel::Capability {

struct BeliefStateValue {
    std::string value;
    float confidence = 1.0f;
    uint64_t timestamp = 0;
};

class WorldModel {
public:
    static WorldModel& getInstance();
    void updateBelief(const std::string& key, const std::string& value, float confidence);
    BeliefStateValue getBelief(const std::string& key);
    std::unordered_map<std::string, BeliefStateValue> getAllBeliefs();
    
private:
    std::unordered_map<std::string, BeliefStateValue> m_beliefs;
    std::mutex m_mutex;
};

}
```
