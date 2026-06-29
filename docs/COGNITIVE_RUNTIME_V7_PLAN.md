# Ronin Cognitive Runtime v7.0: Decoupled Cognitive Microkernel Architecture

This document outlines the system architecture for **Ronin Cognitive OS (v7.0)**, transitioning the core into a decoupled, actor-based cognitive microkernel suitable for production deployment.

---

## 1. Unified Operating Architecture (v7.0)

```
                            PHYSICAL SENSORS
                       (Mic, IMU, GPS, Bluetooth)
                                   │
                                   ▼
                       Shared Sensor Stream Bus
                                   │
                                   ▼
                     Priority-Based Event Bus
                (Critical, High, Normal, Low Queues)
                                   │
                                   ▼
                           Attention Manager   <──>   Resource Manager
                    (Importance, Novelty, Urgency)  (CPU, RAM, DSP Buffers)
                                   │
                                   ▼
                           Task Decomposer
                                   │
                                   ▼
                            Plan Engine        <──>   Planner Rule Cache
                                   │
                                   ▼
                             Policy Engine
                  (Enforces Perm, Privacy, Limits)
                                   │
                                   ▼
                           Execution Context
                     (Shared Across Actors & Runs)
                                   │
                                   ▼
                           Actor Framework
                (Sensor, Memory, Planner, Llm Actors)
                                   │
                                   ▼
                             Goal Monitor
                    (Closed-Loop Goal Validation)
                                   │
                                   ▼
                         Reflection Engine
                                   │
                                   ▼
                          Meta-Cognition Layer
                    (Planner Feedback Loop & Wastes)
                                   │
                                   ▼
             Knowledge Graph, Episode Graph & Semantic Index
                                   │
                                   ▼
                          Capability Compiler
                   (Graph Rewrite & Rule Compilation)
```

---

## 2. Deep Microkernel Core Specifications (v7.0)

### A. Modular Reasoning Interface (`IReasoningEngine`)
To utilize specialized models for distinct cognitive tasks, we decompose the interface into atomic services.

```cpp
namespace Ronin::Kernel::Reasoning {

struct CognitiveContext {
    std::string goal;
    std::string active_state;
    std::vector<std::string> environmental_context;
};

struct DecomposedSubtasks {
    std::vector<std::string> subtasks;
    float confidence = 1.0f;
};

struct ExecutionGraphPlan {
    std::vector<std::string> steps;
    std::unordered_map<std::string, std::string> parameters;
};

class IReasoningEngine {
public:
    virtual ~IReasoningEngine() = default;
    
    virtual DecomposedSubtasks decompose(const std::string& goal, const CognitiveContext& ctx) = 0;
    virtual ExecutionGraphPlan reason(const std::string& subtask, const CognitiveContext& ctx) = 0;
    virtual std::string reflect(const std::string& observation, const std::string& hypothesis) = 0;
    virtual std::string summarize(const std::string& long_context) = 0;
};

} // namespace Ronin::Kernel::Reasoning
```

### B. Shared Execution Context (`ExecutionContext`)
Every graph execution runs within a shared, immutable-state-thread context. All Actors executing tasks within the pipeline reference this context.

```cpp
namespace Ronin::Kernel::Execution {

struct ExecutionContext {
    std::string goal_id;
    std::string session_id;
    uint64_t deadline_timestamp = 0;
    float battery_budget = 1.0f;
    std::vector<std::string> authorized_permissions;
    std::unordered_map<std::string, std::string> world_snapshot;
    std::string reasoning_engine_id;
    uint32_t max_retry_count = 3;
    uint32_t current_retry_count = 0;
    std::string parent_goal_id;
};

} // namespace Ronin::Kernel::Execution
```

### C. Policy Engine (Gateway)
Positioned between the Plan Engine and the Execution context, the Policy Engine evaluates the generated plan against hardcoded constraints:
* **Permissions Check**: Assures requested capabilities have valid user grants.
* **Privacy Check**: Bypasses cloud endpoints if sensitive attributes (e.g. secure tokens, vault items) are queried.
* **Rate Limits**: Throttles active external connections.
* **HITL Filter**: Raises user confirmation requests for high-risk actions.

### D. Priority-Based Event Bus
The event bus maintains four prioritized queues:
1. **Critical**: Real-time threats, hardware exceptions, safety events (e.g. Earthquake, High Thermal).
2. **High**: Battery alerts, incoming user interface queries, location state boundaries.
3. **Normal**: Generic sensor readings, background SQLite transaction updates.
4. **Low**: Clean-up garbage routines, history compression runs.

### E. Semantic Index over Parallel Graphs
To link relational semantic facts (Knowledge Graph) with chronological traces (Episode Graph), we maintain an FTS (Full-Text Search) and vector **Semantic Index**. A query like `"last time I tuned my guitar"` traverses the index to instantly fetch the targeted episode ID and mapping graph.

---

## 3. Staged Implementation Roadmap (v7.0)

### Phase 1: Context & Policy Guard (C++ Core)
* Implement the `ExecutionContext` structural wrapping.
* Create the `PolicyEngine` gateway, wiring permission verification layers.

### Phase 2: Priority-Based Event Bus & Scheduler Actors
* Implement priority queuing inside the `InternalEventBus`.
* Spawn lifecycled `SensorActor`, `MemoryActor`, `PlannerActor`, and `LlmActor`.

### Phase 3: Planner Rule Cache & Compiler Rewrite
* Write the planner rule cache matcher database engine.
* Integrate node collapsing algorithms to bypass the reasoning engines for recurrent flows.

### Phase 4: Semantic Graph Linkage
* Implement the FTS Semantic Index bridging the parallel Knowledge/Episode graphs.
* Write context traversal logic inside SQLite memory modules.
