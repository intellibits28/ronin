# 8. Security & Privacy Model

## Data Masking
The `ShmAiReviewPipeline` enforces strict data masking. By default, raw sensor arrays are completely filtered out of the JSON payloads before they are routed to any Cloud provider, ensuring sensitive location or mechanical fingerprinting data is not leaked.

## API Key Management
API keys for OpenRouter, OpenAI, or Gemini are stored locally. They are never logged to the native telemetry console and are dynamically injected into HTTPS request headers just-in-time.

## Sandboxing
The Native C++ kernel executes within the standard Android application sandbox. It cannot access files outside its designated data directories unless explicitly granted URI permissions via the Kotlin intent bridge. This prevents rogue AI plans from modifying critical system files.

## Capability Authorization & Lifecycle Governance
The `CapabilityDispatcher` implements strict policy enforcement using `CapabilityPolicyEngine`. Before any tool action is executed (e.g., SMS, Calendar), the request is validated against a statically loaded `policy_manifest.json`. Unlisted capabilities are denied by default. Additionally, all execution contexts are wrapped with guaranteed RAII cleanup to prevent budget leaks, and the `AgentScheduler` strictly propagates cancellation tokens to gracefully terminate tasks upon user cancellation or timeouts.

## Strict Safe Mode
When `Safe Mode` is toggled, all write operations to the `LongTermMemory` (including standard facts, episodes, and the secure vault) are immediately blocked. This provides an instant "read-only" lock for the entire cognitive state, ensuring data immutability during high-risk scenarios.
