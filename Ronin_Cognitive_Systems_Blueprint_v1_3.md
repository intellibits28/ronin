# Ronin Cognitive Systems Blueprint v1.3 (Hardened)

Industrial-grade evolution of Ronin into a deterministic, self-evaluating cognitive runtime platform.

---

## 1. Core Cognitive Architecture

Ronin is a persistent cognitive runtime that:
*   **Observes** the environment.
*   **Maintains Beliefs** about the world.
*   **Executes Goals** via directed actions.
*   **Predicts Outcomes** and evaluates error.
*   **Updates Models** via reflection.

**Core Loop:**
`Observe` → `Update World State` → `Update Belief State` → `Goal Selection` → `Planning` → `Prediction` → `Execution` → `Reflection` → `Memory Consolidation`.

---

## 2. Multi-Tier Memory Architecture (Sovereign Storage)

### 2.1 Memory Ownership Model
Before storage, the **MemoryClassifier** determines the destination tier:
*   **VAULT:** Explicit secrets (API keys, Tokens). Hardware-backed encryption.
*   **FACT:** Structured, durable knowledge (EAV Model). High-precision.
*   **NOTE:** Narrative or fuzzy context. FTS5/Semantic search.
*   **EPISODE:** Historical actions, outcomes, and task logs.
*   **PREDICTION:** Expected future outcomes. Used for belief calibration.

---

## 3. World & Belief State Layers

### 3.1 World State (Transient)
Represents the current physical environment:
```cpp
struct WorldState {
    float battery_percent;
    float ram_available_mb;
    bool gps_available;
    bool network_available;
    bool charging;
    uint64_t timestamp;
};
```

### 3.2 Belief State (Confidence-Weighted)
Represents current assumptions derived from Facts and World State:
```cpp
struct Belief {
    std::string key;
    std::string value;
    float confidence;
    uint64_t updated_at;
};
```

---

## 4. Memory Schema (v13.0 Hardened)

```sql
-- FACTS (Deterministic Knowledge)
CREATE TABLE facts (
    id INTEGER PRIMARY KEY,
    entity TEXT,
    attribute TEXT,
    value TEXT,
    confidence REAL,
    created_at INTEGER,
    updated_at INTEGER
);
CREATE INDEX idx_facts_lookup ON facts(entity, attribute);

-- EPISODES (Task Logs with Confidence Delta)
CREATE TABLE episodes (
    id INTEGER PRIMARY KEY,
    timestamp INTEGER,
    intent TEXT,
    goal_id TEXT,
    node_id TEXT,
    summary TEXT,
    payload_json TEXT,
    outcome_enum INTEGER,
    latency_ms INTEGER,
    confidence_before REAL,
    confidence_after REAL
);

-- PREDICTIONS (Expectation vs Reality)
CREATE TABLE predictions (
    id INTEGER PRIMARY KEY,
    timestamp INTEGER,
    goal_id TEXT,
    node_id TEXT,
    predicted_json TEXT,
    actual_json TEXT,
    error_score REAL
);
```

---

## 5. Bayesian Reasoning Layer

### 5.1 Thompson Sampling & Decay
*   **Sampling:** `Sample ~ Beta(effective_success + 1, effective_failure + 1)`.
*   **Confidence Decay:** `effective_val = current_val * 0.99^weeks_since_update`.
*   **Reflection Engine:** Calculates `Prediction Error = |Actual - Predicted|` to update Beliefs.

---

## 6. Implementation Roadmap

### Phase 1: Interaction & Security (Done)
*   SMS, Location, Hardware Vault.

### Phase 2: Memory Foundation (Done)
*   [x] Refactor Fact/Episode tables (v13.0).
*   [x] Implement Prediction storage.
*   [x] Build Memory Classifier (Intent -> Tier).
*   [x] 1Hz World State Telemetry Injection.
*   [x] Hardware-backed Vault with Biometric HITL.

### Phase 3: Cognitive Runtime (Done)
*   [x] **Belief State:** Persistent key-value store of confidence-weighted truths.
*   [x] **Reflection Engine (RLHF):** Self-correction loop using user feedback and episodic memory.
*   [x] **DAG Graph Executor:** Multi-step planning with dependency resolution.
*   [x] **Bayesian Reward System:** Thompson Sampling with symmetric decay for learning.

### Phase 4: Expanded Capabilities & System Integration (Active)

*   [ ] **Native Alarm Integration:** Setting and managing Android alarms via intents.
*   [ ] **Native Calendar Integration:** Event creation and schedule awareness.
*   [ ] **Long Horizon Planning:** Multi-step goal decomposition (e.g. Calendar-aware Alarms).
*   [ ] **Self-Evaluation Loops:** Continuous strategy optimization.

---
*Revised: 2026-06-08 | Ronin Kernel Engineering Standards v1.3*
