# Ronin Cognitive Runtime v1.0: Production Release Spec & Implementation Blueprint

This document freezes the architectural design scope and establishes the implementation blueprint for the production release of **Ronin Runtime v1.0**. No further architectural layers will be added; focus is shifted entirely to software engineering interfaces, validation, profiling, and resource optimizations on Android.

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
                      (Asynchronous Futures API)
                                   │
                                   ▼
                        Priority-Based Event Bus
                (Backpressure & Event Coalescing Queues)
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

### B. Standard Async Service Contract (`IService`)
Platform services execute asynchronously to prevent blocking the calling Actor threads:

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

class IService {
public:
    virtual ~IService() = default;
    virtual std::future<ServiceResult> execute(const ServiceRequest& request, const ExecutionContext& ctx) = 0;
};

} // namespace Ronin::Kernel::Services
```

### C. Capability Manifest (With Schema Versioning)
Capabilities are declared using schema-versioned manifests to allow easy format migrations:

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
  "deterministic": true
}
```

### D. Strongly Typed Blackboard Working Memory
To avoid string serialization overhead, the `Blackboard` holds strongly typed variants:

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
    void write(const std::string& key, const BlackboardValue& val);
    BlackboardValue read(const std::string& key);
    bool contains(const std::string& key);
    void clear();
    
private:
    std::unordered_map<std::string, BlackboardValue> m_board;
    std::shared_mutex m_rw_mutex; // Reader-Writer lock for concurrency
};

} // namespace Ronin::Kernel::Execution
```

### E. Event Bus Backpressure & Coalescing
To prevent event queue overflow from high-frequency sensors (e.g., 200Hz IMU, 48kHz Mic):
1. **Backpressure**: Standardized queue size limits. If the queue is full, events are dropped or throttled according to the drop policies.
2. **Coalescing**: Consecutive events of the same sensor type are dynamically coalesced/aggregated before routing to subscribers.

### F. Memory Decay & Retention Rules
Memory tables are dynamically pruned to maintain storage health:
* **Recent episodes**: Expire and prune after **30 days**.
* **User-confirmed facts**: Retained permanently (**never expire**).
* **Raw sensor telemetry**: Aggregated or downsampled after **24 hours**.
* **System debug logs**: Automated deletion after **7 days**.

### G. Target Performance Benchmarks
All changes must satisfy the target performance thresholds on target devices:

| Metric | Target Threshold |
| :--- | :--- |
| **Event dispatch latency** | < 1 ms |
| **Blackboard read latency** | < 50 µs |
| **FFT (4096 samples)** | < 10 ms |
| **Memory query latency** | < 20 ms |
| **Cold LLM startup time** | < 2 s |
| **Background RAM footprint**| < 80 MB |

---

## 3. Feature-Driven Milestone Roadmap

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
