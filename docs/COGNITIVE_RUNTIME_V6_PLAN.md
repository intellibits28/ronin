# Ronin Cognitive Runtime v6.0: Model-Agnostic Cognitive Operating System

This document details the architectural specifications for **Ronin Cognitive OS (v6.0)**, transitioning the core into a model-agnostic cognitive operating system with split planning engines, parallel memory graphs, system actors, and meta-cognition controllers.

---

## 1. Unified Operating Architecture (v6.0)

```
                            PHYSICAL SENSORS
                       (Mic, IMU, GPS, Bluetooth)
                                   │
                                   ▼
                       Shared Sensor Stream Bus
                                   │
                                   ▼
                           Internal Event Bus
                 (System, Sensor, Goal, Memory Events)
                                   │
                                   ▼
                           Attention Manager
               (Importance, Novelty, Urgency, Budget)
                                   │
                                   ▼
                           Task Decomposer
                 (Decomposes Goals to Subtasks)
                                   │
                                   ▼
                            Plan Engine
                 (Resolves Capabilities & Builds DAG)
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
                    (Optimal Plan & Waste Profiling)
                                   │
                                   ▼
                   Knowledge Graph  &  Episode Graph
               (Semantic Facts)      (Chronological Run Log)
                                   │
                                   ▼
                          Capability Compiler
                       (Graph Rewrite Engine)
```

---

## 2. Deep Core Specifications (v6.0)

### A. Model-Agnostic Reasoning Engine (`IReasoningEngine`)
The reasoning layer is fully interface-driven, allowing developers to plug in different local or cloud-based neural models without modifying the operating system scheduler.

```cpp
namespace Ronin::Kernel::Reasoning {

struct CognitiveContext {
    std::string goal;
    std::string active_state;
    std::vector<std::string> environmental_context;
};

struct ReasoningResult {
    std::string intent;
    std::vector<std::string> decomposed_steps;
    std::unordered_map<std::string, std::string> parameters;
    float confidence = 1.0f;
};

class IReasoningEngine {
public:
    virtual ~IReasoningEngine() = default;
    virtual ReasoningResult reason(const CognitiveContext& ctx) = 0;
};

// Plugins implementing the interface
class LocalGemmaEngine : public IReasoningEngine {
public:
    ReasoningResult reason(const CognitiveContext& ctx) override;
};

class CloudOpenAiEngine : public IReasoningEngine {
public:
    ReasoningResult reason(const CognitiveContext& ctx) override;
};

} // namespace Ronin::Kernel::Reasoning
```

### B. Parallel Graph Memory: Knowledge Graph & Episode Graph
1. **Knowledge Graph**: Holds static relationships and world facts (`[fan] --(produces)--> [120Hz]`).
2. **Episode Graph**: Holds sequential chronological trace execution records (`[Episode_421] --(used)--> [PitchAnalysis] --(result)--> [440Hz] --(user_reaction)--> [Accepted]`).

The Capability Compiler crawls the **Episode Graph** to discover recurrent patterns and compiles them into Macro-Capabilities.

### C. System Actors & Lifecycles
All system routines run within asynchronous **Actor threads** managing their own state lifecycles:
* `SensorActor`: Manages stream capture and hardware buffers.
* `MemoryActor`: Handles Knowledge Graph and Episode Graph write/read transactions.
* `PlannerActor`: Orchestrates goal decomposition and graph builder runs.
* `ReflectionActor`: Evaluates facts, explanations, and hypothesis validation.
* `LearningActor`: Compiles, rewrites, and registers new capabilities.

### D. Attention & Cognitive Relevance
The **Attention Manager** computes execution relevance weights:

$$\text{Relevance} = \alpha \cdot \text{Importance} + \beta \cdot \text{Novelty} + \gamma \cdot \text{Urgency} + \delta \cdot \text{Goal\_Relevance}$$

Only actors with a relevance score exceeding the system threshold under current battery constraint mode are authorized to capture hardware cycles.

### E. Goal Monitor & Meta-Cognition Loop
* **Goal Monitor**: Tracks runtime compliance. If step execution yields error values, the monitor triggers the Task Decomposer to formulate a new recovery sequence.
* **Meta-Cognition**: Sits above reflection to evaluate OS performance. It asks:
  * *Was this graph execution optimal?*
  * *How much battery and CPU cycles were wasted?*
  * *Does this recurring sequence warrant Capability Compilation?*

---

## 3. Staged Implementation Roadmap (v6.0)

### Phase 1: Engine Abstraction & PLugins (C++ Core)
* Create the `IReasoningEngine` interface.
* Move local Gemma calls behind `LocalGemmaEngine` implementation.

### Phase 2: Internal Event Bus & System Actors
* Implement the centralized `InternalEventBus` supporting publish/subscribe topologies.
* Refactor base scheduler to spawn the lifecycle-managed `SensorActor`, `MemoryActor`, and `PlannerActor`.

### Phase 3: Episode Graph Database Integration
* Create the `episodes_graph` SQLite schema schema.
* Set up edge relations mapping transaction histories.

### Phase 4: Meta-Cognition Profiling
* Integrate execution time and battery depletion listeners.
* Implement plan scoring algorithms under `MetaCognitionController`.
