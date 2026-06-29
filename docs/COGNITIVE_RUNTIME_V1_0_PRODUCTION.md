# Ronin Cognitive Runtime v1.0: Locked Production Release Specification

This document freezes the architectural design scope and establishes the implementation blueprint for the production release of **Ronin Runtime v1.0**. The architectural specs are locked. Any future changes require a formal RFC (Request for Change) process with design review and benchmark proof.

---

## 1. Unified Operating Architecture (Ronin Runtime v1.0)

```
                            PHYSICAL SENSORS
                       (Mic, IMU, GPS, Bluetooth)
                                   │
                                   ▼
                       Shared Sensor Stream Bus
                                   │
                                   ▼
                    Uniform Kernel Services (IService)
                         (TaskHandle Async API)
                                   │
                                   ▼
                        Priority-Based Event Bus
                (Backpressure, Coalescing & Priority Inversion Prevention)
                                   │
                                   ▼
              Attention Manager   <───>   Resource Manager
                                   │
                                   ▼
                           Actor Supervisor
                    (OTP-Style Supervision Trees)
                                   │
                                   ▼
                            Actor Framework
                 (Sensor, Memory, Planner, Llm Actors)
                                   │
                                   ▼
                           Task Decomposer
                                   │
                                   ▼
                              Plan Engine  <───>  Planner Rule Cache
                                   │                  (Auto-Invalidated)
                                   ▼
                             Policy Engine
                                   │
                                   ▼
                 ExecutionContext & Blackboard Memory
                     (Strongly Typed Shared Blackboard)
                                   │
                                   ▼
                             Goal Monitor
                                   │
                                   ▼
                             Reflection
                                   │
                                   ▼
                             Evaluator
                 (Accuracy, Latency, Power, Satisfaction)
                                   │
                                   ▼
                           Meta-Cognition
                                   │
                                   ▼
                       Event Sourced Memory Log
                                   │
                                   ▼
                     Knowledge Graph & Semantic Index
                                   │
                                   ▼
                          Capability Compiler
                   (Shadow Tests, A/B & Safe Promotes)
```

---

## 2. Deep Systems Engineering Specifications (v1.0)

### A. Modular Repository Architecture (`ronin-runtime/`)
The runtime is organized into isolated decoupled directories:
```
ronin-runtime/
 ├── kernel/          # Microkernel core scheduler and priority event bus
 ├── actors/          # Actor framework and lifecycle handlers
 ├── services/        # Platform service contracts (IService)
 ├── adapters/        # Android Adapter Layer (WorkManager, Services, Activity bindings)
 ├── capabilities/    # Manifests and capability registry
 ├── dsp/             # High performance stream bus and ring buffers
 ├── memory/          # Event sourcing appends, Knowledge Graph, SQLite
 ├── reasoning/       # Model-agnostic plugins (IReasoningEngine)
 ├── benchmarks/      # Latency, battery, and memory profiling harnesses
 ├── tests/           # Integration and gtest suites
 └── examples/        # Reference implementations (e.g. Guitar Tuner)
```

### B. Standard TaskHandle & Async Service Contract (`IService`)
Instead of using `std::future`, platform services utilize a `TaskHandle` supporting cancellation, timeout checks, and state monitoring:

```cpp
namespace Ronin::Kernel::Services {

struct ServiceRequest {
    std::string action;
    std::string payload_json;
};

struct ServiceResult {
    bool success = false;
    std::string result_json;
    std::string error_message;
};

class TaskHandle {
public:
    virtual ~TaskHandle() = default;
    virtual bool cancel() = 0;
    virtual bool isFinished() = 0;
    virtual ServiceResult await() = 0;
};

class IService {
public:
    virtual ~IService() = default;
    virtual std::unique_ptr<TaskHandle> execute(const ServiceRequest& request, const ExecutionContext& ctx) = 0;
};

} // namespace Ronin::Kernel::Services
```

### C. Capability Manifest (With Schema Versioning & Execution Metadata)
Manifest schemas contain explicit schema versions and execution properties to guide graph planner optimizations:

