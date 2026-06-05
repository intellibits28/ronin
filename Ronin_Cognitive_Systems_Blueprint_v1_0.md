# Ronin Cognitive Systems Blueprint v1.3 (Hardened)

Industrial-grade evolution of Ronin into a deterministic agent runtime platform.

---

## 1. Multi-Tier Memory Architecture (Sovereign Storage)

### 1.1 Memory Classifier & Ownership
The **MemoryClassifier** determines the destination tier based on input intent:
*   **VAULT:** Secrets, API Keys (Hardware encrypted).
*   **FACT:** Concise, structured knowledge (EAV Model).
*   **NOTE:** Narrative or fuzzy context (FTS5 enabled).
*   **EPISODE:** Automatic logs of past actions and results (FTS5 enabled).

### 1.2 Schema (v13.0 - Lifecycle-Aware)
```sql
-- FACTS (EAV with Source Tracking)
CREATE TABLE facts (
    id INTEGER PRIMARY KEY,
    entity TEXT,
    attribute TEXT,
    value TEXT,
    source_type INTEGER, -- 0:USER_EXPLICIT, 1:USER_INFERRED, 2:OCR
    confidence REAL,
    last_verified_at INTEGER,
    created_at INTEGER,
    updated_at INTEGER
);
CREATE INDEX idx_facts_lookup ON facts(entity, attribute);

-- EPISODES (Searchable Task Logs)
CREATE TABLE episodes (
    id INTEGER PRIMARY KEY,
    timestamp INTEGER,
    intent TEXT,
    summary TEXT,
    payload_json TEXT,
    outcome_enum INTEGER -- 0:FAIL, 1:SUCCESS
);
CREATE VIRTUAL TABLE episodes_fts USING fts5(summary, content='episodes', content_rowid='id');
```

### 1.3 Memory Consolidator (The Lifecycle Service)
A background worker responsible for data health:
*   **Conflict Resolution:** Detects multiple values for a single fact (e.g. `Father:Medication`) and promotes the newest/most verified.
*   **Deduplication:** Merges redundant notes and expires old, failed episodes.

---

## 2. Reasoning Layer: The Bayesian Brain

### 2.1 Symmetric Confidence Decay
*   To prevent bias, both success and failure weights are aged:
    *   `effective_success = current_success * aging_factor`
    *   `effective_failure = current_failure * aging_factor`
*   **Thompson Selection:** `Sample ~ Beta(effective_success + 1, effective_failure + 1)`.

---

## 3. Presentation Layer: Developer HUD (Mi 11 Lite 5G NE Optimized)

### 3.1 Tiered Metrics (1 Hz Refresh Rate)
*   **Runtime:** Active Sessions, Queue Depth.
*   **LLM:** Latency, Token Budget.
*   **Bayesian:** Belief Scores, Thompson Samples.

---

## 4. Implementation Roadmap (Revised)

### Phase 1: Interaction Foundation (Active)
*   [x] SMS, Location, Vault.

### Phase 2: Memory Mastery
*   [ ] Refactor Fact Store (Source Tracking + EAV).
*   [ ] Implement Searchable Episodic Memory (FTS5).
*   [ ] Build Memory Classifier (Intent -> Tier).

### Phase 3: Memory Lifecycle
*   [ ] Memory Consolidator Service (Conflict/Deduplication).
*   [ ] Advanced Memory Search (Exact/Episodic/FTS).

### Phase 4: Bayesian Reasoning
*   [ ] Thompson Sampling with Symmetric Decay.
*   [ ] TaskNode DAG Executor.

### Phase 5: Observability
*   [ ] 1Hz Developer HUD & Visualization.

---
*Revised: 2026-06-04 | Ronin Kernel Engineering Standards*
