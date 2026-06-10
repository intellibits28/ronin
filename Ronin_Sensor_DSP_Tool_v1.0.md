# Ronin Sensor DSP & Tool Calling Specification v1.0
*(Non-SHM, Event-Driven Batch Processing Architecture)*

## 1. Architectural Philosophy
To achieve maximum RAM and Thermal efficiency on Mi 11 Lite 5G NE, the legacy Shared Memory (SHM) streaming loop is completely deprecated. Sensor data processing follows the **"Collect → C++ DSP → On-Demand Tool JSON"** execution pattern. This aligns with Ronin Memory Model v2.1's "Derive More, Store Less" principle by treating sensor analytics as transient tool outputs rather than persistent streams.

## 2. DSP Engine Execution Flow (C++ Layer)
*   **Library:** PFFFT (Pretty Fast FFT) with native ARM NEON SIMD optimization.
*   **Background Thread:** Collects raw accelerometer/gyroscope signals during seismic events or periodic intervals into a local isolated buffer (`std::vector<float>`).
*   **Pre-processing:** DC offset removal and 4th order Butterworth IIR filtering.
*   **DSP Processing:** C++ executes FFT and Power Spectral Density (PSD) analysis using Welch's Method (Hann windowing + segment averaging) natively on the collected batch using optimized NEON intrinsics.
*   **Output State:** The analysis is compressed into a structured C++ Struct, ready to be serialized into a minimal JSON string (< 500 bytes).
*   **Thread Safety:** All DSP state access is protected by `std::shared_mutex` to allow concurrent reads from Gemma Tool Calls while blocking only during batch updates.

## 3. Gemma 4 Tool Schema Definition
Gemma 4 is authorized to invoke the sensor module using the following JSON Schema when high-level hardware analytics are required.

```json
{
  "name": "get_sensor_analysis",
  "description": "Retrieves the latest native DSP analysis for structural health, resonance frequency, and seismic anomalies.",
  "parameters": {
    "type": "object",
    "properties": {
      "sensor_type": {
        "type": "string",
        "enum": ["accelerometer", "gyroscope", "all"],
        "description": "The specific hardware sensor array to query."
      }
    },
    "required": ["sensor_type"]
  }
}
```

## 4. C++ Native Interface Contract
Tool Calling Bridge မှတဆင့် ခေါ်ယူမည့် Native Function Signature နှင့် Return Format။

### Function Signature
```cpp
namespace ronin::sensor {
    // Returns JSON string directly; empty string on error
    std::string get_sensor_analysis(const std::string& sensor_type);
}
```

### Standard Response Format
```json
{
  "resonance_freq_hz": 12.45,
  "psd_peak_db": -34.2,
  "noise_floor_db": -78.5,
  "anomaly_detected": false,
  "sample_count": 1024,
  "timestamp_ms": 1716408000000
}
```

### Error Response Format
```json
{
  "error": "SENSOR_UNAVAILABLE",
  "message": "Accelerometer not responding or permission denied",
  "timestamp_ms": 1716408000000
}
```

## 5. Integration with Memory Model v2.1
Sensor Analysis Results ကို Long-term Memory အဖြစ် သိမ်းဆည်းရန် Strategy။

| Scenario | Memory Action | State | Rationale |
| :--- | :--- | :--- | :--- |
| Normal Reading | ❌ Do Not Store | N/A | Transient data; derive on-demand only |
| Anomaly Detected | ✅ Store Summary | Cold (1) | "Resonance spike at 12.45Hz detected" → FTS5 Indexed |
| User Asks About Past | ✅ Promote on Recall | Active (0) | If recalled > 2 times → KV Cache Resident |
| Privacy Purge | 🗑️ Tombstone | Tombstoned (4) | Immediate deletion per v2.1 §3 |

> ⚠️ **Critical Rule:** Raw sensor buffers are NEVER stored in SQLite. Only human-readable DSP summaries generated via Gemma Reflection are persisted as memories.

## 6. Hardware Guard-rails (Mi 11 Lite 5G NE)
*   **Thermal Throttle:** DSP batch processing pauses if device temperature > 42°C (read via `/sys/class/thermal/thermal_zone0/temp`).
*   **Battery Awareness:** DSP collection interval doubles when battery < 20% or charging disabled.
*   **Cancellation Support:** Background collection thread checks `std::atomic<bool> cancel_flag` every 10ms; responds to user message arrival within < 50ms.
*   **Memory Cap:** Local DSP buffer hard-limited to 2MB max; older samples dropped FIFO if exceeded.

## 7. Migration Notes from SHM Architecture
*   Remove all `mmap()`, `shm_open()`, and ring buffer synchronization primitives.
*   Delete `SensorStreamingWorker` class; replace with `SensorBatchCollector`.
*   JNI layer no longer requires file descriptor passing; simple string return via `NewStringUTF()`.
*   GPU Delegate remains exclusive to Gemma 4 inference; DSP runs CPU-only (NEON).

*   **Status:** Phase 2 (Hardened Native DSP) Complete.

---
**Version:** 1.0  
**Target Device:** Xiaomi Mi 11 Lite 5G NE (Snapdragon 778G)  
**Dependencies:** Ronin Memory Model v2.1, LiteRT GPU Delegate, NEON Intrinsics  
**Status:** In Progress (Phase 3 Active)
