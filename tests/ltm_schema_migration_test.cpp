#include <gtest/gtest.h>
#include "long_term_memory.h"
#include <cstdio>
#include <sqlite3.h>
#include <string>

using Ronin::Kernel::Memory::LongTermMemory;

namespace {

std::string tempDbPath(const char* name) {
    return std::string("./ronin_") + name + ".db";
}

void execSql(sqlite3* db, const char* sql) {
    char* err = nullptr;
    ASSERT_EQ(sqlite3_exec(db, sql, nullptr, nullptr, &err), SQLITE_OK) << (err ? err : "");
    if (err) sqlite3_free(err);
}

int scalarInt(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    EXPECT_EQ(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr), SQLITE_OK);
    int value = -1;
    if (stmt && sqlite3_step(stmt) == SQLITE_ROW) {
        value = sqlite3_column_int(stmt, 0);
    }
    if (stmt) sqlite3_finalize(stmt);
    return value;
}

bool tableExists(sqlite3* db, const char* table) {
    sqlite3_stmt* stmt = nullptr;
    EXPECT_EQ(sqlite3_prepare_v2(
        db,
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?;",
        -1,
        &stmt,
        nullptr), SQLITE_OK);
    sqlite3_bind_text(stmt, 1, table, -1, SQLITE_STATIC);
    bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
}

} // namespace

TEST(LongTermMemorySchemaMigration, ArchivesLegacyFactsAndSetsSchemaVersion) {
    const std::string path = tempDbPath("legacy_facts");
    std::remove(path.c_str());

    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);
    execSql(db, "CREATE TABLE facts (key TEXT PRIMARY KEY, value TEXT);");
    execSql(db, "INSERT INTO facts(key, value) VALUES ('legacy_name', 'Ronin');");
    sqlite3_close(db);

    LongTermMemory ltm(path);
    ASSERT_NE(ltm.getDatabase(), nullptr);

    EXPECT_EQ(scalarInt(ltm.getDatabase(), "SELECT version FROM schema_version LIMIT 1;"), 3);
    EXPECT_TRUE(tableExists(ltm.getDatabase(), "facts_legacy_v0"));
    EXPECT_TRUE(tableExists(ltm.getDatabase(), "facts"));

    EXPECT_TRUE(ltm.storeFact("user", "name", "Ronin"));
    EXPECT_EQ(ltm.lookupFact("user", "name"), "Ronin");

    std::remove(path.c_str());
}

TEST(LongTermMemorySchemaMigration, NotesFtsTracksInsertUpdateAndDelete) {
    LongTermMemory ltm(":memory:");
    ASSERT_TRUE(ltm.storeNote("alpha", "first keyword", "test"));
    EXPECT_FALSE(ltm.searchNotes("keyword").empty());

    execSql(ltm.getDatabase(), "UPDATE notes SET content='second marker' WHERE title='alpha';");
    EXPECT_TRUE(ltm.searchNotes("keyword").empty());
    EXPECT_FALSE(ltm.searchNotes("marker").empty());

    execSql(ltm.getDatabase(), "DELETE FROM notes WHERE title='alpha';");
    EXPECT_TRUE(ltm.searchNotes("marker").empty());
}

TEST(LongTermMemorySchemaMigration, EpisodesFtsTracksInsertUpdateAndDelete) {
    LongTermMemory ltm(":memory:");
    ASSERT_TRUE(ltm.storeEpisode("TEST", "first episode keyword", "{}", true));
    EXPECT_FALSE(ltm.searchEpisodes("keyword").empty());

    execSql(ltm.getDatabase(), "UPDATE episodes SET summary='second episode marker' WHERE intent='TEST';");
    EXPECT_TRUE(ltm.searchEpisodes("keyword").empty());
    EXPECT_FALSE(ltm.searchEpisodes("marker").empty());

    execSql(ltm.getDatabase(), "DELETE FROM episodes WHERE intent='TEST';");
    EXPECT_TRUE(ltm.searchEpisodes("marker").empty());
}

