# Ronin Cognitive Systems Blueprint v1.0 (Hardened)

This document outlines the implementation of the industrial-grade AI agent runtime, focusing on deterministic memory management, Bayesian reasoning, and optimized observability for mid-range hardware (Mi 11 Lite 5G NE).

---

## 1. Multi-Layer Memory Architecture (Sovereign Storage)

Memory is no longer a simple text store; it is a classified cognitive hierarchy.

### 1.1 Data Tiers & Storage Strategy
| Tier | Description | Storage Engine | Security |
| :--- | :--- | :--- | :--- |
| **NOTE** | Long-form narratives, general context, and logs. | SQLite FTS5 | Plaintext (Internal) |
| **FACT** | Structured "Ground Truth" (EAV Model). | SQLite (Relational) | Plaintext (Internal) |
| **VAULT** | API Keys, Passwords, Sensitive Data. | Android Keystore | AES-256-GCM |

### 1.2 Schema Evolution (v10.0)
The `memories` table is extended to support these tiers:
```sql
ALTER TABLE memories ADD COLUMN tier_enum INTEGER DEFAULT 0; -- 0:NOTE, 1:FACT, 2:VAULT
ALTER TABLE memories ADD COLUMN entity_attr TEXT; -- For FACT mapping (e.g. "Car:Plate")
```

---

## 2. Reasoning Layer: The Bayesian Brain (GraphExecutor)

Decision making uses probabilistic modeling to choose the best action path.

### 2.1 Thompson Sampling Logic
*   **Bayesian Belief:** Calculated as `Belief = (Success + 1) / (Success + Failure + 2)`.
*   **Dynamic Weighting:** Every edge in the Graph holds a weight that updates based on tool execution outcome (`reportOutcomeNative`).

### 2.2 Hierarchical DAG Layout
The system organizes capabilities in a directed acyclic graph:
`ROOT` → `Intent Detection` → `Capability Selection` → `Execution`.

---

## 3. Presentation Layer: Observable UI (Mi 11 Lite 5G NE Optimized)

To prevent thermal throttling and screen clutter, the UI follows a "Minimalist-Instrumentation" approach.

### 3.1 Device Constraints (Xiaomi Mi 11 Lite 5G NE)
*   **Thermal Budget:** Low (Slim build). LLM activities must minimize concurrent GPU load.
*   **Display:** AMOLED. Uses Pure Black (#000000) for battery saving and burn-in protection.
*   **Refresh Rate:** Adaptive. System metrics update every 5s instead of every frame.

### 3.2 UI Component Strategy
1.  **Instrumentation Top Bar:** Real-time RAM/Temp/JNI stats (Togglable).
2.  **Live Reasoning Overlay:** A bottom sheet showing the current step (e.g., "[AGENT] Resolving Contact...").
3.  **Bayesian Graph View:** Accessed via a "System Info" icon. Uses static line art instead of heavy glow effects to save GPU cycles.
4.  **HITL Dialogs:** Standardized safety prompts for sensitive actions (SMS/Vault).

---

## 4. Implementation Roadmap

### Phase 1: Memory Tiering (Alpha)
*   [ ] Implement SQL schema updates for NOTE/FACT/VAULT.
*   [ ] Add `storeFact` and `storeNote` functions to `LongTermMemory.cpp`.
*   [ ] Integrate Android Keystore for VAULT encryption.

### Phase 2: Graph Logic (Beta)
*   [ ] Build `BetaDistribution` helper in C++.
*   [ ] Hook `GraphExecutor` to Thompson Sampler for real node selection.

### Phase 3: Optimized UI (Stable)
*   [ ] Implement pure-black Compose theme.
*   [ ] Create the "Developer Overlay" for real-time cognitive tracking.

---
*Created on: 2026-06-04 | Ronin Kernel Engineering Standards*
