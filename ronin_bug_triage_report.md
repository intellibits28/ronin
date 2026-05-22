
```markdown
# 🔴 BUG TRIAGE REPORT: RONIN KERNEL AI SPINE INFERENCE CORRUPTION

## ၁။ အကျဉ်းချုပ် (Executive Summary)
* **ပြဿနာအမျိုးအစား:** Local LLM Inference State Corruption & Token Looping Bug
* **လက်ရှိသက်ရောက်မှု:** Gemma 4 Model သည် မေးခွန်း (၃) ခုခန့် မေးပြီးပါက Empty Response `💬` များသာ ထုတ်ပေးခြင်း၊ တူညီသော စာသားများအား အဆုံးမရှိ ပတ်ချာလည်ထုတ်နေခြင်း (Word Loop) နှင့် Special Tokens (`<|eot_id|>`) များ Output တွင် ကျွံထွက်လာခြင်း။
* **သက်ရောက်မှုရှိသော Branch:** `dev-recovery-4.8.1` / `feature/hydration-fix`
* **အရေးကြီးမှုအဆင့်:** **P1 - Critical** (Core System Usability ကို ထိခိုက်နေသောကြောင့် Main Branch သို့ Merge ရန် မသင့်သေးပါ)။

---

## ၂။ ပြဿနာ၏ လက္ခဏာရပ်များနှင့် တွေ့ရှိချက်များ (Symptoms)
1. **Infinite Token Looping:** စကားပြောနေစဉ်အတွင်း `(သို့မဟုတ်) လက်သည်း` ကဲ့သို့သော စာသားများကို အထပ်ထပ်အခါခါ ထုတ်ပေးပြီး `StandaloneCoroutine was cancelled` ဖြစ်ကာ Inference ရပ်တန့်သွားခြင်း။
2. **Special Token Leakage:** Model Prompt Template ၏ Raw Syntaxes များဖြစ်သော `<|eot_id|><|start_header_id|>assistant<|end_header_id|>` များ UI ပေါ်သို့ ကျွံထွက်လာခြင်း။
3. **Wiped Chat History but Corrupted Inference:** `/reset` command ကို အသုံးပြု၍ Local SQLite History ကို ဖျက်လိုက်သော်လည်း၊ Empty `💬` နှင့် အဓိပ္ပာယ်မဲ့ Character (`*`, `[`, `1`) များ ဆက်လက်ထွက်ပေါ်နေခြင်း။

---

## ၃။ အခြေခံ အကြောင်းအရင်းများ (Root Cause Analysis)

### Static Context Window Overflow Diagram
```text
[User Prompt 1 + Response 1] ──► 600 Tokens
[User Prompt 2 + Response 2] ──► 1300 Tokens
[User Prompt 3 + Response 3] ──► 2100 Tokens ──► [2048 Max Limit Exceeded] ──► Cache Crash & Word Loop

```
 * **Static KV-Cache Overflow (2K Window):** လက်ရှိ InferenceEngine.cpp တွင် max_sequence_length = 2048 ကို **Static Mode** (kv_cache_config.enable = true) ဖြင့်သာ သုံးထားပြီး၊ Context Window ပြည့်သွားပါက Token ဟောင်းများကို Prune လုပ်ပေးမည့် **Eviction/Rolling Strategy** မရှိခြင်း။
 * **In-Memory Cache Leakage:** /reset ခေါ်ချိန်တွင် Local Storage သာ ပျောက်ပျက်သွားပြီး C++ Layer ၏ LlmInference Memory Pointer အတွင်းရှိ KV-Cache များအား Hard Reset (Re-hydration) မလုပ်ပေးနိုင်ခြင်း။
 * **Missing Stop Tokens Configuration:** Tokenizer အတွင်း Gemma 4 ၏ မျိုးဆက်ပြောင်း လက္ခဏာရပ်များကို တားဆီးရန် Stop Tokens များကို တင်းကျပ်စွာ Masking မလုပ်ထားခြင်း။
## ၄။ အကြံပြုထားသော နည်းပညာဆိုင်ရာ ဖြေရှင်းနည်းများ (Proposed Action Plan)
### A. C++ Configuration Modification (InferenceEngine.cpp)
Static Cache စနစ်မှ Dynamic Sliding Window စနစ်သို့ ပြောင်းလဲရန်နှင့် Stop Tokens များ သတ်မှတ်ရန်။
```cpp
// Update to Sliding Window to avoid 2K Token Crash
options.kv_cache_config.type = KVCacheType::SLIDING_WINDOW;
options.kv_cache_config.rolling_window_size = 1536; // Keep 512 tokens free for output

// Enforce Stop Tokens Guard
options.stop_tokens = {
    "<|eot_id|>", 
    "<|im_end|>", 
    "<|start_header_id|>",
    "\nuser:", 
    "\nassistant:"
};

```
### B. Implementation of Native Reset Logic (ronin_kernel.cpp)
/reset Command ဝင်လာပါက Chat History ဖျက်ရုံတင်မကဘဲ C++ Level ရှိ LLM Instance ကိုပါ Reset ချပေးရန် JNI Bridge အသစ် တည်ဆောက်ခြင်း။
```cpp
extern "C" JNIEXPORT void JNICALL
Java_com_intellibits28_ronin_NativeEngine_nativeResetContext(JNIEnv* env, jobject thiz) {
    if (llm_inference_instance != nullptr) {
        llm_inference_instance.reset(); // Wipe In-Memory Corrupted KV-Cache
        llm_inference_instance = LlmInference::Create(global_options); // Re-hydrate freshly
    }
}

```
### C. Cognitive Memory Alignment (Memory Model v2.1)
Sliding Window ကြောင့် လွတ်ကျသွားသော Context အဟောင်းများကို state_enum = 1 (Cold Memory) အဖြစ် SQLite ထဲသို့ နောက်ကွယ်မှ Auto-push လုပ်ပေးရန်နှင့် လိုအပ်ပါက FTS5 Keyword စနစ်ဖြင့် Prompt ခေါင်းပိုင်းတွင် Summary အဖြစ် Dynamic ပြန်လည် ထည့်သွင်းပေးရန်။
**Report Status:** Pending Approval for implementation on feature/hydration-fix branch.
```