TEST(LongTermMemoryDecay, PrunesLowRetentionFactsAndEpisodes) {
    LongTermMemory ltm(":memory:");
    uint64_t now = 10000000;
    
    // Inferred fact with confidence 0.20, aged 2000000 seconds -> retention < 0.15 -> should be pruned
    using Ronin::Kernel::Memory::SourceType;
    ASSERT_TRUE(ltm.storeFact("env", "noise", "high", SourceType::USER_INFERRED, 0.20f));
    execSql(ltm.getDatabase(), ("UPDATE facts SET last_verified_at = " + std::to_string(now - 2000000) + ";").c_str());
    
    // Explicit user fact aged 2000000 seconds -> retention remains higher -> should NOT be pruned
    ASSERT_TRUE(ltm.storeFact("user", "preference", "dark_mode", SourceType::USER_EXPLICIT, 0.90f));
    execSql(ltm.getDatabase(), ("UPDATE facts SET last_verified_at = " + std::to_string(now - 2000000) + " WHERE entity='user';").c_str());
    
    ltm.applyDecay(now);
    
    EXPECT_EQ(ltm.lookupFact("env", "noise"), "");
    EXPECT_EQ(ltm.lookupFact("user", "preference"), "dark_mode");
}

TEST(LongTermMemoryVectorHybrid, L2NormalizationAndAlignedDotProduct) {
    std::vector<float> v1 = {3.0f, 4.0f, 0.0f, 0.0f};
    LongTermMemory::l2Normalize(v1);
    EXPECT_NEAR(v1[0], 0.6f, 1e-5f);
    EXPECT_NEAR(v1[1], 0.8f, 1e-5f);

    std::vector<float> v2 = {0.6f, 0.8f, 0.0f, 0.0f};
    float dot = LongTermMemory::alignedDotProduct(v1.data(), v2.data(), v1.size());
    EXPECT_NEAR(dot, 1.0f, 1e-5f);
}

TEST(LongTermMemoryVectorHybrid, DeduplicatesOnConflictAndRunsHybridRRF) {
    LongTermMemory ltm(":memory:");
    std::vector<float> vec_quake = {1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<float> vec_music = {0.0f, 1.0f, 0.0f, 0.0f};

    // Insert chunk 0 and overwrite chunk 0 to verify UNIQUE(doc_path, chunk_index) ON CONFLICT REPLACE
    ASSERT_TRUE(ltm.storeDocumentEmbedding("/docs/shm.md", 0, "Old draft text", vec_quake));
    ASSERT_TRUE(ltm.storeDocumentEmbedding("/docs/shm.md", 0, "Yangon earthquake fault line structural resonance 8.06Hz", vec_quake));
    ASSERT_TRUE(ltm.storeDocumentEmbedding("/docs/shm.md", 1, "Guitar tuning E2 82.4Hz standard acoustic", vec_music));
    ASSERT_TRUE(ltm.storeDocumentEmbedding("/docs/other.md", 0, "Unrelated document chunk", vec_quake));

    // Verify deduplication: /docs/shm.md should only have 2 rows (chunk 0 and chunk 1)
    EXPECT_EQ(scalarInt(ltm.getDatabase(), "SELECT COUNT(*) FROM document_embeddings WHERE doc_path='/docs/shm.md';"), 2);

    // Scoped hybrid search on /docs/shm.md
    auto results = ltm.hybridSearch("earthquake resonance", vec_quake, "/docs/shm.md", 2);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].chunk_index, 0);
    EXPECT_GT(results[0].cosine_score, 0.99f);
    EXPECT_GT(results[0].lexical_hits, 0);
    EXPECT_GT(results[0].rrf_score, results[1].rrf_score);

    // SafeMode read-only guard test on storeDocumentEmbedding
    ltm.setReadOnly(true);
    EXPECT_FALSE(ltm.storeDocumentEmbedding("/docs/shm.md", 2, "Blocked in safe mode", vec_quake));
    EXPECT_FALSE(ltm.storeVault("secret_title", "encrypted_payload"));
    ltm.setReadOnly(false);
}

