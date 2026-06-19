# Current Memory Model

This document replaces the legacy `memories`/`memories_fts` memory model description. The old document is archived at `old_logs_and_context/MEMORY_MODEL_V2_legacy.md`.

## Overview

Ronin uses local SQLite as the authoritative long-term memory store. The current implementation is lexical-first: it uses direct structured lookup for facts and FTS5 search for notes, episodes, and files. Myanmar segmentation support is provided by the native `MyanmarSegmenter`, but the current database schema is organized by memory tier rather than a single `memories` table.

## Database Owner

Native implementation:

- `include/long_term_memory.h`
- `src/long_term_memory.cpp`

Runtime initialization:

- `src/ronin_jni.cpp` creates `LongTermMemory(base_path + "/ronin_cognitive.db")`.
- `LongTermMemory::initSchema()` creates the active schema.

## Active Tables

Current schema:

```sql
CREATE TABLE IF NOT EXISTS notes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT,
    content TEXT,
    tags TEXT,
    created_at INTEGER,
    updated_at INTEGER
);

CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts
USING fts5(title, content, content='notes', content_rowid='id');

CREATE TABLE IF NOT EXISTS facts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    entity TEXT,
    attribute TEXT,
    value TEXT,
    source_type INTEGER DEFAULT 0,
    confidence REAL DEFAULT 1.0,
    last_verified_at INTEGER,
    created_at INTEGER,
    updated_at INTEGER
);

CREATE INDEX IF NOT EXISTS idx_facts_lookup ON facts(entity, attribute);

CREATE TABLE IF NOT EXISTS vault (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT,
    encrypted_blob TEXT,
    created_at INTEGER
);

CREATE TABLE IF NOT EXISTS episodes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER,
    intent TEXT,
    goal_id TEXT,
    node_id TEXT,
    summary TEXT,
    payload_json TEXT,
    outcome_enum INTEGER,
    latency_ms INTEGER,
    confidence_before REAL,
    confidence_after REAL
);

CREATE VIRTUAL TABLE IF NOT EXISTS episodes_fts
USING fts5(summary, content='episodes', content_rowid='id');

CREATE TABLE IF NOT EXISTS predictions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER,
    goal_id TEXT,
    node_id TEXT,
    predicted_json TEXT,
    actual_json TEXT,
    error_score REAL
);

CREATE TABLE IF NOT EXISTS chat_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    role TEXT,
    content TEXT,
    timestamp INTEGER
);

CREATE VIRTUAL TABLE IF NOT EXISTS file_index
USING fts5(name, path, extension, last_modified UNINDEXED);

CREATE TABLE IF NOT EXISTS audit (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    action TEXT,
    details TEXT,
    timestamp INTEGER
);

CREATE TABLE IF NOT EXISTS failures (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    node_id TEXT,
    failure_type INTEGER,
    timestamp INTEGER,
    retry_count INTEGER,
    resolution TEXT
);
```

## Current Memory APIs

Exposed through JNI and Kotlin `NativeEngine`:

- `storeNote`, `searchNotes`
- `storeFact`, `lookupFact`
- `storeVault`, `lookupVault`
- `storePrediction`
- `searchEpisodes`
- `indexFiles`, `searchFiles`
- `getChatHistory`, `clearHistory` through native command/runtime paths
- failure storage and failure lookup for runtime healing

## Current Gaps

- There is no explicit `schema_version` table.
- FTS triggers currently cover insert synchronization for notes and episodes, but update/delete trigger coverage should be expanded and tested.
- Legacy table cleanup exists for old `facts.key` shape, but migration behavior is not generalized.
- Vault encryption is provided by Android-side `SecurityProvider`; native storage currently stores encrypted blobs and does not enforce biometric policy itself.
- Memory tier policy exists across code paths but is not yet centralized as one classifier/policy module.

## Improvement Targets

1. Add a `schema_version` table and incremental migrations.
2. Add migration tests for legacy facts and FTS rebuild behavior.
3. Add FTS update/delete triggers for notes and episodes.
4. Define one memory classification entry point for NOTE, FACT, VAULT, EPISODE, and PREDICTION.
5. Keep Myanmar segmentation dictionary loading explicit and testable.
6. Ensure vault lookup and sensitive fact access route through centralized policy and audit.
