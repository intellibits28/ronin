# Ronin Memory Model v2.1 
*(Revised for Single Gemma 4 E2B-it Architecture)*

## 1. Architectural Overview & Philosophy

The Ronin Memory Model operates on a **Single Gemma 4 (E2B-it) Architecture**. 
* **Core Philosophy:** "Derive More, Store Less." 
* **Deprecation Notice:** The Multilingual-E5-Small embedding model and its associated Vector DB architecture have been completely removed.
* **Storage Strategy:** All representations of `embedding_vector` (BLOB) and `translated_text_en` (TEXT) are permanently eliminated.
* **Search Strategy:** Ronin relies exclusively on ultra-fast, lexical **SQLite FTS5 Search** focusing solely on Myanmar word segmentation strings (`segmented_text_mm`), targeting **0ms search latency** on mobile hardware.

---

## 2. Core SQLite Schema (E5-Free & State-Aware)

The schema is optimized for internal rowid mapping and cognitive state management.

### Base Table: `memories`
```sql
CREATE TABLE IF NOT EXISTS memories (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    raw_text_mm TEXT NOT NULL,
    segmented_text_mm TEXT NOT NULL,
    state_enum INTEGER DEFAULT 0, -- 0:Active, 1:Cold, 2:Archived, 3:Forgotten, 4:Tombstoned
    timestamp INTEGER NOT NULL,
    source TEXT DEFAULT 'user'
);
```

### FTS5 Virtual Table: `memories_fts`
The FTS5 table uses `content_rowid='id'` to map directly to the primary key of the `memories` table for sub-millisecond keyword and BM25 queries.
```sql
CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5(
    segmented_text_mm,
    content='memories',
    content_rowid='id'
);
```

---

## 3. Triggers & FTS5 Ghost Data Prevention

To ensure absolute integrity and prevent "Ghost Data" in the FTS5 index during updates, explicit triggers are defined using the `id` identifier.

### The UPDATE Trigger (`memories_au`)
When a memory state or text is updated, we perform an explicit `delete` followed by a fresh `insert` into the FTS5 index.

```sql
CREATE TRIGGER IF NOT EXISTS memories_au AFTER UPDATE ON memories 
BEGIN
    -- 1. Explicitly DELETE old content from FTS5 index
    INSERT INTO memories_fts (memories_fts, id, segmented_text_mm) 
        VALUES ('delete', old.id, old.segmented_text_mm);
        
    -- 2. Explicitly INSERT new content into FTS5 index
    INSERT INTO memories_fts (id, segmented_text_mm) 
        VALUES (new.id, new.segmented_text_mm);
END;
```

### Supporting Triggers (AI/AD)
```sql
-- Sync on INSERT
CREATE TRIGGER IF NOT EXISTS memories_ai AFTER INSERT ON memories BEGIN
  INSERT INTO memories_fts(id, segmented_text_mm) VALUES (new.id, new.segmented_text_mm);
END;

-- Sync on DELETE
CREATE TRIGGER IF NOT EXISTS memories_ad AFTER DELETE ON memories BEGIN
  INSERT INTO memories_fts(memories_fts, id, segmented_text_mm) VALUES('delete', old.id, old.segmented_text_mm);
END;
```

---

## 4. Context-Safe Resurrection Logic (Token Budget)

When retrieving "Forgotten Memories" (State 3) via BM25, strict context bounds are enforced for Gemma 4.

* **Top-K Chunk Limit:** Return no more than **Top-K = 3** chunks per search.
* **Max Reflection Token Budget:** Total context injected from memories must NOT exceed **800 Tokens**.
* **State Filtering:** Search queries should ideally filter by `state_enum` to prioritize Active (0) or Cold (1) memories before digging into Forgotten (3) ones.

---

## 5. Hardware Guard-rails: Per-Token Cancellation

To ensure system responsiveness, foreground user requests must be able to interrupt background reflection tasks instantly.

* **Atomic Flag Check:** The Native C++ Inference Engine MUST check a `std::atomic<bool>` cancellation flag at the start of every token generation step.
* **Latency Bound:** Background inference MUST abort within **< 50ms** upon receiving a new User Message.
* **Memory Ordering:** Use `std::memory_order_relaxed` for the frequent per-token checks, and `std::memory_order_acquire/release` for the final signal propagation.

---

## 6. Tool Calling Bounds

Gemma 4 is authorized to use Tool Calling for memory management, with a strict safety cap.

* **Tool Depth Restriction:** `MAX_TOOL_CALL_DEPTH` is hardcoded to **1**.
* **Infinite Loop Prevention:** If the model attempts to call a tool recursively or chain multiple tools without a terminal response, the Intent Engine will force a break and return the current state to the user.