```json
{
  "schema_version": 1,
  "id": "pitch_analysis",
  "capability_version": "1.2.0",
  "dependencies": ["fft>=1.0.0", "detect_peaks>=1.0.0"],
  "permissions": ["android.permission.RECORD_AUDIO"],
  "inputs": ["audio_stream"],
  "outputs": ["pitch_frequency"],
  "estimated_power_cost": "LOW",
  "deterministic": true,
  "streaming": true,
  "cacheable": true,
  "thread_safe": true,
  "idempotent": true
}
```

### D. Strongly Typed Blackboard Working Memory (With Write Ownership)
The `Blackboard` maps strongly typed values and enforces strict write-ownership rules to prevent race conditions:
* `SensorActor` has write-ownership on sensor keys (e.g., `raw_audio`).
* `DspActor` has write-ownership on computation keys (e.g., `fft_result`).
* `PlannerActor` has write-ownership on execution routing keys (e.g., `execution_plan`).

```cpp
namespace Ronin::Kernel::Execution {

using BlackboardValue = std::variant<
    int,
    float,
    bool,
    std::string,
    std::vector<float>,
    nlohmann::json
>;

class Blackboard {
public:
    void write(const std::string& key, const BlackboardValue& val, const std::string& actor_id);
    BlackboardValue read(const std::string& key);
    bool contains(const std::string& key);
    void clear();
    
private:
    std::unordered_map<std::string, BlackboardValue> m_board;
    std::unordered_map<std::string, std::string> m_ownership; // key -> owner_actor_id
    std::shared_mutex m_rw_mutex;
};

} // namespace Ronin::Kernel::Execution
```

### E. Event Bus Priority Inversion Prevention
To prevent low-priority tasks from locking resources required by critical events, the scheduler implements **Priority Inheritance** (temporarily boosting locked resource owner priority to match the critical request) and **Timeout Preemption** policies.

### F. Relevance-Based Memory Decay
The memory retention score is calculated dynamically to manage SQLite footprint:

$$\text{Retention Score} = \frac{\text{Importance} \cdot \text{Access Frequency} \cdot \text{User Confirmation}}{\text{Age}}$$

* Recent episodes: Expire and prune if retention score falls below critical threshold.
* User-confirmed facts: Retained permanently.

---

## 3. Implementation Dependency Graph & Build Order

```
[Event Bus] ──> [Actor Runtime] ──> [Kernel Services] ──> [DSP Core] ──> [Capability Registry] ──> [Planner] ──> [Policy] ──> [Gemma Engine]
```

---

## 4. Target Performance Benchmarks & CI/CD Regression Gates
Every build run inside the CI/CD pipeline executes benchmark tests, failing the build if a performance regression is observed:

| Metric | Target Threshold | Regression Limit |
| :--- | :--- | :--- |
| **Event dispatch latency** | < 1 ms | Max +5% deviation |
| **Blackboard read latency** | < 50 µs | Max +10% deviation |
| **FFT (4096 samples)** | < 10 ms | Max +5% deviation |
| **Memory query latency** | < 20 ms | Max +5% deviation |
| **Cold LLM startup time** | < 2 s | Max +10% deviation |
| **Background RAM footprint**| < 80 MB | Absolute limit 90MB |

---

## 5. Feature-Driven Milestone Roadmap

### Milestone 0: Infrastructure Setup
* **Deliverables**: CMake multiplatform configuration, CI/CD automated pipeline, logging harnesses, benchmark runners, unit-test suites.

### Milestone 1: Hello World Cognitive Runtime
* **Deliverables**: Priority Event Bus, Actor Runtime, Execution Context, Blackboard State.
* **Outcome**: Verified multithreaded message dispatcher and JNI execution boundary.

### Milestone 2: Audio & DSP Engine (Guitar Tuner)
* **Deliverables**: Audio Service implementation (`IService`), PFFFT stream pipeline, Capability Registry parsing, Mic Actor integration.
* **Outcome**: Fully operational Guitar Tuner processing mic input and calculating pitch offsets.

### Milestone 3: World Memory & Validation (Resonance Analyzer)
* **Deliverables**: World Model Graph, Reflection Engine, closed-loop Goal Monitor.
* **Outcome**: Background resonance tracking and environment state synthesis.

### Milestone 4: Self-Optimizing Cognitive OS
* **Deliverables**: Policy Engine, Actor Supervisor Tree, Capability Compiler, A/B Version Testing, Planner Rule Cache.
* **Outcome**: Runtime dynamically optimizes paths, versioning macro-capabilities, and throttling budgets on low-power devices.
