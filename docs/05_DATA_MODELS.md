# 5. Data Models & Schemas

## `ShmSession` Schema
The `ShmSession` is the single source of truth for the Structural Health Monitoring pipeline. It is serialized into an Engineering JSON format.

Key properties include:
*   `sessionId`: Unique UUID for tracking.
*   `deviceProfile`: Phone model, OS version.
*   `sensorMetadata`: Sampling rate, noise floor limits.
*   `dspResult`: Vibration energy, dominant mechanical axis.
*   `features`: The extracted `baseline_f0` and frequency candidates.
*   `decision`: The final state (e.g., "HEALTHY", "STRUCTURAL_RESONANCE").
*   `reasoningTrace`: Step-by-step logs of how the DSP engine reached its conclusion.

## Memory Schemas
Memory entries in the SQLite database follow a strict schema allowing for temporal queries and semantic weighting. Key fields include:
*   `id`: Primary key.
*   `semantic_weight`: Float indicating the importance of the memory (used for decay).
*   `timestamp`: Epoch creation time.
*   `payload`: The actual text or binary blob of the memory.

## Integration Payloads (AI Truncation)
When sending a `ShmSession` to a Cloud AI, Ronin strips the raw float arrays from the payload. This optimization ensures we do not exceed the context window or token limits of models like Gemma 4 E2B or Gemini 2.5 Flash, providing only the necessary semantic `features` and `dspResult`.
