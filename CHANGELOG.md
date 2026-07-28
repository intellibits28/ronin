# Changelog

All notable changes to the Ronin Kernel project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased / v3.0] - 2026-07-28

### Added
- **Production-Grade SHM AI Review Pipeline**: Export SHM analysis metrics in structured formats (Engineering JSON, Human Report, Developer Debug Logs).
- **SHM Compose Canvas Chart**: Lightweight, custom Canvas visualizer for drawing Structural Health Monitoring frequency peaks directly in the Android UI.
- **AI Review Integrations**: Added support for structural AI reviews using Local Gemma (via Edge inference) or Cloud Gemini/OpenRouter via the `ShmAiReviewPipeline`.
- **Clipboard Copy Support**: Included one-click clipboard copy features for Executive Summaries and AI Reports inside the SHM diagnostic views.
- **Cloud Provider Management UI**: Revamped `SettingsSection` and `ModelPicker` allowing developers to set custom endpoints and input API keys for OpenRouter, OpenAI, and Custom endpoints.
- **Industrial Modal Validation Engine v3**: Implemented Native C++ `vibe_monitor.cpp` updates featuring a sub-window Welch PSD, axis coherence checking, and multi-stage confidence scoring for SHM data.
- **Outlier Gate Hysteresis**: Added state-machine based hysteresis in the Native Decision Engine to effectively manage and stabilize `baseline_f0` shifts over time.
- **SHM Stress Evaluation Harness**: Synthetic data generators for quantitative benchmark reporting of SHM engine accuracy.
- **Architecture Documentation**: Documented the full data flow and system capabilities of the new pipeline inside `docs/SHM_PIPELINE_V3_ARCHITECTURE.md`.

### Changed
- **Modular Android UI Architecture**: Refactored the monolithic `MainActivity.kt` into `RoninUIComponents.kt`, cleanly separating the `ReasoningConsole`, `DeveloperHud`, and `SystemStatusCard`.
- **Dynamic F0 UI Binding**: The `ShmDetailScreen` now dynamically binds `baseline_f0`, `filtered_f0`, and `vibration_energy` directly from live C++ telemetry rather than static mocks.
- **AI LaTeX Parsing**: Extracted outputs from the Local Gemma 4 E2B model are now aggressively parsed (e.g., stripping `$\text{}$` blocks) to render cleanly without external Markdown libraries.
- **Native Telemetry Logs**: C++ Decision Engine trace logs (Layer 1, Layer 2, Modal Validation steps) are passed up cleanly via JNI into the Compose UI in real-time.

### Fixed
- Fixed critical brace cut-offs and UI compilation syntax errors during the UI modularization phase.
- Fixed Cloud provider dynamic model listing returning generic 404 errors by implementing proper `NativeEngine` Endpoint override checking.
- Fixed hardcoded baseline F0 fallback showing `0.1 Hz` constantly in the Android HUD.
- Fixed non-responsive fallback text inside the AI review dialog when local reasoning traces were missing.