TEST(LongTermMemoryVectorHybrid, AtomicReindexingPurgesStaleTrailingChunksAndTracksHash) {
    LongTermMemory ltm(":memory:");
    const std::string doc_path = "/docs/spec_v5.md";
    const std::string model_id = "embeddinggemma-2-270m";

    // 1. Initial indexing with 10 chunks
    std::vector<LongTermMemory::DocumentChunk> initial_chunks;
    for (int i = 0; i < 10; ++i) {
        LongTermMemory::DocumentChunk chunk;
        chunk.index = i;
        chunk.text = "Initial chunk version 1 section " + std::to_string(i);
        chunk.embedding = {static_cast<float>(i + 1), 1.0f, 0.5f, 0.2f};
        initial_chunks.push_back(chunk);
    }

    ASSERT_TRUE(ltm.storeDocumentChunksAtomic(doc_path, "sha256_hash_v1", model_id, initial_chunks));
    EXPECT_TRUE(ltm.isDocumentIndexed(doc_path, "sha256_hash_v1", model_id));
    EXPECT_FALSE(ltm.isDocumentIndexed(doc_path, "sha256_hash_v2", model_id));
    EXPECT_EQ(scalarInt(ltm.getDatabase(), ("SELECT COUNT(*) FROM document_embeddings WHERE doc_path='" + doc_path + "';").c_str()), 10);

    // 2. Re-indexing with only 7 chunks (e.g. document was shortened)
    std::vector<LongTermMemory::DocumentChunk> updated_chunks;
    for (int i = 0; i < 7; ++i) {
        LongTermMemory::DocumentChunk chunk;
        chunk.index = i;
        chunk.text = "Shortened revision 2 section " + std::to_string(i);
        chunk.embedding = {1.0f, static_cast<float>(i + 1), 0.2f, 0.8f};
        updated_chunks.push_back(chunk);
    }

    ASSERT_TRUE(ltm.storeDocumentChunksAtomic(doc_path, "sha256_hash_v2", model_id, updated_chunks));

    // Verify atomic purge: exactly 7 rows must remain, old chunks 7, 8, 9 must be completely gone
    EXPECT_EQ(scalarInt(ltm.getDatabase(), ("SELECT COUNT(*) FROM document_embeddings WHERE doc_path='" + doc_path + "';").c_str()), 7);
    EXPECT_TRUE(ltm.isDocumentIndexed(doc_path, "sha256_hash_v2", model_id));
    EXPECT_FALSE(ltm.isDocumentIndexed(doc_path, "sha256_hash_v1", model_id));

    // Verify safe mode blocks atomic re-indexing
    ltm.setReadOnly(true);
    EXPECT_FALSE(ltm.storeDocumentChunksAtomic(doc_path, "sha256_hash_v3", model_id, updated_chunks));
    ltm.setReadOnly(false);
}

