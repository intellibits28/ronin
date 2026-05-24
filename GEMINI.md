🧠 Ronin Kernel - AI Context & Engineering Standards (REVISED v4.7)

## Project Overview
Modular AI agent runtime, Android (Snapdragon 778G+), C++20 + Kotlin-JNI.
Core Philosophy: A single-spine reasoning kernel.

## Current Phase: 4.7 (Single Gemma 4 Only)

## ⚠️ CRITICAL ARCHITECTURAL RULES (v3.0 RESET)

### 1. Single Reasoning Spine (Gemma 4)
*   **Engine:** Gemma 4 (LiteRT-LM) is the ONLY reasoning engine. 
*   **Vectorless Search:** All legacy embedding models (E5/ONNX) have been REMOVED. Ronin relies purely on SQLite FTS5 for fast keyword retrieval and Gemma 4's In-context Reasoning for semantic understanding.
*   **Zero-SHM:** The legacy Shared Memory ring buffer has been deprecated. Token streaming is handled via direct JNI/Kotlin callbacks.

### 2. SentencePiece Runtime
*   **Tokenizer:** Use SentencePiece Runtime (C++) with `assets/models/sentencepiece.bpe.model`.

### 3. JNI Memory Safety (The JNI Guard)
*   **Deep-Copy Requirement:** All JNI strings must be immediately deep-copied to `std::string` using the `GetStringUTFChars` / `ReleaseStringUTFChars` pattern.
*   **Threading:** C++ threads must be strictly asynchronous. Attach/DetachCurrentThread pairing is mandatory.

### 4. Memory & LMK Guards
*   Adaptive RAM Guard thresholds:
    *   1.0GB for < 800MB free.
    *   1.2GB for < 1.5GB free.
    *   1.5GB for Gemma 4.

## Memory Persistence: Spec v2.1
* **Core Strategy:** Lexical Keyword Search via FTS5.
* **Source of Truth:** Full SQLite table structures and triggers are defined in:
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
