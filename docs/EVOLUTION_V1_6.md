# 🚀 Ronin Kernel v1.6: Behavioral Evolution Implementation Plan

## Overview
This document outlines the transition from a technically self-healing runtime (v1.5) to a behaviorally evolving autonomous agent (v1.6). The goal is to enable Ronin to learn from semantic failures, user feedback, and long-term reflection.

---

## Phase 1: Semantic Failure & Feedback Integration
**Goal:** Capture not just system crashes, but user dissatisfaction and logical mismatches.

### 1.1 Update Failure Taxonomy
- **Add Types:** `USER_REJECTED`, `HITL_DENIED`, `LOGICAL_MISMATCH`, `CONTEXT_MISMATCH`.
- **Location:** `include/ronin_types.hpp` and `FailureTelemetryBus`.
- **Action:** Allow JNI layer to report these types when a user cancels an action or provides negative feedback.

### 1.2 Reward/Penalty Expansion (RLHF)
- **Mechanism:** Update `ThompsonSampler` weights based on user feedback.
- **Rules:** Positive feedback increases success probability; `USER_REJECTED` applies a heavier penalty than a technical `TIMEOUT`.

---

## Phase 2: Nightly Reflection & Memory Consolidation
**Goal:** Synthesize daily lessons into global policy rules.

### 2.1 Reflection Trigger
- **Mechanism:** Android `WorkManager` triggers `runNightlyReflection()` during idle/charging states.

### 2.2 Episode Synthesis
- **Action:** LLM processes the last 24 hours of `EPISODES` and `FAILURES`.
- **Output:** A natural language or JSON summary of "Lessons Learned" and "Behavioral Adjustments."

### 2.3 Policy Evolution
- **Action:** Update the `policies` table with new constraints (e.g., "User prefers specific tool X for task Y").

---

## Phase 3: Dynamic DAG Mutation & Evolutionary Planning
**Goal:** Allow the agent to invent new recovery strategies instead of following static rules.

### 3.1 Meta-Planner Integration
- **Action:** On terminal failure, send the failed DAG and log to the LLM.
- **Prompt:** "Generate an alternative plan using different tools to achieve goal G."

### 3.2 Speculative Validation
- **Action:** Use `SpeculativeGraphExecutor` to dry-run the new plan before execution.

---

## Phase 4: Context-Aware Policy Engine
**Goal:** Change behavior based on time, location, and system state.

### 4.1 Context Injection
- **Action:** Inject `TIME_OF_DAY` and `LOCATION_CONTEXT` (Home/Work) into the World State.

### 4.2 Adaptive Resource Allocation
- **Action:** Scale execution budgets based on context (e.g., more time for deep reasoning at night).

---

## Phase 5: Macro-Skill Consolidation
**Goal:** Turn frequent multi-step patterns into single specialized tools.

### 5.1 Pattern Mining
- **Action:** Identify sequences of 3+ nodes that frequently succeed together.

### 5.2 Dynamic Capability Registration
- **Action:** Create a virtual "Macro-Node" and register it in the `CapabilityGraph`.

---

## Success Criteria
1. Agent reduces repeated mistakes reported via user feedback.
2. Agent successfully recovers from terminal node failures using novel LLM-generated plans.
3. System stability is maintained under speculative mutation.
