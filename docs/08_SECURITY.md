# 8. Security & Privacy Model

## Data Masking
The `ShmAiReviewPipeline` enforces strict data masking. By default, raw sensor arrays are completely filtered out of the JSON payloads before they are routed to any Cloud provider, ensuring sensitive location or mechanical fingerprinting data is not leaked.

## API Key Management
API keys for OpenRouter, OpenAI, or Gemini are stored locally. They are never logged to the native telemetry console and are dynamically injected into HTTPS request headers just-in-time.

## Sandboxing
The Native C++ kernel executes within the standard Android application sandbox. It cannot access files outside its designated data directories unless explicitly granted URI permissions via the Kotlin intent bridge. This prevents rogue AI plans from modifying critical system files.
