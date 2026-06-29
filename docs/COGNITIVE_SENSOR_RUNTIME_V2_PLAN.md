# Ronin Cognitive Sensor Runtime v2.0: Implementation Plan

This document outlines the implementation plan and architectural specifications to transition the Ronin Kernel from a static **Intent-to-Capability Router** into a dynamic **Dynamic Capability-Graph Router** leveraging on-device sensors, DSP acceleration, perception state classification, post-execution reflection, and self-learning pattern compilation.

---

## 1. Architectural Architecture Spec

```
                                USER
                                 │
                     Text / Voice / Event / State
                                 │
                                 ▼
                         Gemma 4 (Brain)
                                 │
                    Goal / Dynamic Intent Extraction
                                 │
                                 ▼
                    Capability Discovery Engine
                                 │
                      (Search & Rank Tools)
                                 │
                                 ▼
                     Dynamic Graph Planner
                                 │
                       (DAG Graph Generation)
                                 │
                                 ▼
                         Graph Executor
          ┌──────────────────────┼──────────────────────┐
          ▼                      ▼                      ▼
    Sensor Capabilities      DSP Engines         Memory/State Tools
     (IMU, Mic, GPS)       (FFT, Peak, RMS)       (Vault, Episodic)
          └──────────────────────┬──────────────────────┘
                                 │
                                 ▼
                    Structured Observation (JSON)
                                 │
                                 ▼
                      Reflection Loop (Gemma)
                                 │
                       (State Synthesis)
                                 │
                                 ▼
                       Perception Engine
                                 │
                        (Activity/Context)
                                 │
                                 ▼
                      Self-Learning Skill compiler
                                 │
                        (Macro-Skill Promotion)
```

---

## 2. Core Architectural Components

### A. Capability Discovery Engine (C++)
The Discovery Engine replaces the hardcoded `mapIntentToCapability` with a registry-based lookup.

* **Tool Signature Metadata**:
  Each registered tool defines its expected inputs, outputs, description, and permissions.
  ```json
  {
    "tool_name": "fft",
    "description": "Computes Fast Fourier Transform on float arrays to find frequencies",
    "inputs": ["float_array"],
    "outputs": ["float_array_frequencies"],
    "required_permissions": []
  }
  ```
* **Discovery & Ranking Mechanism**:
  When Gemma outputs a high-level goal (e.g. `"Need frequency measurement for pitch"`), the Discovery Engine performs:
  1. **Keyword/Token Similarity matching** (via Trie Segmenter and Jaccard distance on descriptions).
  2. **Dependency Resolution**: Ensures that the output type of a candidate tool matches the input type of the next candidate tool.
  3. **Candidate Ranking**: Sorts tools based on execution latency (historical), resource weight, and confidence scores.

### B. C++ DSP Capability Layer
Exposes high-performance native DSP functions. Instead of custom analytical logic for every sensor, we expose standard primitive building blocks as JNI/C++ tools.

1. **`fft(array)`**: Uses the existing optimized `PFFFT` library.
2. **`bandpass/lowpass/highpass(array, fc, fs)`**: Configurable IIR/FIR Butterworth filters.
3. **`detect_peaks(array, threshold)`**: Peak-to-peak frequency detection.
4. **`zero_crossing(array)`**: Fundamental frequency estimation.
5. **`rms(array)`**: Root Mean Square to measure energy.

### C. Perception Engine & Sensor Fusion
Perception should be run deterministically on-device inside a native background scheduler, updating a global **Belief Context / Perception State** that JParams injects into every tool execution.
* **Trained Segmented Classifiers**: Uses simple heuristics + lightweight models (e.g. decision trees or shallow neural nets) to classify physical states (e.g. `Walking`, `Phone in Pocket`, `Door Knock`, `Guitar Practice`).
* **Why runtime instead of LLM?** Avoids resource wasting. The LLM only reviews the classified state from the perception engine rather than computing raw sensor inputs.

### D. Skill Compiler & Self-Learning (Pattern Promotion)
To avoid planning overhead for recurring user workflows:
1. Every session execution is recorded to the long-term memory episodes table: `(goal, steps_taken, outcome, execution_time_ms)`.
2. When the same sequential pattern of tool usage (e.g., `Mic` -> `FFT` -> `Peak Detect` -> `Note Mapper`) is executed successfully over **100 times**:
   - The **Skill Compiler** synthesizes a new compound intent: `GuitarTuner`.
   - The sequence is compiled into a single static virtual tool registered in the tool registry.
   - The intent planner is updated by writing a dynamic few-shot rule directly into the Long-Term Memory `lessons` table, bypassing future graph-generation reasoning.

---

## 3. Class Design & API Specifications (C++)

### 1. `ToolRegistry` (`include/capabilities/tool_registry.h`)
```cpp
namespace Ronin::Kernel::Capability {

struct ToolMetadata {
    std::string name;
    std::string description;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> required_permissions;
    float average_latency_ms = 0.0f;
    float success_rate = 1.0f;
};

class ToolRegistry {
public:
    static ToolRegistry& getInstance();
    void registerTool(const ToolMetadata& meta, std::function<std::string(const std::string&)> impl);
    std::vector<ToolMetadata> searchTools(const std::string& query);
    std::string execute(const std::string& name, const std::string& payload);
    
private:
    std::unordered_map<std::string, ToolMetadata> m_registry;
    std::unordered_map<std::string, std::function<std::string(const std::string&)>> m_implementations;
};

} // namespace Ronin::Kernel::Capability
```

### 2. `CapabilityDiscoveryEngine` (`include/capabilities/discovery_engine.h`)
```cpp
namespace Ronin::Kernel::Reasoning {

class CapabilityDiscoveryEngine {
public:
    CapabilityDiscoveryEngine(Capability::ToolRegistry* registry);
    
    // Ranks tools based on capability requirements
    std::vector<Capability::ToolMetadata> resolveCapabilities(const std::vector<std::string>& requirements);
    
    // Builds a directed acyclic execution graph (DAG) based on input/output compatibility
    std::vector<std::string> buildExecutionGraph(const std::vector<Capability::ToolMetadata>& resolved_tools);
};

} // namespace Ronin::Kernel::Reasoning
```

---

## 4. Staged Implementation Roadmap

### Stage 1: Tool Registry & DSP Core (C++) - Completed & Verified
* Refactor `IntentEngine` to use `ToolRegistry` dynamic declarations.
* Build the native C++ DSP library wrappers around standard utilities using `PFFFT` (FFT, filter bands, peak detection).

### Stage 2: Capability Discovery Engine (C++) - Completed & Verified
* Implement Jaccard/TF-IDF similarity checks between user requirements and tool descriptions.
* Construct DAG-based planners by matching outputs and inputs of candidate capabilities.

### Stage 3: Sensor Fusion & Android Perception Layer (Kotlin) - Completed & Verified
* Implement high-frequency background handlers for Accelerometer/Gyroscope and Mic capturing inside `MainActivity.kt`.
* Wire a rule-based Perception state machine that runs at 10Hz and stores current status (e.g. `Walking`, `Quiet Room`) in SQLite.

### Stage 4: Self-Learning Skill compiler - Completed & Verified
* Write the C++ skill promotion background thread that scans SQLite episodes, detects recurrent step sequences, and compiles/registers them as static macro-skills.
