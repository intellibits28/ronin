# Plan: Gemma 4 Inference Bottleneck & Hydration Fixes

## Objective
Resolve the "No Response" bottleneck by shifting `InferenceService.kt` from a synchronous execution block to an asynchronous streaming model using `generateResponseAsync()`. Address memory contention and Rule #6 compliance by removing redundant `mmap` and `mlock` routines in C++ `HydrationManager.cpp`, delegating memory management entirely to the `:inference_core` MediaPipe engine. Inject the requested Burmese Precision Profile (Temperature=0.1) directly into the model's initialization.

## Code Changes (Specific Diffs)

### 1. `android/app/src/main/kotlin/com/ronin/kernel/InferenceService.kt`
- **Shift to Async:** Change `inference.generateResponse()` to `inference.generateResponseAsync()`.
- **SHM Ring Buffer Integration:** Add `.setResultListener` to the `LlmInferenceOptions` builder, which immediately pushes partial tokens to the C++ Shared Memory Ring Buffer via `pushTokenToSHMNative`.
- **Burmese Precision:** Hard-code `.setTemperature(0.1f)` and `.setTopK(40)`.

```kotlin
// In tryHydrate()
        return try {
            val builder = LlmInference.LlmInferenceOptions.builder()
                .setModelPath(path)
                .setMaxTokens(1024)
                .setTemperature(0.1f) // Burmese Precision Profile
                .setTopK(40)
                .setResultListener { partialResult, done ->
                    if (partialResult != null) {
                        pushTokenToSHMNative(partialResult, done)
                    } else if (done) {
                        pushTokenToSHMNative("", true) // Signal completion
                    }
                }
// ... existing delegate logic ...

// In executeReasoning()
        Log.d(TAG, "Executing Reasoning [SafeMode: $isSafeModeActive].")
        
        return try {
            if (isSafeModeActive) {
                Log.w(TAG, "Neural Reasoning under Thermal Stress (Safe Mode Active).")
            }

            // Asynchronous generation: returns immediately, preventing Binder IPC block.
            inference.generateResponseAsync(formattedPrompt)
            "Reasoning Started [SHM Active]"
        } catch (e: Exception) {
            Log.e(TAG, "Inference crash in service: ${e.message}")
            "Error: Neural spine failure - ${e.message}"
        }
```

### 2. `src/models/hydration_manager.cpp`
- **Remove `mmap` Contention:** Strip out the `mmap()`, `mlock()`, and `fstat()` calls. MediaPipe in the worker process handles mapping natively and optimally. 

```cpp
bool HydrationManager::hydrate(const std::string& model_path) {
    dehydrate();

    // Phase 2 Sanitization: Remove whitespace and handle Android symlinks
    std::string sanitized_path = model_path;
    sanitized_path.erase(0, sanitized_path.find_first_not_of(" \t\n\r"));
    sanitized_path.erase(sanitized_path.find_last_not_of(" \t\n\r") + 1);

    // We no longer manually open() or mmap() here. MediaPipe will handle the 
    // memory mapping internally within the :inference_core process to avoid duplicate mapping.
    
    // Fallback sanity check just to ensure file exists
    if (access(sanitized_path.c_str(), R_OK) != 0) {
        LOGE(TAG, "Hydration Error: Cannot access file %s", sanitized_path.c_str());
        return false;
    }

    // Phase 7.0: Pipeline Validation
    if (!verifyChecksum()) return false;
    if (!parseMetadata()) return false;

    return true;
}

void HydrationManager::dehydrate() {
    // No manual unmapping required.
    m_model_size = 0;
}
```

## Postponements & Future Tracks
- **Speculative Decoding:** Postponed to `feature/performance-tuning`.
- **Memory v2.1 Migration:** Migration of SQLite schemas for `state` based logic (ACTIVE, COLD, ARCHIVED, FORGOTTEN) is postponed until this current inference branch is merged and finalized.

## Verification
1.  **Binder Unblocked:** User commands should immediately receive the "Reasoning Started [SHM Active]" response.
2.  **Streaming Active:** Tokens should visibly stream to the UI rather than waiting for full generation.
3.  **RAM Contention Removed:** Memory footprint during hydration should not double.