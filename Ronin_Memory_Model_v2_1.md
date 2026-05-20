# 🧠 Ronin Kernel: Cognitive Memory Model Specification (v2.1)

## ၁။ နိဒါန်းနှင့် ဒဿန (Core Philosophy)
Ronin ၏ Memory စနစ်သည် **"Derive More, Store Less"** နှင့် **"Cognitive Suppression"** မူဝါဒအပေါ် အခြေခံသည်။ အချက်အလက်အားလုံးကို အမြဲတမ်း သိမ်းဆည်းရန် မဟုတ်ဘဲ၊ လိုအပ်ချိန်တွင်သာ "အမှတ်ရမှု" (Retrieval) ကို အဆင့်ဆင့် လုပ်ဆောင်ပေးမည့် စနစ်ဖြစ်သည်။

---

## ၂။ Memory Schema (SQLite Structure)
*Multilingual-E5 Strategy: Native Burmese support enabled. Translation tier removed.*

```sql
CREATE TABLE memories (
    id INTEGER PRIMARY KEY,
    original_text_mm TEXT,        -- မူရင်း မြန်မာစာသား (Ground Truth / NFKC Normalized)
    segmented_text_mm TEXT,       -- FTS5 Search အတွက် Tokenized လုပ်ထားသော စာသား
    embedding_vector BLOB,        -- 384-dim Float32 Vector (Multilingual-E5-Small)
    embedding_provider TEXT,      -- "e5-small-v1" (Model versioning for re-indexing)
    importance_score REAL,        -- အခြေခံ အရေးကြီးမှု (0.0 - 1.0)
    recall_count INTEGER DEFAULT 0,
    creation_time INTEGER,        -- Unix Timestamp
    last_accessed_time INTEGER,   -- နောက်ဆုံး အမှတ်ရခဲ့သည့်အချိန်
    state_enum INTEGER            -- Memory State (0-4: Active to Tombstoned)
);
```

---

## ၃။ Memory States & Lifecycle (Refined)
Memory တစ်ခုချင်းစီ၏ "ရှာဖွေနိုင်စွမ်း" (Searchability) ကို State ပေါ်မူတည်၍ ခွဲခြားသည်။

| State | ENUM | definition | Retrieval Priority |
| :--- | :--- | :--- | :--- |
| **Active** | 0 | Working Memory (Cache-hot) | High |
| **Cold** | 1 | မကြာသေးမီက သုံးထားသော Storage data | Medium |
| **Archived** | 2 | **Compressed but Searchable.** အနှစ်ချုပ်ထားသော ဗဟုသုတများ။ | Low |
| **Forgotten** | 3 | **Stored but Non-searchable.** Retrieval noise လျှော့ချရန် ဖုံးကွယ်ထားခြင်း (Suppression)။ | Fallback Only |
| **Tombstoned** | 4 | **Scheduled for Destruction.** Privacy Purge အတွက် သေမိန့်ပေးထားခြင်း။ | None |

---

## ၄။ Retrieval Modes & Orchestration
စွမ်းဆောင်ရည်နှင့် User Experience ကို မျှတစေရန် Retrieval ကို (၄) မျိုး ခွဲခြားထားသည်။

| Mode | Trigger | Search Scope | Description |
| :--- | :--- | :--- | :--- |
| **Fast Recall** | Normal Chat | Active + Cold | အမြန်ဆုံး အဖြေထုတ်ပေးရန်။ |
| **Deep Recall** | Low Confidence | Partial Forgotten | Standard search မှာ အဖြေမထွက်ပါက အတိတ်ကို လှမ်းစမ်းကြည့်ခြင်း။ |
| **Explicit Recall** | User Request | Full Forgotten | "ဟိုတုန်းက မှတ်မိလား" ဟု မေးပါက Scan အပြည့်ဖတ်ခြင်း။ (Intentional Slowness ပါဝင်မည်)။ |
| **Reflection** | Idle Task | Archived + Forgotten | စက်နားချိန်တွင် Data patterns များ ရှာဖွေပြီး အနှစ်ချုပ်ထုတ်ခြင်း။ |

---

## ၅။ Resurrection Logic (မှတ်ဉာဏ် ပြန်လည်နှိုးဆော်ခြင်း)
`Forgotten` memory များကို အောက်ပါအတိုင်း စနစ်တကျ ပြန်လည်အသုံးပြုမည်။

1.  **Confidence Fallback:** Standard search ရဲ့ Similarity score နည်းနေပါက `Forgotten` storage ကို FTS5 (Keyword) ဖြင့် နောက်ကွယ်မှ scan ဖတ်မည်။
2.  **Psychological Interface:** Explicit Recall လုပ်ပါက Personality Layer မှ "စဉ်းစားနေသည်" ဟု အသိပေးပြီး စက္ကန့်အနည်းငယ် ဆိုင်းငံ့မည်။
3.  **Temporary Resurrection:** ရှာတွေ့လာသော memory ကို Context ထဲတွင် ဧည့်သည်အဖြစ် ခေတ္တသုံးမည်။ ထပ်ခါတလဲလဲ သုံးပါက `Cold` သို့ ပြန်မြှင့်တင် (Promote) မည်။

---

## ၆။ Hardware & Thermal Optimization
Mi 11 Lite နှင့် Snapdragon 778G+ Hardware များအတွက် Guard-rails များ ထည့်သွင်းထားသည်။

- **Low Priority Chunked Scan:** `Forgotten` scan ဖတ်ရာတွင် CPU ဝန်မပိစေရန် thread priority ကို လျှော့ချပြီး data များကို chunk အလိုက် ခွဲဖတ်မည်။
- **Cancellable Worker:** User က အကြောင်းအရာ ပြောင်းသွားပါက background scan ကို ချက်ချင်း ရပ်ဆိုင်းမည်။
- **Vector Quantization:** 384-dimensions အား Memory footprint လျှော့ချရန်အတွက် Storage ထဲတွင် Float16 ဖြင့် သိမ်းဆည်းရန် စဉ်းစားနိုင်သည်။

---

**Ronin Kernel Development Team**
*Version: 2.1 (E5-Small & Multilingual Update)*
