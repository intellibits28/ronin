# Track Specification: Resolve Critical UX Regressions and Stabilize IPC Hydration

## 1. Problem Statement
The Ronin Kernel is experiencing several critical user experience regressions and stability issues in its current Phase 4.5 implementation. Key issues include skills being unavailable on the first run, non-functional command suggestions, failing file searches, slow inference responses, and cloud-provider 404 errors.

## 2. Goals
*   **Restore Functional Agency:** Ensure all modular skills (Flashlight, WiFi, BT, GPS) and terminal commands are available immediately upon a fresh install.
*   **Stabilize Remote Inference:** Eliminate Cloud 404 errors through robust endpoint versioning (v1/v1beta fallback) and correct model ID handling.
*   **Improve UX Flow:** Restore command suggester visibility and ensure model selections persist across app restarts.
*   **Enforce Data Sovereignty:** Implement a proactive permission request flow for `MANAGE_EXTERNAL_STORAGE` to enable file search and indexing.

## 3. Scope
*   **Initialization:** Fix asset deployment logic for `capabilities.json` and model files.
*   **UI/UX:** Restore the floating command suggester and implement a 'Kernel Ready' notification system.
*   **Android Layer:** Re-implement the comprehensive runtime permission request flow.
*   **Service Layer:** Stabilize IPC hydration with wait mechanisms for service binding and increased context token limits (1024).

## 4. Technical Constraints
*   **Android Permissions:** Must handle API 30+ scoped storage restrictions via `ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION`.
*   **Binder IPC:** Asynchronous service binding must be synchronized with model loading attempts.
*   **Memory Management:** Continue aggressive RAM freeing during local inference as per established policy.
