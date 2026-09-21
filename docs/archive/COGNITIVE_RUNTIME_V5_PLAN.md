# Ronin Cognitive Runtime v5.0: Cognitive Operating Core Architecture

This document establishes the architecture for **Ronin Cognitive OS (v5.0)**, upgrading the runtime from an event-driven capability planner into a decentralized, actor-based cognitive operating core.

---

## 1. Unified Operating Architecture (v5.0)

```
                            PHYSICAL SENSORS
                       (Mic, IMU, GPS, Bluetooth)
                                   │
                                   ▼
                       Shared Sensor Stream Bus
                                   │
                                   ▼
                         Event Detection Layer
                                   │
                                   ▼
                           Attention Manager
                      (Resource & Battery Budget)
                                   │
                                   ▼
                          Capability Registry
                 (Logical Cap Decoupled from Tools)
                                   │
                                   ▼
                                Planner
                 (Using Gemma as Reasoning Coprocessor)
                                   │
                                   ▼
                       Runtime Scheduler (Actors)
                   (Asynchronous Message Passing)
                                   │
                                   ▼
                            Graph Executor
                                   │
                                   ▼
                             Goal Monitor
                    (Closed-Loop Goal Validation)
                                   │
                                   ▼
                   Scientific Reflection & Experiment
                 (Obs -> Expl -> Hyp -> Exp -> Fact)
                                   │
                                   ▼
                            Knowledge Graph
                     (Relation Traversal Engine)
                                   │
                                   ▼
                          Capability Compiler
                       (Graph Rewrite Engine)
```

---

## 2. Deep Core Specifications (v5.0)

### A. Reasoning Coprocessor Paradigm
Gemma 4 is treated as a **Reasoning Coprocessor** rather than the runtime's central CPU. The Runtime Scheduler and Event System drive the core logic, ensuring that changing the underlying LLM does not break the execution core.

### B. Actor-Based Runtime Scheduler
To avoid race conditions and deadlocks on Android, the scheduler manages hardware and compute resources as independent **Actors** communicating via asynchronous message queues.

```cpp
namespace Ronin::Kernel::Execution {

struct ActorMessage {
    std::string sender_id;
    std::string recipient_id;
    std::string type;
    std::string payload_json;
};

class Actor {
public:
    virtual ~Actor() = default;
    void postMessage(const ActorMessage& msg) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_inbox.push(msg);
        m_cv.notify_one();
    }
    
protected:
    virtual void processMessage(const ActorMessage& msg) = 0;
    
    std::queue<ActorMessage> m_inbox;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};

// Specialized Sensor / Compute Actors
class MicActor : public Actor { /* Handler */ };
class LlmCoprocessorActor : public Actor { /* Handler */ };
class DspActor : public Actor { /* Handler */ };

}
```

### C. Knowledge Graph (Relation Traversal)
The World Model is upgraded to a semantic **Knowledge Graph** supporting relation paths. This allows the system to traverse edges to explain root causes:
* Query: `"Why is there a 120Hz vibration?"`
* Traversal path: `[living_room] ──(contains)──> [fan] ──(produces)──> [120Hz vibration]`
* Reasoning output: `"The 120Hz vibration is caused by the fan located in the living room."`

### D. Attention System (Resource Allocator)
The **Attention Manager** throttles active actors and sensor frequency based on hardware battery budget and user constraints.

```cpp
namespace Ronin::Kernel::Energy {

struct SystemBudgets {
    float cpu_limit = 1.0f;
    float battery_level = 1.0f;
    bool enable_high_power_sensors = true;
};

class AttentionManager {
public:
    static AttentionManager& getInstance();
    void updateBudgets(const SystemBudgets& budgets);
    bool shouldAuthorizeExecution(const std::string& actor_name);
    
private:
    SystemBudgets m_current_budgets;
    std::mutex m_mutex;
};

}
```

### E. Goal Monitor (Closed-Loop Executor)
Unlike fire-and-forget execution, the **Goal Monitor** verifies outcomes. If a goal (e.g. `"Detect pitch resonance"`) fails because FFT peaks are below threshold, it triggers retries with adjusted parameters (e.g., `"increase mic gain"`).

### F. Scientific Reflection & Experiment Pipeline
Insights undergo scientific validation:
$$\text{Observation} \longrightarrow \text{Explanation} \longrightarrow \text{Hypothesis} \longrightarrow \text{Experiment} \longrightarrow \text{Fact}$$

Example:
1. **Observation**: Room acoustic resonance changed.
2. **Explanation**: Sound absorption or noise pattern shifted.
3. **Hypothesis**: The fan was turned on.
4. **Experiment**: Dynamic check on fan activity or power state.
5. **Fact**: Fan is confirmed running; fact committed to LTM.

---

## 3. Staged Implementation Roadmap (v5.0)

### Phase 1: Actor Model Framework (C++ Core)
* Implement base `Actor` message queues and initialize the `MicActor`, `DspActor`, and `LlmCoprocessorActor` threads.
* Integrate the `RuntimeScheduler` dispatch loop to post messages directly to respective Actor queues.

### Phase 2: Goal Monitor & Closed-Loop Planning
* Write the `GoalMonitor` verification callbacks.
* Allow execution graphs to dynamically yield branch options if a node output fails verification thresholds.

### Phase 3: Attention Manager & Dynamic Budgets
* Implement Battery status event listeners in Kotlin.
* Wire budget updates to C++ to toggle hardware authorization locks.

### Phase 4: Knowledge Graph Reasoning
* Implement relational path traversal algorithms in C++ to allow Gemma to traverse network connections.
