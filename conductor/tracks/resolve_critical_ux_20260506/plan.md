# Implementation Plan - Resolve Critical UX Regressions and Stabilize IPC Hydration

## Phase 1: Stabilization of Boot & Permissions [checkpoint: e35692d]
- [x] Task: Re-implement comprehensive 1st-run permission requests (WiFi, BT, GPS, All Files). ef44bdb
    - [ ] Update `MainActivity.kt` to request all necessary runtime permissions upon first launch.
    - [ ] Implement special handling for `MANAGE_EXTERNAL_STORAGE` on Android 11+.
- [x] Task: Fix asset deployment logic to ensure skills are available on 1st run. 1c2dc6f
    - [ ] Update `copyAssetsToFilesDir` to correctly deploy `capabilities.json` and model files to the paths expected by the C++ Kernel.
- [x] Task: Conductor - User Manual Verification 'Phase 1: Stabilization of Boot & Permissions' (Protocol in workflow.md) e35692d

## Phase 2: UI Restoration & Command Intelligence [checkpoint: 3999b53]
- [x] Task: Restore and refine the floating command suggester UI. 12df5fa
    - [ ] Re-implement the `/` suggester to float above the input field within the `Scaffold`.
    - [ ] Ensure suggestions are filtered in real-time as the user types.
- [x] Task: Implement 'Kernel Ready' notification system. 5ab0aad
    - [ ] Add a Toast or UI indicator that appears once the native neural bridge is fully active.
- [x] Task: Add explicit `/search` and `/find` commands to the intent engine. f33803d
    - [ ] Update `src/intent_engine.cpp` and `src/capabilities/file_search_node.cpp` to handle explicit search prefixes.
- [x] Task: Conductor - User Manual Verification 'Phase 2: UI Restoration & Command Intelligence' (Protocol in workflow.md) 3999b53

## Phase 3: Cloud & Persistence Layer Fixes
- [ ] Task: Resolve Cloud 404 errors with v1/v1beta auto-fallback.
    - [ ] Update `NativeEngine.kt` to attempt `v1` endpoint if `v1beta` fails (and vice versa) for Gemini models.
- [ ] Task: Fix model selection persistence on app restart.
    - [ ] Implement a wait mechanism in `loadModel` to ensure the Inference Service is bound before attempting hydration.
    - [ ] Ensure `primary_cloud_provider` and `local_model_path` are correctly saved and restored from `SharedPreferences`.
- [ ] Task: Conductor - User Manual Verification 'Phase 3: Cloud & Persistence Layer Fixes' (Protocol in workflow.md)

## Phase 4: Optimization & Verification
- [ ] Task: Increase context tokens to 1024 to prevent truncated responses.
    - [ ] Update `InferenceService.kt` to set `maxTokens` to 1024 in the `LlmInference` builder.
- [ ] Task: Optimize local inference response speed.
    - [ ] Profile and refine the `stopLowPriorityTasks()` overhead to ensure maximum responsiveness.
- [ ] Task: Final comprehensive verification of all modular skills and cloud fallback.
- [ ] Task: Conductor - User Manual Verification 'Phase 4: Optimization & Verification' (Protocol in workflow.md)
