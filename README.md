# Ronin Kernel (v5.1.3 — Research-Grade SHM & Sovereign Tactical Upgrade)

[![Ronin Kernel Build & Test](https://github.com/intellibits28/ronin/actions/workflows/build.yml/badge.svg)](https://github.com/intellibits28/ronin/actions/workflows/build.yml)
[![Ronin Isolated Inference CI](https://github.com/intellibits28/ronin/actions/workflows/ronin-ci.yml/badge.svg)](https://github.com/intellibits28/ronin/actions/workflows/ronin-ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-emerald.svg)](LICENSE)

> **📥 Download Latest Android APK / နောက်ဆုံးထွက် APK ဒေါင်းလုဒ်ရယူရန်:**  
> 👉 **[Download from GitHub Latest Release (`https://github.com/intellibits28/ronin/releases/latest`)](https://github.com/intellibits28/ronin/releases/latest)**  
> 📖 **[User Guide & Manual (အသုံးပြုသူ လမ်းညွှန် - English / မြန်မာ)](docs/USER_GUIDE.md)** • **[v5.1.3 Release Notes](RELEASE_NOTES_v5.1.3.md)** • **[v5.1.1 Release Notes](RELEASE_NOTES_v5.1.1.md)** • **[Changelog](CHANGELOG.md)**

---

## 🌐 Overview (English) | စနစ်မိတ်ဆက် (မြန်မာ)

### 🇬🇧 English Overview
**Ronin** is a sovereign, privacy-first mobile AI runtime and cognitive agent for Android. It combines a **C++20 native microkernel**, Kotlin/JNI integration, isolated on-device **LiteRT-LM `0.19.1` (`Gemma 4 E2B` & multimodal `EmbeddingGemma 2`)** inference, a **SQLite FTS5 + 16-byte aligned SIMD Vector Store**, and hardware/tool capabilities. Ronin is engineered for autonomous on-device intelligence: observe device state, minimize Variational Free Energy (RAIK), retrieve hybrid lexical/semantic document and cross-modal media memory, plan multi-step actions, execute capabilities with Human-In-The-Loop (HITL) governance, and analyze physical structural integrity via its **Structural Health Monitoring (SHM v3.5)** DSP subsystem.

### 🇲🇲 မြန်မာဘာသာဖြင့် မိတ်ဆက်
**Ronin Kernel** သည် အင်တာနက်မလိုဘဲ ဖုန်းထဲတွင် သီးသန့် လုံခြုံစွာ အလုပ်လုပ်နိုင်သော **On-Device Sovereign AI Agent** နှင့် **အဆောက်အအုံ ကြံ့ခိုင်မှု တိုင်းတာရေး (SHM) Runtime** ဖြစ်ပါသည်။ C++20 Native Cognitive Microkernel နှင့် Android (Kotlin/Jetpack Compose) တို့ကို တိုက်ရိုက် ပေါင်းစပ်ထားပြီး Google LiteRT-LM `0.19.1` (`Gemma 4` နှင့် Multimodal `EmbeddingGemma 2`) မော်ဒယ်များကို ဖုန်းထဲတွင် Offline အပြည့်အဝ သုံးစွဲနိုင်သကဲ့သို့၊ လိုအပ်ပါက Cloud AI (Gemini, OpenAI, OpenRouter) များနှင့်လည်း ချိတ်ဆက်အသုံးပြုနိုင်ပါသည်။

---

## 🚀 Key Capabilities (v5.1.1 Sovereign Tactical Upgrade) | အဓိက စွမ်းဆောင်ရည်များ

| Feature / စွမ်းဆောင်ရည် | English Description | မြန်မာဘာသာ ရှင်းလင်းချက် |
| :--- | :--- | :--- |
| **🎨 Tactical Brutalist UI & Myanmar Typography** | AMOLED pure black (`#0A0A0A`) theme, unified `8.dp` card geometry, diacritic-safe `21.sp` Myanmar dual-font system, and dismissible `/` command overlay. | AMOLED ဘက်ထရီချွေတာသည့် အနက်ရောင် ဒီဇိုင်း၊ မြန်မာစာ အပေါ်တင်/အောက်မြစ် မပြတ်တောက်သော `21.sp` စာလုံးစနစ်နှင့် အလွယ်တကူ ပိတ်နိုင်သော `/` Command list။ |
| **📚 Hybrid RRF Document RAG (`/docsearch`)** | `EmbeddingGemma 2` (`270M` Text-Only, `740M` Omnimodal) with official asymmetric query/document prompts, SQLite FTS5 BM25 + 768D SIMD Cosine Similarity fused via RRF ($k=60$), and `MyanmarSegmenter` fallback. | စာအုပ်နှင့် PDF စာတမ်းကြီးများကို `EmbeddingGemma 2` (Query/Document prompt prefixes) နှင့် SQLite FTS5 ပေါင်းစပ်ထားသော Hybrid RRF စနစ်ဖြင့် အပိုဒ်လိုက် တိကျစွာ ရှာဖွေပေးခြင်း။ |
| **📸 Multimodal Media Search (`/mediaindex`, `/mediasearch`)** | Cross-modal photo, 16 kHz mono WAV audio, and keyframe/audio video search powered by LiteRT-LM `0.19.1` + `EmbeddingGemma 2 740M`, SQLite `media_embeddings`, reverse visual lookup (`📎`), and timestamp seeking. | ဖုန်းထဲရှိ ဓာတ်ပုံ၊ အသံဖိုင်နှင့် ဗီဒီယိုအခိုက်အတန့်များကို မြန်မာ/အင်္ဂလိပ် စာသား (သို့) ပုံတူရှာဖွေမှုဖြင့် Offline ရှာဖွေပြီး သက်ဆိုင်ရာ Timestamp သို့ တိုက်ရိုက်ဖွင့်ကြည့်နိုင်ခြင်း။ |
| **📇 Smart OCR Business Card & Contact Flow (`/contact`)** | Mobile screenshot preprocessing (`OcrImagePreprocessor`), bilingual card parsing (`ContactCardParser`), existing-contact matching, biometric Privacy Shield PII unlock, and biometric contact save/update. | ဓာတ်ပုံ/Screenshot OCR မှ နာမည်၊ ဖုန်းနံပါတ်များ၊ Email နှင့် ကုမ္ပဏီကို ထုတ်ယူကာ လက်ဗွေအတည်ပြုချက်ဖြင့် New Contact ထည့်သွင်းခြင်း (သို့) ရှိပြီးသား Contact ထဲ Update လုပ်ခြင်း။ |
| **🔎 Privacy-Aware Local Research (`/research`)** | Queries DuckDuckGo Lite via HTTP `POST` (256 KB stream cap, SSRF DNS-pinning) and synthesizes locally via **Gemma 4 E2B** with `[1][2][3]` citations & **"SAVE TO VAULT"**. | ပြင်ပ Paid API မလိုဘဲ DuckDuckGo Lite မှ ရှာဖွေကာ ဖုန်းတွင်းရှိ Gemma 4 ဖြင့် 100% Local အနှစ်ချုပ်ထုတ်ပေးပြီး Source Citations `[1][2]` များဖြင့် ပြသပေးခြင်း။ |
| **🔌 Hardened Termux MCP Bridge (`/mcp`)** | Loopback `127.0.0.1:8765` bridge secured with HMAC-SHA256, `±30s` timestamp window, 16-byte Nonce LRU anti-replay cache, and strict Capability Policy (`ALLOW`/`CONFIRM`/`DENY`). | Termux ရှိ Developer Tools များနှင့် HMAC-SHA256 + Nonce Anti-Replay စနစ်ဖြင့် လုံခြုံစွာ ချိတ်ဆက်ပြီး ဖိုင်စစ်ဆေးခြင်းနှင့် Git လုပ်ငန်းများ ဆောင်ရွက်နိုင်ခြင်း။ |
| **🏗️ Structural Health Monitoring (SHM v3.5)** | 100Hz 3-axis accelerometer ingestion, 64-bit IEEE 754 biquad filter, Median+3*MAD Welch segment impulse gating, Nakamura H/V spectral ratio sway validation, 4-factor Quality Gate (`RESEARCH_GRADE` to `UNRELIABLE`), Anomaly-First baseline protection, Eurocode 8/ASCE 7 building priors, and AI structural review. | 64-bit Biquad Filter နှင့် Median+3*MAD Impulse Gating ဖြင့် ခြေသံ/ဆူညံသံများကို ဖယ်ရှားကာ Nakamura H/V Spectral Ratio ဖြင့် အဆောက်အအုံ ယိမ်းခါမှု (Sway) နှင့် ကြမ်းခင်းတုန်ခါမှု (Bounce) ကို ခွဲခြားပြီး 4-Factor Research-Grade Quality Gate စနစ်ဖြင့် အင်ဂျင်နီယာအဆင့် တိကျစွာ တိုင်းတာစစ်ဆေးပေးခြင်း။ |
| **🧠 Active Inference Kernel (RAIK v2)** | Unified Free Energy Principle ($0.81\ \mu\text{s}$ hot-loop) across **SHM Gaze Gating**, **Dual-Gate BM25 Smart RAG Wakeup**, **Privacy-Weighted Brain Routing**, **Multi-Hop Research Foraging**, **32-Slot MCP Anomaly Gate**, **Surprise-Driven Reflection**, and **Lock-Free Audio Kalman DSP**. | Free Energy Principle (FEP) ဖြင့် SHM အာရုံခံကြိမ်နှုန်းသာမက `/docsearch`, Brain Router, `/research`, `/mcp`, `/reflect` နှင့် Audio DSP တို့ကိုပါ သင်္ချာမော်ဒယ်တစ်ခုတည်းဖြင့် ထိန်းကျောင်းပေးခြင်း။ |
| **🛡️ Privacy Shield & `.roninvault` Backup** | On-device PII redaction (Myanmar NRC, phone, email, bank cards) and atomic WAL-checkpointed SQLite backups encrypted with **AES-256-GCM** via Android SAF. | မြန်မာမှတ်ပုံတင် (NRC)၊ ဖုန်းနံပါတ်နှင့် ဘဏ်ကတ်များကို အလိုအလျောက် ဖုံးကွယ်ပေးခြင်းနှင့် မှတ်ဉာဏ် Database ကို AES-256-GCM ဖြင့် ကုဒ်ဝှက်၍ `.roninvault` အဖြစ် သိမ်းဆည်းခြင်း။ |
| **🔐 Multi-Centroid Semantic Router & Vault Safety** | Cascaded intent classification across 12 intents with 3-register sub-clusters (`EN`, `MM`, `MIX`), L2 re-normalization, top-2 margin ambiguity guard ($S_1 - S_2 \ge 0.04$), non-destructive stateful updates (`PendingSlotState`), and zero-credential regex redaction (`[REDACTED_SECRET]`). | ဘာသာစကားအလိုက် Sub-Centroid စနစ်ဖြင့် မြန်မာ/အင်္ဂလိပ် မေးခွန်းများကို တိကျစွာ ခွဲခြားခြင်း၊ မပြည့်စုံသော update တွင် Vault entry မပျက်စီးစေဘဲ စကားပြောဖြည့်စွက်နိုင်ခြင်းနှင့် API Key များကို DevHUD/Logs များတွင် အလိုအလျောက် ဖုံးကွယ်ပေးခြင်း။ |

---

## 📲 Quick Start & APK Installation | စတင်ထည့်သွင်းအသုံးပြုနည်း

1. **Download APK / APK ဒေါင်းလုဒ်ရယူရန်**:
   - Visit **[GitHub Latest Release (`https://github.com/intellibits28/ronin/releases/latest`)](https://github.com/intellibits28/ronin/releases/latest)** and download `ronin-kernel-*.apk`.
   - [GitHub Latest Release](https://github.com/intellibits28/ronin/releases/latest) သို့ ဝင်ရောက်၍ `ronin-kernel-*.apk` ကို ဒေါင်းလုဒ်ဆွဲပြီး Install (သို့မဟုတ် ရှိပြီးသားအပေါ်မှ တိုက်ရိုက် Update) ပြုလုပ်ပါ။
2. **Load Local Brain or Cloud Provider / မော်ဒယ် ချိတ်ဆက်ရန်**:
   - **Local On-Device Mode**: Open **Menu (☰) > Settings > Section 4: Local Brain** and tap **Import Brain (`.litertlm`)** to load `Gemma 4 E2B` or `EmbeddingGemma 2` models.
   - **Cloud Mode**: Open **Settings > Section 3: Cloud Providers** to configure Gemini, OpenAI, OpenRouter, or custom OpenAI-compatible endpoints.
3. **Full User Guide / အသေးစိတ် လမ်းညွှန်**:
   - Read the complete bilingual guide in **[`docs/USER_GUIDE.md`](docs/USER_GUIDE.md)**.

---

## ⚡ Slash Commands Reference | အမြန်သုံး Command များ

| Command | English Function | မြန်မာဘာသာ လုပ်ဆောင်ချက် |
| :--- | :--- | :--- |
| `/help`, `/capabilities` | Show available capabilities and tools | သုံးနိုင်သော စွမ်းဆောင်ရည်နှင့် Tool များ စာရင်းကြည့်ရန် |
| `/status` | Inspect kernel health, thermal state, and RAM | စနစ်၏ ကျန်းမာရေး၊ အပူချိန်နှင့် RAM အခြေအနေ စစ်ဆေးရန် |
| `/research <query>` | Run privacy-aware web research with local Gemma 4 synthesis | DuckDuckGo Lite + Local Gemma 4 ဖြင့် ဝဘ်သုတေသန ပြုလုပ်ရန် |
| `/docsearch <path> <query>` | Hybrid RRF (FTS5 + 768D SIMD Vector) in-document search | စာရွက်စာတမ်းကြီးများထဲမှ အပိုဒ်များကို Hybrid RRF ဖြင့် ရှာဖွေရန် |
| `/mediaindex [all\|photos\|audio\|video\|status\|clear]` | Index phone Photos, Audio & Videos with `EmbeddingGemma 2 740M` | ဖုန်းတွင်းရှိ ဓာတ်ပုံ၊ အသံနှင့် ဗီဒီယိုများကို Index ပြုလုပ်ရန် |
| `/mediasearch [photo\|audio\|video] <query>` | Cross-modal photo/audio/video moment search & `--image <path>` | ဓာတ်ပုံ၊ အသံနှင့် ဗီဒီယိုအခိုက်အတန့်များကို စာသား/ပုံဖြင့် ရှာဖွေရန် |
| `/contact [info]` | Parse business card / inline text & save/update contact | Business Card / စာသားမှ Contact အသစ်ထည့်/ပြင်ဆင်ရန် |
| `/summarize <path>` | Sliding-window document summarization | သတ်မှတ်ထားသော ဖိုင်ကို AI ဖြင့် အနှစ်ချုပ်ခိုင်းရန် |
| `/files <keyword>` | Fast SQLite FTS5 file search with `FileResultCard` | ဖုန်းထဲရှိ ဖိုင်များကို အမြန်ရှာဖွေပြီး ကတ်ဖြင့် ပြသရန် |
| `/mcp status\|token\|tools` | Manage and inspect authenticated Termux MCP Bridge | Termux MCP Server ချိတ်ဆက်မှု၊ Token နှင့် Tool စာရင်း စစ်ဆေးရန် |
| `/mcp call <tool> <json>` | Execute Termux MCP tool with HITL confirmation | Termux MCP Tool ကို အတည်ပြုချက်ဖြင့် လှမ်းခေါ်အသုံးပြုရန် |
| `/model`, `/skills` | Inspect active `.litertlm` model and capability nodes | လက်ရှိ သုံးနေသော Model နှင့် Native Skill များကို စစ်ဆေးရန် |
| `/reset`, `/reflect` | Reset KV cache/conversation or trigger reflection | စကားပြောမှတ်တမ်း အသစ်ပြန်စရန် သို့မဟုတ် Reflection run ရန် |

---

## 🏗️ Current Architecture | ဗိသုကာ တည်ဆောက်ပုံ

At runtime, the Android application initializes the native C++20 kernel through `NativeEngine` (`KernelRuntimeContext`) and binds an isolated `InferenceService` running in the `:inference_core` process. User input flows from Jetpack Compose into JNI (`JniExecutionGateway`), through the Active Inference & Intent layers, and either executes a policy-governed skill or invokes the LiteRT-LM reasoning spine.

```mermaid
flowchart LR
    UI["Compose UI\n(MainActivity / RoninUIComponents)"] --> Engine["NativeEngine (Kotlin)\nIPC Sub-Batching & Media Drivers"]
    Engine <-->|AIDL Stream & Paths| Worker["InferenceService (:inference_core)\nLiteRT-LM 0.19.1 (Gemma 4 / EmbeddingGemma 2)"]
    Engine <-->|JNI Bridge| Kernel["C++20 Native Kernel (libronin_kernel.so)\nRAIK + IntentEngine + GraphExecutor"]
    Kernel <--> LTM["SQLite + FTS5 + SIMD Vector Store\n(document_embeddings / media_embeddings / .roninvault)"]
    Engine <-->|HMAC-SHA256 + Nonce| MCP["Termux MCP Server\n(127.0.0.1:8765)"]
```

### Source Map

| Path | Purpose |
| :--- | :--- |
| `src/ronin_kernel.cpp`, `include/ronin_kernel.hpp` | Top-level observe/orient/decide/act kernel loop and world-state hooks. |
| `src/ronin_jni.cpp`, `src/jni/jni_memory_bindings.cpp`, `src/jni/jni_registration.cpp` | Modular JNI registration, memory/document/media vector bindings, governance, and lifecycle hooks. |
| `src/intent_engine.cpp`, `include/intent_engine.h` | Intent classification, slash commands, task planning, skill registry, and thread-safe cloud/model routing. |
| `src/long_term_memory.cpp`, `include/long_term_memory.h` | SQLite v3 persistence, FTS5 sync triggers (`ai`/`ad`/`au`), `document_embeddings` Hybrid RRF, and `media_embeddings` 16-byte aligned SIMD cosine search. |
| `src/capability_policy_engine.cpp`, `assets/capabilities.json` | Centralized capability security policy enforcement and manifest governance (including `ContactsNode` & `MediaSearchNode`). |
| `src/reasoning/`, `include/reasoning/` | Active Inference Kernel (RAIK), Variational Free Energy ($F$), and Expected Free Energy ($G(\pi)$) router. |
| `src/dsp/vibe_monitor.cpp`, `src/dsp/resonance_analyzer.cpp` | Real-time sensor DSP analysis, Modal Validation Engine v3, Welch PSD, and Kalman NIS tracking. |
| `android/app/src/main/kotlin/com/ronin/kernel/MainActivity.kt` | Compose UI host, SAF encrypted `.roninvault` backup/restore, media/contact handlers, model/traineddata import sanitization. |
| `android/app/src/main/kotlin/com/ronin/kernel/RoninUIComponents.kt` | Tactical Brutalist UI cards (`8.dp`), Myanmar-safe typography, `ResearchResultCard`, `FileResultCard`, and dismissible command overlay. |
| `android/app/src/main/kotlin/com/ronin/kernel/MediaSearchEngine.kt`, `MediaSearchUiComponents.kt` | Photo/Audio/Video discovery, EXIF resize, `MediaExtractor`/`MediaCodec` 16 kHz mono WAV windowing, adaptive video frames, `MediaSearchResultListCard`, and `MediaMomentPlayerDialog`. |
| `android/app/src/main/kotlin/com/ronin/kernel/OcrImagePreprocessor.kt`, `ContactCardParser.kt`, `ContactManager.kt`, `ContactUiComponents.kt` | Mobile screenshot preprocessing, bilingual business card extraction, duplicate matching, PII biometric unlock, and biometric contact save/update. |
| `android/app/src/main/kotlin/com/ronin/kernel/LocalResearchEngine.kt` | DuckDuckGo Lite POST client, 256 KB stream cap, SSRF multi-hop DNS-pinning, and local citation synthesis. |
| `android/app/src/main/kotlin/com/ronin/kernel/McpClientManager.kt`, `scripts/termux_mcp_server.py` | Authenticated Termux MCP client and server with HMAC-SHA256, timestamp, and 16-byte Nonce LRU anti-replay. |
| `android/app/src/main/kotlin/com/ronin/kernel/DocumentIntelligence.kt`, `OcrEngine.kt`, `PrivacyShield.kt`, `PrivacyVault.kt` | Task-prefixed document chunking, 16-chunk IPC sub-batching, Tesseract 5.x bilingual OCR, PII redaction, and AES-256-GCM encryption. |

---

## 🛠️ Build & Test | တည်ဆောက်ခြင်းနှင့် စမ်းသပ်စစ်ဆေးခြင်း

### Host Native Build & C++ Tests (19/19 Suites)
```bash
cmake -S . -B build_host -DCMAKE_BUILD_TYPE=Debug
cmake --build build_host -j2
cd build_host && ctest --output-on-failure
```

### Android APK Build & JVM Unit Tests
```bash
cd android
gradle testDebugUnitTest
gradle assembleDebug
```

---

## 📚 Documentation Index | စာရွက်စာတမ်း လမ်းညွှန်များ

* **[`docs/USER_GUIDE.md`](docs/USER_GUIDE.md)**: Complete Bilingual User Guide & Operations Manual (အသုံးပြုသူ လမ်းညွှန် အပြည့်အစုံ).
* **[`RELEASE_NOTES_v5.1.3.md`](RELEASE_NOTES_v5.1.3.md)**: Executive Release Notes for Ronin v5.1.3 Research-Grade SHM & Quality Gate Upgrade.
* **[`RELEASE_NOTES_v5.1.1.md`](RELEASE_NOTES_v5.1.1.md)**: Executive Release Notes for Ronin v5.1.1 Sovereign Tactical Upgrade.
* **[`CHANGELOG.md`](CHANGELOG.md)**: Chronological version history and release changelog.
* **[`docs/MANUAL_INDEX.md`](docs/MANUAL_INDEX.md)**: Master documentation index and deep-dive technical links.
* **[`docs/ARCHITECTURE_CURRENT.md`](docs/ARCHITECTURE_CURRENT.md)**: Production runtime architecture, RAIK math, Hybrid RAG, and security model.
* **[`docs/TECHNICAL_SPECS.md`](docs/TECHNICAL_SPECS.md)**: Native core, Android layer, SQLite v3 schemas, and CI specifications.
* **[`docs/SHM_PIPELINE_V3_ARCHITECTURE.md`](docs/SHM_PIPELINE_V3_ARCHITECTURE.md)**: Research-Grade DSP v3.5, Welch PSD, Nakamura H/V ratio, Quality Gate, and SHM AI Review.
* **[`docs/08_SECURITY.md`](docs/08_SECURITY.md)**: Security governance, PII redaction, and vulnerability remediation notes.

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).
