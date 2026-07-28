# 3. Core Components Deep-Dive

## The Native Memory Manager (`src/long_term_memory.cpp`)
Memory is not treated as a flat text file in Ronin. The SQLite-backed memory engine supports:
*   **Tiering**: Short-term (working context) vs. Long-term (persisted facts).
*   **Ebbinghaus Decay**: Memories that are not accessed frequently gradually lose semantic weight and are pruned to prevent database bloat.
*   **FTS5 Search**: Highly efficient lexical searching with Myanmar segmentation support for rapid recall during the Retrieve phase.

## The SHM Subsystem (`src/dsp/vibe_monitor.cpp`)
The Structural Health Monitoring (SHM) engine is a flagship capability of the Ronin Kernel.
*   **Industrial Modal Validation Engine v3**: Processes 100Hz accelerometer batches.
*   **Welch PSD & Detrending**: Uses a sub-windowed Welch method for precise Power Spectral Density estimation, stripping out low-frequency noise (high-pass cutoff at 0.5Hz).
*   **Outlier Gate Hysteresis**: A state machine that stabilizes the extraction of Modal Frequencies (F₀) over time, ensuring sudden impacts don't corrupt the baseline health metric.

## The Android UI Shell (`RoninUIComponents.kt`)
The UI is built using Jetpack Compose, emphasizing modularity. Key components include:
*   **ReasoningConsole**: Displays real-time cognitive trace logs from the native kernel.
*   **SystemStatusCard**: Visualizes RAM, Battery, and AI Provider status.
*   **SHM Canvas Charts**: Directly plots structural frequency peaks (`ShmSession.top_candidates`) on a custom canvas without heavy external charting libraries.
