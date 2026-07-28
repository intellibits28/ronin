# ADR-001: Hybrid AI & Deterministic DSP for Structural Health Monitoring

## Status
Accepted

## Context
The Ronin Kernel requires the ability to evaluate Structural Health Monitoring (SHM) data (e.g., 100Hz accelerometer streams). We need to determine how to extract meaningful structural insights (like resonant frequency F₀) and provide engineering feedback to the user, balancing processing constraints, privacy, latency, and accuracy. 

Feeding raw 100Hz sensor data directly into an LLM is not feasible due to token context window limits, the poor deterministic mathematical abilities of standard LLMs, and significant privacy concerns regarding raw hardware telemetry transmission.

## Decision
We will employ a Hybrid Architecture:
1. **Deterministic DSP Math (Native C++):** All raw sensor data will be processed on-device in the native C++ layer (`vibe_monitor.cpp`). We will use sub-windowed Welch Power Spectral Density (PSD) and Outlier Gate Hysteresis to extract stable structural features (F₀, Energy).
2. **Hybrid AI Review Pipeline (Kotlin/JNI):** The deterministic telemetry (extracted features) will be structured into JSON. We will route this lightweight payload to either a Local Edge Model (Gemma 4 E2B) or a Cloud API (Gemini/OpenRouter) for semantic review.

## Consequences
**Trade-offs & Benefits:**
*   **High Accuracy:** The deterministic math guarantees < 2% Coefficient of Variation (CV) for F₀ extraction.
*   **Privacy & Token Efficiency:** Raw float arrays are masked out; only structured, high-value tokens are sent over the network.
*   **Maintenance Overhead:** Requires maintaining complex DSP algorithms in C++ alongside the AI prompt engineering pipeline in Kotlin.
