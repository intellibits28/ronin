🧠 Ronin Kernel - AI Context & Engineering Standards (REVISED v4.6)

## Project Overview
Modular AI agent runtime, Android (Snapdragon 778G+), C++20 + Kotlin-JNI.
Core Philosophy: A sentient, self-improving hardware-aware agent.

## Current Phase: 4.6 (Core Router & Manual Import Strategy)

## ⚠️ CRITICAL ARCHITECTURAL RULES (v2.0 RESET)

### 1. Core Router (E5) Strategy
*   **Engine:** Multilingual-E5 (TFLite) is the ONLY embedding engine. Legacy ONNX is DEPRECATED and REMOVED.
*   **Manual Import:** The 139MB E5 model is NOT bundled in the APK. The app starts in **Setup Mode** (Bootstrap Wizard) until the user manually imports the `.tflite` file.
*   **Integrity:** Native validation must check for `TFL3` magic bytes before hydration.

### 2. SentencePiece Runtime
*   **Tokenizer:** Use SentencePiece Runtime (C++) with `assets/models/sentencepiece.bpe.model`.
*   **Build Optimization:** `SPM_ENABLE_TRAINING=OFF` and `SPM_ENABLE_TRAIN=OFF`. Build ONLY the static runtime.

### 3. JNI Memory Safety (The JNI Guard)
*   **Deep-Copy Requirement:** All JNI strings must be immediately deep-copied to `std::string` using the `GetStringUTFChars` / `ReleaseStringUTFChars` pattern before any file or mmap operations.
*   **Threading:** C++ threads must be strictly asynchronous. Attach/DetachCurrentThread pairing is mandatory.

### 4. Setup Wizard State Machine
*   **States:** `MISSING_CORE`, `IMPORTING`, `VERIFYING`, `ACTIVE`.
*   **Lock Logic:** The UI must remain locked in the Wizard state until the Core Router is verified and the state transitions to `ACTIVE`.

### 5. Build Configuration
*   **Pathing:** Gradle must point to `../../CMakeLists.txt` (Root).
*   **TFLite Linkage (Direct CI Injection):** 
    *   Bypass Prefab for TFLite linkage due to CI metadata inconsistencies.
    *   The CI workflow (`build.yml`) must manually download the TFLite AAR and extract `libtensorflowlite_jni.so` into `android/app/src/main/jniLibs/arm64-v8a/`.
    *   CMake must define an `IMPORTED SHARED` target for `tensorflowlite` pointing to this manual path.
*   **Targets:** Ninja must be forced to build ONLY `ronin_kernel` to ensure sub-5-minute CI success.

### 6. Memory & LMK Guards
*   Adaptive RAM Guard thresholds:
    *   1.0GB for < 800MB free.
    *   1.2GB for < 1.5GB free.
    *   1.5GB for Gemma 4.

## Memory Persistence & Routing Architecture
* **Core Strategy:** Zero-VSS, Ultra-Low RAM Hybrid Search.
* **Implementation:** Combined Text Matching via FTS5 and Semantic Vector Space via Multilingual-E5-small (384-dimensions).
* **Source of Truth:** For full SQLite table structures, schemas, triggers, and native C++ linear search loops, strictly refer to the dedicated artifact:
    👉 **`[Ronin_Memory_Model_v2_1.md](./Ronin_Memory_Model_v2_1.md)`**
* **CLI Constraint:** Never inline the complete schema here. Always modify the dedicated memory model file for any data-tier alterations.

## Audit & Verification Protocol
1.  **Linker Check:** Verify `TFLITE_JNI_LIB` resolves to Play Services TFLite binary.
2.  **Constructor Audit:** `NeuralEmbeddingNode` must have a default constructor for `IntentEngine` compatibility.
3.  **UI Scope:** Use `LocalContext.current` for `filesDir` access in Composables.
4.  **Schema Alignment:** Verify SQLite logic against `Ronin_Memory_Model_v2_1.md`.

### 7. Security Posture: Sovereign Control Mode
*   **Sandboxing (Privilege Isolation):** 
    *   Intent Engine မှ NativeBridge ကို ခေါ်ယူရာတွင် 'Read-only' mode အား Default အဖြစ် ကျင့်သုံးသည်။ 
    *   System-wide change များအတွက် 'Write-Barrier' ပါဝင်ပြီး User ၏ တိုက်ရိုက်ခွင့်ပြုချက် (Explicit Consent) လိုအပ်သည်။
*   **Atomic Shutdown Path:** 
    *   `Kernel::Shutdown()` ခေါ်ယူသည်နှင့် Native memory အားလုံးအား `munmap` လုပ်ခြင်းနှင့် LiteRT Session ဖျက်သိမ်းခြင်းတို့ကို Atomic ဖြစ်စေရမည်။
    *   Shutdown စတင်ချိန်မှစ၍ Inference အသစ်များ လက်ခံခြင်းကို ချက်ချင်းရပ်ဆိုင်းသည်။
*   **Sovereign Override (`FORCE_EXECUTE`):** 
    *   User မှ `FORCE_EXECUTE` keyword အား ထည့်သွင်းအသုံးပြုပါက Safety filtering နှင့် Confidence thresholds အားလုံးကို Bypass လုပ်ခွင့်ရှိသည်။ 
    *   Bypass လုပ်သော်လည်း အဆိုပါ action အား Audit-trail အဖြစ် စနစ်တကျ မှတ်တမ်းတင် သိမ်းဆည်းသည်။