TEST(LongTermMemoryVectorHybrid, ModelIdIsolationAndCleanFtsFallback) {
    LongTermMemory ltm(":memory:");
    const std::string doc_270 = "/docs/ai_architecture_270.md";
    const std::string doc_440 = "/docs/ai_architecture_440.md";
    const std::string model_270m = "embeddinggemma-2-270m";
    const std::string model_440m = "embeddinggemma-2-440m";

    std::vector<float> vec_a = {1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<float> vec_b = {0.0f, 1.0f, 0.0f, 0.0f};

    ASSERT_TRUE(ltm.storeDocumentEmbedding(doc_270, "hash_270", 0, "Transformer attention head pruning", vec_a, model_270m));
    ASSERT_TRUE(ltm.storeDocumentEmbedding(doc_440, "hash_440", 0, "Multimodal vision encoder cross-attention", vec_b, model_440m));

    // Search strictly with 270m model
    auto results_270m = ltm.searchEmbeddings(vec_a, doc_270, 5, model_270m);
    ASSERT_EQ(results_270m.size(), 1u);
    EXPECT_EQ(results_270m[0].model_id, model_270m);
    EXPECT_GT(results_270m[0].cosine_score, 0.99f);

    // Search strictly with 440m model
    auto results_440m = ltm.searchEmbeddings(vec_b, doc_440, 5, model_440m);
    ASSERT_EQ(results_440m.size(), 1u);
    EXPECT_EQ(results_440m[0].model_id, model_440m);
    EXPECT_GT(results_440m[0].cosine_score, 0.99f);

    // Clean FTS fallback test: when query vector is empty, hybridSearch falls back to FTS BM25
    std::vector<float> empty_vec;
    auto fts_fallback_results = ltm.hybridSearch("pruning", empty_vec, "", 5, model_270m);
    ASSERT_FALSE(fts_fallback_results.empty());
    EXPECT_EQ(fts_fallback_results[0].model_id, model_270m);
    EXPECT_GT(fts_fallback_results[0].rrf_score, 0.0f);
}

TEST(LongTermMemoryVectorHybrid, MultiModelCoexistenceOnSamePath) {
    LongTermMemory ltm(":memory:");
    const std::string shared_path = "/docs/shared_spec.md";
    const std::string model_270m = "embeddinggemma-2-270m";
    const std::string model_440m = "embeddinggemma-2-440m";

    std::vector<float> vec_270 = {1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<float> vec_440 = {0.0f, 1.0f, 0.0f, 0.0f};

    // Index chunk 0 with model_270m, and chunk 0 with model_440m on the EXACT SAME PATH
    ASSERT_TRUE(ltm.storeDocumentEmbedding(shared_path, "hash_v1", 0, "Quantum qubit entanglement protocol", vec_270, model_270m));
    ASSERT_TRUE(ltm.storeDocumentEmbedding(shared_path, "hash_v1", 0, "Photovoltaic solar cell semiconductor", vec_440, model_440m));

    // Both should coexist under UNIQUE(doc_path, model_id, chunk_index)
    EXPECT_EQ(scalarInt(ltm.getDatabase(), ("SELECT COUNT(*) FROM document_embeddings WHERE doc_path='" + shared_path + "';").c_str()), 2);
    EXPECT_EQ(scalarInt(ltm.getDatabase(), ("SELECT COUNT(*) FROM document_embeddings WHERE doc_path='" + shared_path + "' AND model_id='" + model_270m + "';").c_str()), 1);
    EXPECT_EQ(scalarInt(ltm.getDatabase(), ("SELECT COUNT(*) FROM document_embeddings WHERE doc_path='" + shared_path + "' AND model_id='" + model_440m + "';").c_str()), 1);

    auto res_270 = ltm.searchEmbeddings(vec_270, shared_path, 5, model_270m);
    ASSERT_EQ(res_270.size(), 1u);
    EXPECT_EQ(res_270[0].model_id, model_270m);

    auto res_440 = ltm.searchEmbeddings(vec_440, shared_path, 5, model_440m);
    ASSERT_EQ(res_440.size(), 1u);
    EXPECT_EQ(res_440[0].model_id, model_440m);
}

TEST(LongTermMemoryVectorHybrid, DocumentChunksFtsSyncOnReindex) {
    LongTermMemory ltm(":memory:");
    const std::string doc_path = "/docs/research_paper.md";
    const std::string model_id = "embeddinggemma-2-270m";

    // 1. Initial indexing with unique keyword 'superconducting'
    std::vector<LongTermMemory::DocumentChunk> chunks_v1;
    LongTermMemory::DocumentChunk c1;
    c1.index = 0;
    c1.text = "The superconducting quantum qubit processor achieves high coherence times.";
    c1.embedding = {1.0f, 0.5f, 0.0f, 0.0f};
    chunks_v1.push_back(c1);

    ASSERT_TRUE(ltm.storeDocumentChunksAtomic(doc_path, "hash_initial", model_id, chunks_v1));

    // FTS match for 'superconducting' must return 1 hit
    std::vector<float> empty_vec;
    auto initial_search = ltm.hybridSearch("superconducting", empty_vec, doc_path, 5, model_id);
    ASSERT_EQ(initial_search.size(), 1u);
    EXPECT_EQ(initial_search[0].chunk_index, 0);

    // 2. Re-index document with completely revised text having 'photovoltaic' (no 'superconducting')
    std::vector<LongTermMemory::DocumentChunk> chunks_v2;
    LongTermMemory::DocumentChunk c2;
    c2.index = 0;
    c2.text = "The photovoltaic perovskite solar array demonstrates high conversion efficiency.";
    c2.embedding = {0.0f, 0.0f, 1.0f, 0.5f};
    chunks_v2.push_back(c2);

    ASSERT_TRUE(ltm.storeDocumentChunksAtomic(doc_path, "hash_revised", model_id, chunks_v2));

    // 3. Verify old keyword 'superconducting' is PURGED from FTS (0 hits)
    auto purged_search = ltm.hybridSearch("superconducting", empty_vec, doc_path, 5, model_id);
    EXPECT_TRUE(purged_search.empty());

    // 4. Verify new keyword 'photovoltaic' is successfully indexed in FTS (1 hit)
    auto new_search = ltm.hybridSearch("photovoltaic", empty_vec, doc_path, 5, model_id);
    ASSERT_EQ(new_search.size(), 1u);
    EXPECT_EQ(new_search[0].chunk_index, 0);

    // 5. Verify SQLite integrity and FTS5 integrity
    execSql(ltm.getDatabase(), "PRAGMA integrity_check;");
    execSql(ltm.getDatabase(), "INSERT INTO document_chunks_fts(document_chunks_fts) VALUES('integrity-check');");
}

TEST(LongTermMemoryMediaEmbeddings, AtomicStoreModalityFiltersTimestampsAndSafeMode) {
    LongTermMemory ltm(":memory:");
    const std::string model_740m = "embeddinggemma-2-740m";

    ASSERT_TRUE(tableExists(ltm.getDatabase(), "media_embeddings"));

    // Photo embedding along +X axis
    std::vector<LongTermMemory::MediaSegment> imgSegs(1);
    imgSegs[0].modality = "IMAGE";
    imgSegs[0].segment_index = 0;
    imgSegs[0].start_ms = 0;
    imgSegs[0].end_ms = 0;
    imgSegs[0].embedding = {1.0f, 0.0f, 0.0f, 0.0f};
    ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(
        "/sdcard/DCIM/beach_dog.jpg",
        "content://media/external/images/media/101",
        "beach_dog.jpg",
        "DCIM Camera",
        1700000100LL,
        245000LL,
        0LL,
        model_740m,
        imgSegs));

    // Audio voice memo along +Y axis
    std::vector<LongTermMemory::MediaSegment> audSegs(2);
    audSegs[0] = {"AUDIO", 0, 0LL, 20000LL, {0.1f, 0.9f, 0.0f, 0.0f}};
    audSegs[1] = {"AUDIO", 1, 15000LL, 35000LL, {0.0f, 1.0f, 0.0f, 0.0f}};
    ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(
        "/sdcard/Recordings/q4_budget.m4a",
        "content://media/external/audio/media/202",
        "q4_budget.m4a",
        "Recordings",
        1700000200LL,
        512000LL,
        35000LL,
        model_740m,
        audSegs));

    // Video with two frame segments (+Z and +X) and one audio segment (+W)
    std::vector<LongTermMemory::MediaSegment> vidSegs(3);
    vidSegs[0] = {"VIDEO_FRAME", 0, 0LL, 5000LL, {0.2f, 0.0f, 0.8f, 0.0f}};
    vidSegs[1] = {"VIDEO_FRAME", 1, 45000LL, 50000LL, {0.95f, 0.0f, 0.25f, 0.0f}};
    vidSegs[2] = {"VIDEO_AUDIO", 0, 30000LL, 50000LL, {0.0f, 0.0f, 0.1f, 0.99f}};
    ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(
        "/sdcard/Movies/holiday.mp4",
        "content://media/external/video/media/303",
        "holiday.mp4",
        "Movies",
        1700000300LL,
        4096000LL,
        60000LL,
        model_740m,
        vidSegs));

    EXPECT_TRUE(ltm.isMediaIndexed("/sdcard/DCIM/beach_dog.jpg", 1700000100LL, model_740m));
    EXPECT_FALSE(ltm.isMediaIndexed("/sdcard/DCIM/beach_dog.jpg", 1700000999LL, model_740m));

    // Query near +X across ALL: image (1.0) should rank #1, video frame at 45000ms (~0.967) #2
    auto allMatches = ltm.searchMediaEmbeddings({1.0f, 0.0f, 0.0f, 0.0f}, "ALL", model_740m, "", 5);
    ASSERT_GE(allMatches.size(), 2u);
    EXPECT_EQ(allMatches[0].media_path, "/sdcard/DCIM/beach_dog.jpg");
    EXPECT_EQ(allMatches[0].modality, "IMAGE");
    EXPECT_EQ(allMatches[1].media_path, "/sdcard/Movies/holiday.mp4");
    EXPECT_EQ(allMatches[1].start_ms, 45000LL);

    // Query near +X across VISUAL: returns IMAGE and VIDEO_FRAME only (never AUDIO or VIDEO_AUDIO)
    auto visualMatches = ltm.searchMediaEmbeddings({0.0f, 1.0f, 0.0f, 1.0f}, "VISUAL", model_740m, "", 10);
    ASSERT_EQ(visualMatches.size(), 3u);
    for (const auto& vm : visualMatches) {
        EXPECT_TRUE(vm.modality == "IMAGE" || vm.modality == "VIDEO_FRAME");
    }

    // Filter VIDEO matches both VIDEO_FRAME and VIDEO_AUDIO
    auto videoAudioMatches = ltm.searchMediaEmbeddings({0.0f, 0.0f, 0.0f, 1.0f}, "VIDEO", model_740m, "", 5);
    ASSERT_FALSE(videoAudioMatches.empty());
    EXPECT_EQ(videoAudioMatches[0].modality, "VIDEO_AUDIO");
    EXPECT_EQ(videoAudioMatches[0].start_ms, 30000LL);

    // Filter AUDIO
    auto audioMatches = ltm.searchMediaEmbeddings({0.0f, 1.0f, 0.0f, 0.0f}, "AUDIO", model_740m, "", 5);
    ASSERT_EQ(audioMatches.size(), 2u);
    EXPECT_EQ(audioMatches[0].start_ms, 15000LL);

    // Atomic re-index purges stale segments (3 -> 1)
    std::vector<LongTermMemory::MediaSegment> vidUpdated(1);
    vidUpdated[0] = {"VIDEO_FRAME", 0, 10000LL, 15000LL, {0.0f, 0.0f, 1.0f, 0.0f}};
    ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(
        "/sdcard/Movies/holiday.mp4",
        "content://media/external/video/media/303",
        "holiday.mp4",
        "Movies",
        1700000400LL,
        4096000LL,
        60000LL,
        model_740m,
        vidUpdated));
    EXPECT_EQ(scalarInt(ltm.getDatabase(), "SELECT COUNT(*) FROM media_embeddings WHERE media_path='/sdcard/Movies/holiday.mp4';"), 1);

    // Stats check
    std::string statsJson = ltm.getMediaIndexStatsJson(model_740m);
    EXPECT_NE(statsJson.find("\"images\":1"), std::string::npos);
    EXPECT_NE(statsJson.find("\"audio\":1"), std::string::npos);
    EXPECT_NE(statsJson.find("\"videos\":1"), std::string::npos);
    EXPECT_NE(statsJson.find("\"video_frames\":1"), std::string::npos);
    EXPECT_NE(statsJson.find("\"total_segments\":4"), std::string::npos);

    // SafeMode blocks writes/deletes/clear
    ltm.setReadOnly(true);
    EXPECT_FALSE(ltm.storeMediaEmbeddingsAtomic("/sdcard/DCIM/blocked.jpg", "", "blocked.jpg", "", 10LL, 10LL, 0LL, model_740m, imgSegs));
    EXPECT_EQ(ltm.deleteMediaEmbeddings("/sdcard/DCIM/beach_dog.jpg", model_740m), 0);
    EXPECT_EQ(ltm.clearMediaEmbeddings(model_740m), 0);
    ltm.setReadOnly(false);

    EXPECT_EQ(ltm.deleteMediaEmbeddings("/sdcard/DCIM/beach_dog.jpg", model_740m), 1);
    EXPECT_EQ(ltm.clearMediaEmbeddings(model_740m), 3);
}


