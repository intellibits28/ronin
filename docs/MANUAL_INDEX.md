# The Ronin Kernel Technical Manual

Welcome to the definitive technical reference for the Ronin Kernel. This manual is designed for developers, Edge AI researchers, and system architects looking to understand the inner workings of this sovereign mobile AI runtime.

## User Guides & Practical References

* **[Ronin User Guide & Manual (အသုံးပြုသူ လမ်းညွှန်)](USER_GUIDE.md)**
  * Comprehensive end-user instructions covering On-Device model setup, Cloud Providers, SHM vibration monitoring, Document Intelligence & OCR, Privacy Shield, device tools, and cognitive memory backup/restore.
* **[Current Architecture Specification](ARCHITECTURE_CURRENT.md)**
  * Process model, `KernelRuntimeContext`, Active Inference Kernel (RAIK), Document Intelligence, Privacy Shield, LiteRT-LM SDK, AIDL IPC, and security governance.
* **[SHM Pipeline v3 Architecture](SHM_PIPELINE_V3_ARCHITECTURE.md)**
  * Zero-padded Welch PSD, Modal Validation Engine v3, and Kalman NIS filter.
* **[Documentation Archive (Historical Proposals & Phase Plans)](archive/README.md)**
  * Historical v1.0 - v2.0 design roadmaps and drafts preserved for reference.

## Technical Deep-Dive Manual

1. [Executive Summary](01_EXECUTIVE_SUMMARY.md)
   * High-level overview of capabilities and target environments.
2. [Architecture Overview](02_ARCHITECTURE_OVERVIEW.md)
   * System boundaries, cognitive loop, and C4 architecture diagrams.
3. [Core Components Deep-Dive](03_CORE_COMPONENTS.md)
   * The Native Memory Manager, SHM Subsystem, and Android UI Shell.
4. [Design Decisions & ADRs](04_DESIGN_DECISIONS.md)
   * Why C++20, Local + Cloud Hybrid approaches, and deterministic math vs. LLMs.
5. [Data Models & Schemas](05_DATA_MODELS.md)
   * `ShmSession` schema, SQLite memory schemas, and AI truncation.
6. [Integration Points & APIs](06_INTEGRATION_POINTS.md)
   * JNI Contracts, AI Provider integrations, and Hardware Sensors.
7. [Performance Characteristics](07_PERFORMANCE.md)
   * SHM Benchmarking, inference latency, and Android memory pressure management.
8. [Security & Privacy Model](08_SECURITY.md)
   * Data masking, API Key management, and C++ Sandboxing.
9. [Appendices](09_APPENDICES.md)
   * Glossary, Developer Setup, and Troubleshooting.

---
*Note: For user-facing app setup, commands, and disaster recovery steps, see [USER_GUIDE.md](USER_GUIDE.md).*
