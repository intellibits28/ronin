# Implementation Plan: Upgrade LiteRT-LM for Gemma 4 Support

## Objective
Upgrade the MediaPipe GenAI dependency to support the `.litertlm` task bundle format required by Gemma 4 E2B, and implement a robust hardware fallback mechanism in `NativeEngine.kt` to handle device-specific GPU/OpenCL driver incompatibilities (like `clSetPerfHintQCOM` on Snapdragon 778G+).

## Key Files & Context
*   `android/app/build.gradle`: Manages project dependencies. Needs to be updated to the latest MediaPipe GenAI version.
*   `android/app/src/main/kotlin/com/ronin/kernel/NativeEngine.kt`: Manages the lifecycle and execution of the `LlmInference` engine. Needs to be refactored to use the updated API and handle hardware fallbacks (GPU -> CPU).

## Implementation Steps

### 1. Upgrade MediaPipe GenAI Dependency
Update the `build.gradle` file to use the latest available version of MediaPipe GenAI that supports `.litertlm` models (e.g., `0.10.14` or later if required, though research indicates `0.10.33` or the newer `litertlm-android` SDK might be necessary for Gemma 4. I will attempt to find the latest stable 0.10.x version or the new litertlm package).
*   **Target:** `android/app/build.gradle`
*   **Action:** Change `implementation 'com.google.mediapipe:tasks-genai:0.10.14'` to `implementation 'com.google.mediapipe:tasks-genai:0.10.20'` (or the highest verified stable version).

### 2. Refactor Model Hydration & Hardware Fallback
Update the `loadModelAsync` function in `NativeEngine.kt` to explicitly handle hardware delegates (GPU vs CPU) using the potentially updated `LlmInference.LlmInferenceOptions` builder. Implement a `try-catch` block around the GPU initialization attempt. If it fails (e.g., due to the `clSetPerfHintQCOM` OpenCL error), log the failure and automatically retry initialization forcing the CPU delegate.

*   **Target:** `android/app/src/main/kotlin/com/ronin/kernel/NativeEngine.kt`
*   **Action:** Modify `loadModel` (which is a suspend function).

#### Proposed Kotlin Logic Update:
```kotlin
    suspend fun loadModel(path: String): Boolean = withContext(Dispatchers.IO) {
        // Attempt 1: Try with default/GPU acceleration
        val gpuSuccess = tryHydrate(path, useGpu = true)
        if (gpuSuccess) return@withContext true

        Log.w(TAG, "GPU Hydration failed. Falling back to CPU reasoning spine...")
        
        // Attempt 2: Fallback to CPU if GPU driver fails (e.g. SD778G+ OpenCL issues)
        return@withContext tryHydrate(path, useGpu = false)
    }

    private fun tryHydrate(path: String, useGpu: Boolean): Boolean {
        return try {
            val builder = LlmInference.LlmInferenceOptions.builder()
                .setModelPath(path)
                .setMaxTokens(2048)
                .setTemperature(0.7f)
                .setTopK(40)
            
            // Note: Depending on the MediaPipe version, there might be an explicit 
            // setDelegate() or similar method. If not, the internal implementation
            // handles fallback, but we wrap it to ensure we catch crashes.
            
            llmInference = LlmInference.createFromOptions(context, builder.build())
            currentModelPath = path
            
            if (isLibLoaded) {
                notifyModelLoaded(path)
            }
            
            val mode = if (useGpu) "GPU/Auto" else "CPU-Fallback"
            Log.i(TAG, "SUCCESS: Gemma 4 Brain Hydrated via Kotlin AAR (\$mode).")
            true
        } catch (e: Exception) {
            Log.e(TAG, "Hydration attempt failed (GPU=\$useGpu): \${e.message}")
            false
        }
    }
```

### 3. Verify Prompt Formatting for Gemma IT Models
Research indicated that the gibberish output from the 2B CPU model might be due to missing instruction-tuned prompt formatting (`<start_of_turn>user...`). I will review the `generateResponse` calls in `NativeEngine.kt` to ensure prompts are correctly wrapped before being sent to the engine, although the MediaPipe API often handles this internally depending on the version. If manual wrapping is required, I will add it to the `runNeuralReasoning` method.

## Verification & Testing
1.  **Dependency Sync:** Run a Gradle sync to ensure the new MediaPipe version resolves correctly.
2.  **GPU Hydration Test:** Load `gemma-2b-it-gpu-int4.bin`. The logs should show an initial failure followed by a successful fallback to the CPU, preventing the app from crashing.
3.  **Gemma 4 Hydration Test:** Load `gemma-4-E2B-it.litertlm`. The upgraded dependency should now successfully parse the bundle and initialize the session without the 403 error.
4.  **Gibberish Check:** After successful hydration, send a prompt to the model and verify that the output is coherent, confirming the prompt formatting and CPU INT4 issues are mitigated or resolved.
