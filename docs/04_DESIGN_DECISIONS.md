# 4. Design Decisions & ADRs

## Why C++20 for the Kernel?
While Android apps are traditionally written purely in Java/Kotlin, Ronin uses a native C++20 kernel to achieve:
1.  **Determinism**: Avoid JVM garbage collection pauses during critical cognitive loops or DSP math calculations.
2.  **Portability**: The core reasoning engine can theoretically be ported to embedded Linux devices, drones, or iOS with minimal rewrites.
3.  **Performance**: Vectorized math operations (PFFFT) for Structural Health Monitoring are significantly faster in native code.

## Why Local + Cloud Hybrid AI?
Relying solely on Cloud LLMs introduces latency, network fragility, and privacy concerns. Relying solely on Edge LLMs (like Gemma) limits reasoning capacity due to hardware constraints. Ronin uses a hybrid approach:
*   **Local-First**: Trivial tasks, routing, and basic intent classification stay on-device.
*   **Cloud-Escalation**: Complex tasks (like an SHM Engineering Review) can be escalated to Gemini or OpenRouter APIs when the user requires high-confidence insights.

## State Machine vs. LLM for SHM
Why not just feed raw sensor data to an LLM and ask it if the structure is healthy?
*   **Token Limits**: 10 seconds of 100Hz 3-axis accelerometer data generates massive token overhead.
*   **Hallucination**: LLMs are fundamentally poor at deterministic floating-point math.
*   **Solution**: We use deterministic DSP math (Welch PSD, FFT) in C++ to extract features (F₀, Energy). We *only* feed the extracted telemetry to the LLM for semantic review.
