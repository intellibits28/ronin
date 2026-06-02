# 🧠 Ronin Agent Mode Architecture: 10-Layer Systems Engineering (v7.0)

## Vision
Transform Ronin from a conversational chatbot into an **Industrial-grade Personal Agent Runtime**. This architecture utilizes a layered systems engineering approach to isolate hardware complexities, ensure user safety via State Machines, and optimize workflows using probabilistic learning.

---

## 🏗️ The 10-Layer Stack

### Layer 1: Core Contracts
- Defines the fundamental types and communication protocols.
- **Components:** `capability_types.h`, `capability_request.h`, `capability_response.h`.

### Layer 2: Capability Dispatcher
- The kernel's syscall router. Maps requests to the appropriate communication bridge.
- **Components:** `CapabilityDispatcher` class.

### Layer 3: JNI Message Bridge (JSON Only)
- A decoupled, JSON-based bridge between C++ and Kotlin.
- **Workflow:** Request (C++) -> JSON -> Kotlin Driver -> Result -> JSON -> Response (C++).

### Layer 4: Android Driver Layer
- Isolated Android API implementations.
- **Drivers:** `LocationDriver` (FusedLocation), `SmsDriver` (SmsManager).

### Layer 5: Intent → Capability Mapping
- LLM-driven classification. Maps user goals (e.g., "Where am I?") to system capabilities (`LOCATION`).

### Layer 6: Confirmation FSM (Human-in-the-Loop)
- A state machine that intercepts sensitive actions and waits for user approval.
- **States:** `ASK_CONFIRMATION`, `WAITING_PERMISSION`, `EXECUTE`, `COMPLETED`.

### Layer 7: Session Manager
- Manages long-running agentic tasks and ensures state persistence across turns.

### Layer 8: Scheduler
- Priority-based task execution (e.g., SMS takes priority over background DSP sensor analysis).

### Layer 9: Resource Manager
- Handles hardware resource locking (e.g., preventing two sessions from accessing the GPS simultaneously).

### Layer 10: Workflow Optimizer (GraphExecutor Reborn)
- Uses **Thompson Sampling** to select the most efficient execution path (e.g., which sensor filter yields the best result based on historical success).

---

## 🚀 Implementation Roadmap (Phased)

### Phase 1: MVP (Layers 1 - 5)
- **Goal:** Execute `LOCATION` and `SMS` tasks end-to-to-end.
- **Success Criteria:** User asks "Where am I?" -> Map opens. User asks "Send location to X" -> SMS sent.

### Phase 2: Orchestration (Layers 6 - 9)
- **Goal:** Add HITL safety and task scheduling.

### Phase 3: Intelligence (Layer 10)
- **Goal:** Re-integrate Thompson Sampling for workflow optimization and Sensor/DSP analysis.
