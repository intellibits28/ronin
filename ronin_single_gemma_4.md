---

### 📋 Ronin Kernel v2.1: Gemma 4 Single-Model Migration Brief

#### ၁။ Core Architectural Shift
*   **Decision:** Multilingual-E5-Small Embedding Model ကို လုံးဝ ဖယ်ရှားပြီး **Gemma 4 E2B-it Only** Architecture သို့ ကူးပြောင်းမည်။
*   **Rationale:** Mi 11 Lite 5G NE (6/8GB RAM) တွင် E5+Gemma တွဲသုံးပါက LMK Kill Risk များခြင်း၊ E5 TFLite Conversion (`[1,1]` shape/Gather Op) Error ကြောင့် KV Cache Reset ဖြစ်ခြင်းနှင့် Empty/Garbage Response များ ထွက်ပေါ်နေခြင်း။
*   **Philosophy Alignment:** v2.1 ၏ "Derive More, Store Less" ဒဿနအတိုင်း Dedicated Vector DB အစား Gemma 4 ၏ In-context Reasoning နှင့် FTS5 Keyword Search ကို ပေါင်းစပ်အသုံးပြုမည်။

#### ၂။ Critical Technical Fixes (Mandatory Implementation)

**A. FTS5 Ghost Data Prevention (External Content Table Safety)**
`state_enum` Update လုပ်သည့်အခါ FTS5 Index တွင် Ghost Data မကျန်ရစ်စေရန် အောက်ပါ Trigger Pattern ကို Strictly Apply လုပ်ရမည်-
```sql
CREATE TRIGGER IF NOT EXISTS memories_au AFTER UPDATE ON memories BEGIN
    INSERT INTO memories_fts(memories_fts, rowid, segmented_text_mm, translated_text_en)
    VALUES ('delete', old.id, old.segmented_text_mm, old.translated_text_en);
    INSERT INTO memories_fts(rowid, segmented_text_mm, translated_text_en)
    VALUES (new.id, new.segmented_text_mm, new.translated_text_en);
END;
```
*Note:* Version Upgrade တိုင်းတွင် `INSERT INTO memories_fts(memories_fts) VALUES('rebuild');` ကို Migration Step အဖြစ် ထည့်သွင်းရမည်။

**B. Token-Level Cancellation (Instant Responsiveness)**
Background Reflection/Summarization Loop ကို Function Boundary တွင်သာ မဟုတ်ဘဲ **Native Inference Stream ၏ Token Generation Loop အတွင်း** တွင် `std::atomic<bool>` flag ဖြင့် Per-Token Check လုပ်ပြီး ချက်ချင်း Abort လုပ်နိုင်ရမည်။
*   *Target:* User Message ဝင်လာပါက Summarization Latency < 50ms အတွင်း ရပ်တန့်နိုင်ရမည်။
*   *Integration:* SHM Polling Loop နှင့် LiteRT Interpreter Callback ထဲသို့ Cancel Flag Pointer Pass လုပ်ရမည်။

**C. Context-Safe Resurrection Logic (Gemini CLI Recommendation)**
Forgotten Memories များကို Gemma 4 Self-Reflection Prompt ထဲသို့ ထည့်သွင်းရာတွင် Context Window Overflow မဖြစ်စေရန်-
*   **Top-K Limit:** BM25 Score အမြင့်ဆုံး **Max 3 Chunks** ကိုသာ ယူမည်။
*   **Token Budget:** Chunk တစ်ခုလျှင် Max 256 Tokens၊ စုစုပေါင်း Reflection Context အတွက် Max 800 Tokens ကို Hard Limit သတ်မှတ်မည်။
*   **Prompt Template:** `[THINK]` Tag အတွင်း Step-by-step Relevance Evaluation လုပ်စေပြီး မသက်ဆိုင်ပါက Ignore လုပ်စေမည်။

#### ၃။ Schema Adaptation (E5 Removal)
*   `embedding_vector BLOB` Column ကို Nullable/Optional ပြောင်းလဲမည် (သို့မဟုတ် ဖယ်ရှားမည်)။
*   `segmented_text_mm` ပေါ်တွင် FTS5 Index ကို Rebuild လုပ်မည် (Myanmar Word Segmentation ကို C++ Layer တွင် ပြုလုပ်ပြီးမှ Insert လုပ်မည်)။
*   GPU Delegate (LITERT_CL) ကို Gemma 4 Inference အတွက်သာ Exclusive Allocate လုပ်မည်။

#### ၄။ Expected Outcomes
*   ✅ KV Cache Reset ပြဿနာ ပြေလည်ပြီး Conversation Length တိုးတက်မည်။
*   ✅ RAM Usage ~800MB-1GB လျော့ကျပြီး LMK Kill Risk နည်းပါးသွားမည်။
*   ✅ Retrieval Latency (FTS5) 0ms နီးပါး ဖြစ်လာမည်။
*   ✅ UX Responsiveness (Cancellation) ချက်ချင်း တုံ့ပြန်နိုင်မည်။

---
