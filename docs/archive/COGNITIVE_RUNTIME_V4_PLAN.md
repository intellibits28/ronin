# Ronin Cognitive Runtime v4.0: Cognitive Operating Core Architecture

This document establishes the architectural blueprint for upgrading the Ronin Kernel from a tool-calling framework into an **Event-Driven Cognitive Operating Runtime (v4.0)**. 

---

## 1. Unified System Architecture (Cognitive Runtime v4.0)

```
                            PHYSICAL SENSORS
                       (Mic, IMU, GPS, Bluetooth)
                                   │
                                   ▼
                       Shared Sensor Stream Bus
                      (Hardware Reuse & Multiplex)
                                   │
                                   ▼
                         DSP Feature Pipeline
                       (FFT, Filters, Windows)
                                   │
                                   ▼
                         Perception Engine
                                   │
                                   ▼
                           World Model Graph
                 (Entity-Relation Context Graph)
                                   │
                                   ▼
                        Gemma 4 (Brain Engine)
                                   │
                       Goal / Intent Extraction
                                   │
                                   ▼
                      Capability Discovery Pipeline
                     (Type, Perm, Availability Filters)
                                   │
                                   ▼
                       Execution Graph Builder
                                   │
                                   ▼
                            Graph Optimizer
                       (Weighted Multi-Objective)
                                   │
                                   ▼
                         Runtime Scheduler
                   (Priority, Concurrency, Battery)
                                   │
                                   ▼
                            Graph Executor
                                   │
                                   ▼
                       Reflection Engine & Validation
                  (Observation -> Hypothesis -> Fact)
                                   │
                                   ▼
                          Capability Compiler
                       (Collapsing Graph Nodes)
```

---

## 2. Deep Architectural Upgrades

### A. Graph-Based World Model (Entity Graph)
Instead of a simple flat Key-Value store, the World Model is represented as a **Directed Multigraph** mapping entity relationships.

```
       [User] ──(Holding)──> [Phone]
          │                     │
      (InSession)          (LocatedIn)
          │                     │
          ▼                     ▼
    [GuitarPractice]      [LivingRoom] <──(Produces 120Hz)── [Fan]
```

#### C++ Data Structure (`include/capabilities/world_model_graph.h`)
```cpp
namespace Ronin::Kernel::Capability {

struct Relation {
    std::string type;         // e.g., "LocatedIn", "Holding"
    float confidence = 1.0f;  // Probabilistic belief state
    uint64_t timestamp = 0;   // Staleness check
};

struct EntityNode {
    std::string id;           // e.g., "user_1", "living_room"
    std::string type;         // e.g., "User", "Room"
    std::unordered_map<std::string, std::string> properties;
};

class WorldModelGraph {
public:
    static WorldModelGraph& getInstance();
    void addEntity(const EntityNode& node);
    void addRelation(const std::string& source_id, const std::string& target_id, const Relation& relation);
    std::vector<EntityNode> traverse(const std::string& start_id, const std::string& relation_type);
    
private:
    std::unordered_map<std::string, EntityNode> m_nodes;
    std::unordered_map<std::string, std::unordered_map<std::string, Relation>> m_edges; // adjacency list
    std::mutex m_mutex;
};

}
```

### B. Shared Sensor Stream Bus
Exposing sensors via a stream bus using shared ring buffers prevents conflicting hardware locks. Multiple virtual components (Tuner, Noise, Presence) register as consumers to the same raw sensor stream instead of initializing independent driver instances.

```
 [Raw Mic Sensor] ──> [Shared Stream Bus] ──┬──> [Guitar Tuner]
                                            ├──> [Noise Monitor]
                                            └──> [Presence Detector]
```

### C. Capability Discovery & Constraint Pipeline
To scale beyond hundreds of candidate tools, semantic search is placed at the end of a multi-stage filtering pipeline.

```
 [Goal] ──> [Extraction] ──> [Type Constraints] ──> [Permission Filter] ──> [Availability] ──> [Semantic Rank] ──> [Candidates]
```

### D. Graph Optimizer (Weighted Objective Formulation)
Paths are resolved using a weighted scoring objective function:

$$\text{Score} = w_{\text{lat}} \cdot S_{\text{latency}} + w_{\text{pwr}} \cdot S_{\text{power}} + w_{\text{conf}} \cdot S_{\text{confidence}} + w_{\text{cache}} \cdot S_{\text{cache\_hit}}$$

Where:
* $S_{\text{latency}} = \frac{1}{\text{Latency}_{\text{est}}}$
* $S_{\text{power}} = 1.0 - \text{PowerCost}_{\text{est}}$
* $w$ are configurable coefficients depending on performance profiles (e.g., Battery Saving vs Performance Mode).

### E. Runtime Scheduler
Residing between Planner and Executor, the Scheduler orchestrates step parallelism, respects battery throttles, enforces timeouts, and manages resource locks.

```cpp
namespace Ronin::Kernel::Execution {

enum class TaskPriority { LOW, MEDIUM, HIGH, CRITICAL };

struct ScheduledTask {
    std::string task_id;
    std::string tool_name;
    std::string payload;
    TaskPriority priority;
    uint32_t timeout_ms;
};

class RuntimeScheduler {
public:
    static RuntimeScheduler& getInstance();
    std::future<std::string> schedule(const ScheduledTask& task);
    void cancelTask(const std::string& task_id);
    void setBatteryMode(bool power_save);
    
private:
    std::priority_queue<ScheduledTask> m_queue;
    std::unordered_map<std::string, std::shared_ptr<std::promise<std::string>>> m_active_tasks;
    std::mutex m_mutex;
};

}
```

### F. Multi-Dimensional Reflection Validation Pipeline
To ensure truthfulness, reflections undergo validation checking before database write promotion:

```
 [Observation] ──> [Hypothesis] ──> [Validation Check (Min Sessions & Time)] ──> [Fact Promotion]
```

* **Heuristic Constraints**:
  * $\text{Evidence Count} \ge 10$
  * $\text{Accumulated Confidence} \ge 0.90$
  * $\text{Contradiction Rate} \le 0.02$
  * $\text{Validation Period} \ge 7 \text{ days}$

---

## 3. Staged Implementation Plan (v4.0)

### Phase 1: Shared Stream Bus & DSP Ring Buffers (Kotlin & JNI)
* Implement thread-safe ring buffers for real-time accelerometer and microphone streams.
* Update `PerceptionEngine` to register as a listener on the shared bus.

### Phase 2: Graph-based World Model (C++)
* Refactor C++ `BeliefState` database structures to support Directed Multigraph traversal.
* Implement adjacency edge creation and semantic search across nodes.

### Phase 3: Runtime Scheduler & Optimization Weights
* Create the prioritization queue engine.
* Implement weighted scoring logic inside the `GraphOptimizer`.

### Phase 4: Node Collapsing in Capability Compiler
* Refactor the `CapabilityCompiler` compiler loop to generate macro-tool configurations that collapse multiple graph steps into a unified native executable block, bypassing planner overhead.
