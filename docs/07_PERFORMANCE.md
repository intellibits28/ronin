# 7. Performance Characteristics

## Benchmarking SHM (Modal Validation)
The v3.0 Industrial Modal Validation Engine is designed for high precision.
*   **Accuracy Target**: < 2% Coefficient of Variation (CV) for the fundamental frequency (F₀) across repeated measurements.
*   **Latency**: The Welch PSD calculation on a 10-second buffer runs in under 15ms on modern ARM Cortex chips, allowing for real-time visualization on the Android UI.

## Inference Latency
*   **Local (Gemma 4 E2B)**: Prompt processing time scales heavily with context size. Extracting telemetry aggressively keeps local response times under 10 seconds.
*   **Cloud (Gemini)**: Typically responds in 1-3 seconds depending on network conditions.

## Memory Pressure Management
Android aggressively kills background apps consuming too much RAM. Ronin monitors OS `onTrimMemory` events. Upon receiving a warning, the native kernel executes a pruning sweep, unloads idle neural network weights, and forces SQLite checkpoints to avoid abrupt termination.
