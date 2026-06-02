# 🧠 Ronin Agent Mode Architecture Plan (v7.0 Blueprint)

## Vision
Transform Ronin from a conversational chatbot into an **Autonomous Task Solver** capable of orchestrating multi-step workflows (e.g., fetching location -> resolving contacts -> composing SMS -> requesting user confirmation -> executing). This architecture lays the foundation for future Sensor/DSP integrations (resonance frequency, vibration analysis, etc.).

---

## The 4-Pillar Strategy

### Phase 1: The LLM-driven Task Planner (Gemma 4 as the Brain)
Instead of relying solely on brittle keyword matching, Ronin will use Gemma 4 to dynamically generate execution plans in JSON format.
- **Workflow:** User input -> System Prompt asking for a plan -> LLM outputs a JSON plan.
- **Example Output:**
  ```json
  {
    "intent": "send_sms",
    "dependencies": ["contact_name", "location"],
    "parameters": {"contact_name": "Aung Aung"}
  }
  ```
- **Implementation:** Introduce a `TaskPlanner` within `IntentEngine` to parse and validate these JSON structures.

### Phase 2: Dependency Resolver & Human-in-the-Loop (HITL)
Plans are not executed blindly. A state machine will validate dependencies and ensure safety.
- **Missing Information Resolution:** If a dependency (e.g., a phone number) is missing, execution pauses to prompt the user for clarification.
- **System Permissions:** The Kotlin bridge will handle necessary Android permissions (Contacts, Location, SMS).
- **Safety Confirmation (HITL):** Critical actions (sending SMS, deleting files) trigger a `RequiresConfirmation` state. Execution suspends until the user explicitly clicks "Yes" in the UI.

### Phase 3: Dynamic Tool Chain Builder (GraphExecutor Upgrade)
The static capability graph evolves into a dynamic execution chain.
- **Runtime Connection:** Nodes are linked dynamically based on the LLM's plan (e.g., `LocationNode` -> `SMSNode` or `LocationNode` -> `MapNode`).
- **The Blackboard (Shared Memory):** A shared data structure where nodes read and write payloads. For instance, `LocationNode` writes `{lat, lon}` to the Blackboard, and `SMSNode` consumes it to build the message.

### Phase 4: Thompson Sampling (Execution Routing)
Probabilistic routing is applied to tool selection.
- **Method Optimization:** When multiple paths exist to fulfill a node (e.g., getting location via GPS vs. Network), the `Thompson Sampler` evaluates past success rates to choose the optimal path based on speed, accuracy, or battery efficiency.

---

## Implementation Roadmap

1. **C++ Data Structures:** Update `include/ronin_types.hpp` with `AgentPlan`, `AgentState`, and `Dependency` structs.
2. **The Planner:** Refactor `src/intent_engine.cpp` to include the LLM JSON parser.
3. **Graph & Blackboard:** Upgrade `include/graph_executor.h` to support dynamic chaining and shared payloads.
4. **Kotlin UI & Bridge:** Update `NativeEngine.kt` and `MainActivity.kt` to handle permissions, HITL confirmation dialogs, and specific API integrations (SMS, Contacts).
