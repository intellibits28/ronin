# Main Branch Logic Restoration & Phase 4.5 Alignment Plan

## 1. Cloud Logic Restoration (CloudProviderRegistry)
**Objective:** Restore the dynamic `CloudProviderRegistry` supporting custom endpoints and manual API keys, specifically OpenRouter logic.
**Implementation Steps:**
*   Extend the `CloudProvider` data class to include `isCustom: Boolean = false`.
*   In `MainActivity.kt`, restore the UI allowing users to add custom providers (Endpoint, API Key, Model Name) in the `SettingsDialog`.
*   Update `NativeEngine.kt`'s `executeSingleInference` to correctly format requests for custom endpoints (like OpenRouter which expects standard OpenAI API format) rather than hardcoded Gemini logic.
*   Store custom providers securely using `EncryptedSharedPreferences` or update `providers.json`.

## 2. Model Registry UI Fix
**Objective:** Correctly display model names and prevent duplicates.
**Implementation Steps:**
*   Update `scanLocalModels()` in `MainActivity.kt` to ensure only `.litertlm` and `.bin` files are added to the `discoveredModels` list.
*   In `SettingsDialog`, use `java.io.File(path).name` to extract and display just the filename instead of the full path.
*   Ensure the list is distinct (no duplicate paths).

## 3. Intent-based Hardware Control
**Objective:** Use Android 13+ standard Settings Intent Panels for Bluetooth and WiFi toggles to improve privacy and user experience.
**Implementation Steps:**
*   In `setupHardwareCallbacks` within `MainActivity.kt`, modify cases `6` (WiFi) and `7` (Bluetooth).
*   Instead of programmatic toggles, use `startActivity(Intent(Settings.Panel.ACTION_WIFI))` and `startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS))`.
*   Ensure proper `Intent.FLAG_ACTIVITY_NEW_TASK` is set since it's called from a context that might not be the foreground activity in some paths.

## 4. IPC Hydration Stabilization
**Objective:** Fix model hydration failures in the `:inference_core` process.
**Implementation Steps:**
*   Ensure the `path` passed via AIDL to `InferenceService` is an absolute, readable path accessible by the isolated process. The `:inference_core` process shares the same UID but might have different `filesDir` mappings depending on how it's launched. We will ensure the path is absolute.
*   Implement a callback or state flow so that when `loadModel` succeeds in the service, the UI `chatViewModel.isKernelHydrated = true` is explicitly updated, making the radio button turn green automatically.

## 5. UI/UX & Gemma 4 Optimization
**Objective:** Refine the Cloud Profile UI and optimize Gemma 4 response quality.
**Implementation Steps:**
*   Implement a Radio Button list for Cloud Profiles to allow switching between different API providers/models easily.
*   Implement post-processing in `InferenceService.kt` to strip internal tokens (like `turn|user`, `turn|model`) that leak into output.
*   Aggressively halt low-priority background tasks during local inference to maximize RAM availability for Gemma 4.

## 6. Verification
*   Verify that custom OpenRouter/Gemini API endpoints work with the new Profile Registry.
*   Verify model names are clean in the UI and turn green on load.
*   Verify WiFi/Bluetooth toggles open the correct system panels.
*   Verify that internal tokens are stripped from local LLM responses.
