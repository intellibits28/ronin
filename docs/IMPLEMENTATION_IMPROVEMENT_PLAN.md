# Implementation Improvement Plan

This plan turns the current architecture review into staged implementation work. It is designed for Termux + Codex CLI development, with GitHub Actions handling full builds.

## Principles

- Keep changes small enough to review on-device.
- Prefer docs and tests before broad refactors.
- Keep Android behavior stable while native ownership is cleaned up.
- Let GitHub Actions be the full build authority.
- Do not mix unrelated implementation changes into the same commit.

## Phase 0: Documentation Realignment

Status: in progress.

Tasks:

- Archive legacy docs that conflict with current source into `old_logs_and_context/`.
- Maintain current docs in `docs/`.
- Make `docs/ARCHITECTURE_CURRENT.md` the active source of truth.
- Update README links to distinguish current docs from legacy context.

Acceptance:

- README points to current docs.
- Legacy single-process/direct-JNI claims are not presented as current behavior.
- Current docs mention Termux development and GitHub Actions build authority.

## Phase 1: JNI Binding Split

Goal: reduce `src/ronin_jni.cpp` size and risk.

Status: in progress. The first slice moved memory, history, file indexing, file search, Myanmar dictionary, prediction storage, and human-feedback JNI bindings into `src/jni/jni_memory_bindings.cpp`.

Proposed files:

- `src/jni/jni_registration.cpp`
- `src/jni/jni_runtime_bindings.cpp`
- `src/jni/jni_memory_bindings.cpp`
- `src/jni/jni_execution_bindings.cpp`
- `src/jni/jni_sensor_bindings.cpp`
- `src/jni/jni_model_bindings.cpp`

Approach:

1. Add a small internal header for shared JNI binding declarations.
2. Move one group at a time without behavior changes.
3. Update `CMakeLists.txt` after each move.
4. Use GitHub Actions host/Android build as validation.

Acceptance:

- All current registered native methods remain registered.
- No Kotlin external method signatures change.
- CI host tests and APK build pass.

## Phase 2: KernelRuntimeContext

Goal: replace scattered global runtime ownership with explicit lifecycle state.

Proposed object:

```cpp
class KernelRuntimeContext {
public:
    bool initializeMainProcess(...);
    bool initializeWorkerProcess(...);
    void shutdown();
    bool isInitialized() const;

    RoninKernel* kernel();
    IntentEngine* intentEngine();
    LongTermMemory* longTermMemory();
    GraphExecutor* graphExecutor();
};
```

Approach:

1. Introduce the class without moving behavior.
2. Store existing global objects inside the context.
3. Convert JNI functions to access `runtimeContext()`.
4. Add lifecycle smoke tests where possible.

Acceptance:

- Reinitialization does not leak stale global state.
- Main-process and worker-process initialization paths are explicit.
- Shutdown order is deterministic.

## Phase 3: SQLite Migrations

Goal: make memory schema evolution safe.

Tasks:

- Add `schema_version`.
- Convert ad hoc legacy facts cleanup into migration `N -> N+1`.
- Add FTS insert/update/delete triggers for notes and episodes.
- Add tests for new DB initialization and legacy DB upgrade.

Acceptance:

- Existing user DBs migrate forward without dropping unrelated data.
- FTS search remains correct after insert, update, and delete.
- CI host tests cover migration paths.

## Phase 4: Typed Bridge Results

Goal: replace fragile `"Error: ..."` string handling with typed results.

Targets:

- Inference hydration result.
- Inference execution result.
- Capability execution result.
- Cloud provider result.
- Cancellation/timeout result.

Approach:

- Define compact JSON result envelopes at JNI/AIDL boundaries.
- Convert Kotlin internals to sealed result types where practical.
- Keep UI text formatting at the UI edge.

Acceptance:

- Planner and UI can distinguish timeout, cancellation, hydration failure, model missing, service disconnected, and policy denial.
- Existing user-visible behavior remains understandable.

## Phase 5: CapabilityPolicyEngine

Goal: centralize sensitive action rules.

Policy fields:

- capability ID/name
- Android permission requirements
- human confirmation requirement
- audit requirement
- risk level
- offline/cloud allowance
- vault or sensitive fact access rule

Acceptance:

- SMS, contacts, calendar, vault, file access, location, and sensors pass through one policy decision point.
- Denied actions emit telemetry and audit records.
- HITL denial updates failure telemetry and graph confidence consistently.

## Phase 6: Structured Observability

Goal: make multi-step agent failures traceable.

Required fields:

- `session_id`
- `exec_id`
- `corr_id`
- `node_id`
- `capability`
- `latency_ms`
- `outcome`
- `failure_type`

Acceptance:

- Logs and stored episodes can be correlated.
- GitHub Actions test failures include enough context to identify failing subsystem.

## Suggested Commit Order

1. `docs(architecture): realign current implementation docs`
2. `refactor(jni): split memory bindings`
3. `refactor(jni): split execution and model bindings`
4. `refactor(runtime): introduce kernel runtime context`
5. `feat(memory): add schema version migrations`
6. `test(memory): cover migration and fts triggers`
7. `feat(policy): centralize capability policy`
8. `chore(logging): add structured execution fields`
