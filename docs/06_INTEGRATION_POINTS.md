# 6. Integration Points & APIs

## JNI Contract
The Kotlin and C++ boundary is managed through explicit JNI calls mapped in `NativeEngine.kt` and `ronin_jni.cpp`.
*   **Kotlin -> C++**: Sending world-state updates, chat messages, or triggering evaluations.
*   **C++ -> Kotlin**: Firing capability intents (e.g., `sendEmail()`, `readSensor()`) and emitting telemetry for the Reasoning Console.

## AI Provider Integrations
Ronin abstracts the AI provider interface, allowing seamless switching:
*   **Local Gemma**: Accessed via Edge inference workers.
*   **Gemini / OpenAI / OpenRouter**: Accessed via standard REST HTTPS calls. The `SettingsSection` in the UI allows dynamic override of API keys and endpoint URLs.

## Hardware Sensors
The Android `SensorManager` is used to collect raw `TYPE_ACCELEROMETER` data. This data is batched into arrays of floats and passed synchronously to the native `vibe_monitor.cpp` engine for real-time evaluation.
