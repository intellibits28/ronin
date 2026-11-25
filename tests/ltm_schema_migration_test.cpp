#include <gtest/gtest.h>
#include "long_term_memory.h"
#include <cstdio>
#include <nlohmann/json.hpp>
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

    EXPECT_EQ(scalarInt(ltm.getDatabase(), "SELECT version FROM schema_version LIMIT 1;"), 5);
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
    imgSegs[0].modality = "image";
    imgSegs[0].asset_type = "image";
    imgSegs[0].segment_index = 99; // Standalone photo must normalize deterministically to segment_index = 0
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
    audSegs[0] = {"audio", 0, 0LL, 20000LL, {0.1f, 0.9f, 0.0f, 0.0f}, "audio"};
    audSegs[1] = {"audio", 1, 15000LL, 35000LL, {0.0f, 1.0f, 0.0f, 0.0f}, "audio"};
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
    vidSegs[0] = {"image", 0, 0LL, 5000LL, {0.2f, 0.0f, 0.8f, 0.0f}, "video"};
    vidSegs[1] = {"image", 1, 45000LL, 50000LL, {0.95f, 0.0f, 0.25f, 0.0f}, "video"};
    vidSegs[2] = {"audio", 0, 30000LL, 50000LL, {0.0f, 0.0f, 0.1f, 0.99f}, "video"};
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
    EXPECT_EQ(scalarInt(ltm.getDatabase(), "SELECT segment_index FROM media_embeddings WHERE media_path='/sdcard/DCIM/beach_dog.jpg';"), 0);

    // Query near +X across ALL: image (1.0) should rank #1, video frame at 45000ms (~0.967) #2
    auto allMatches = ltm.searchMediaEmbeddings({1.0f, 0.0f, 0.0f, 0.0f}, "ALL", model_740m, "", 5);
    ASSERT_GE(allMatches.size(), 2u);
    EXPECT_EQ(allMatches[0].media_path, "/sdcard/DCIM/beach_dog.jpg");
    EXPECT_EQ(allMatches[0].modality, "image");
    EXPECT_EQ(allMatches[0].asset_type, "image");
    EXPECT_EQ(allMatches[1].media_path, "/sdcard/Movies/holiday.mp4");
    EXPECT_EQ(allMatches[1].modality, "image");
    EXPECT_EQ(allMatches[1].asset_type, "video");
    EXPECT_EQ(allMatches[1].start_ms, 45000LL);

    // Unfiltered legacy VISUAL or empty filter is rejected (returns empty, zero leakage)
    EXPECT_TRUE(ltm.searchMediaEmbeddings({0.0f, 1.0f, 0.0f, 1.0f}, "VISUAL", model_740m, "", 10).empty());
    EXPECT_TRUE(ltm.searchMediaEmbeddings({0.0f, 1.0f, 0.0f, 1.0f}, "", model_740m, "", 10).empty());

    // Strict IMAGE filter returns standalone photos only (never video frames or audio)
    auto imageOnlyMatches = ltm.searchMediaEmbeddings({1.0f, 0.0f, 0.0f, 0.0f}, "IMAGE", model_740m, "", 10);
    ASSERT_EQ(imageOnlyMatches.size(), 1u);
    EXPECT_EQ(imageOnlyMatches[0].modality, "image");
    EXPECT_EQ(imageOnlyMatches[0].asset_type, "image");
    EXPECT_EQ(imageOnlyMatches[0].media_path, "/sdcard/DCIM/beach_dog.jpg");

    // Filter VIDEO matches both video image frames and video audio tracks
    auto videoAudioMatches = ltm.searchMediaEmbeddings({0.0f, 0.0f, 0.0f, 1.0f}, "VIDEO", model_740m, "", 5);
    ASSERT_FALSE(videoAudioMatches.empty());
    EXPECT_EQ(videoAudioMatches[0].modality, "audio");
    EXPECT_EQ(videoAudioMatches[0].asset_type, "video");
    EXPECT_EQ(videoAudioMatches[0].start_ms, 30000LL);

    // Filter AUDIO
    auto audioMatches = ltm.searchMediaEmbeddings({0.0f, 1.0f, 0.0f, 0.0f}, "AUDIO", model_740m, "", 5);
    ASSERT_EQ(audioMatches.size(), 2u);
    EXPECT_EQ(audioMatches[0].modality, "audio");
    EXPECT_EQ(audioMatches[0].asset_type, "audio");
    EXPECT_EQ(audioMatches[0].start_ms, 15000LL);

    // Atomic re-index purges stale segments (3 -> 1)
    std::vector<LongTermMemory::MediaSegment> vidUpdated(1);
    vidUpdated[0] = {"image", 0, 10000LL, 15000LL, {0.0f, 0.0f, 1.0f, 0.0f}, "video"};
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

TEST(LongTermMemoryMediaEmbeddings, ComputesPopulationZScoreAndListsDistinctIndexedMedia) {
    LongTermMemory ltm(":memory:");
    const std::string model_740m = "embeddinggemma-2-740m";

    // Insert 15 background images clustered around (0.65, 0.76, 0, 0) with tiny jitter
    for (int i = 0; i < 15; ++i) {
        std::vector<LongTermMemory::MediaSegment> seg(1);
        float jitter = (i % 3 - 1) * 0.015f;
        seg[0] = {"image", 0, 0LL, 0LL, {0.65f + jitter, 0.76f, 0.0f, 0.0f}, "image"};
        std::string p = "/sdcard/DCIM/Screenshots/screen_" + std::to_string(i) + ".png";
        std::string n = "screen_" + std::to_string(i) + ".png";
        ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(p, "", n, "Screenshots", 1700000000LL + i, 100000LL, 0LL, model_740m, seg));
    }

    // Insert 1 strong match along +X
    {
        std::vector<LongTermMemory::MediaSegment> seg(1);
        seg[0] = {"image", 0, 0LL, 0LL, {0.99f, 0.14f, 0.0f, 0.0f}, "image"};
        ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(
            "/sdcard/DCIM/Camera/bougainvillea_01.jpg",
            "content://media/external/images/media/999",
            "bougainvillea_01.jpg",
            "Camera",
            1700000500LL,
            350000LL,
            0LL,
            model_740m,
            seg));
    }

    // Insert 1 multi-segment video and 1 audio file
    {
        std::vector<LongTermMemory::MediaSegment> vidSegs(2);
        vidSegs[0] = {"image", 0, 0LL, 4000LL, {0.3f, 0.3f, 0.9f, 0.0f}, "video"};
        vidSegs[1] = {"audio", 0, 0LL, 15000LL, {0.2f, 0.2f, 0.2f, 0.9f}, "video"};
        ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(
            "/sdcard/DCIM/Camera/garden_clip.mp4",
            "",
            "garden_clip.mp4",
            "Camera",
            1700000600LL,
            2048000LL,
            15000LL,
            model_740m,
            vidSegs));
    }
    {
        std::vector<LongTermMemory::MediaSegment> audSeg(1);
        audSeg[0] = {"audio", 0, 0LL, 10000LL, {0.0f, 0.0f, 0.0f, 1.0f}, "audio"};
        ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(
            "/sdcard/Recordings/memo.m4a",
            "",
            "memo.m4a",
            "Recordings",
            1700000700LL,
            64000LL,
            10000LL,
            model_740m,
            audSeg));
    }

    // Search along +X across IMAGE (16 images total: 15 background + 1 strong match)
    auto matches = ltm.searchMediaEmbeddings({1.0f, 0.0f, 0.0f, 0.0f}, "IMAGE", model_740m, "bougainvillea", 8);
    ASSERT_EQ(matches.size(), 8u);
    EXPECT_EQ(matches[0].media_path, "/sdcard/DCIM/Camera/bougainvillea_01.jpg");
    EXPECT_EQ(matches[0].modality, "image");
    EXPECT_EQ(matches[0].asset_type, "image");
    EXPECT_EQ(matches[0].corpus_size, 16);
    EXPECT_NEAR(matches[0].corpus_mean, 0.67f, 0.05f);
    EXPECT_GT(matches[0].corpus_std, 0.0f);
    EXPECT_GT(matches[0].z_score, 3.0f);
    EXPECT_GE(matches[0].lexical_hits, 1);
    EXPECT_EQ(matches[1].lexical_hits, 0);
    EXPECT_LT(matches[1].z_score, 1.0f);

    // Flat orthogonal query along +Z over IMAGE -> std == 0 -> z_score == 0.0f
    auto flatMatches = ltm.searchMediaEmbeddings({0.0f, 0.0f, 1.0f, 0.0f}, "IMAGE", model_740m, "", 5);
    ASSERT_EQ(flatMatches.size(), 5u);
    EXPECT_EQ(flatMatches[0].corpus_size, 16);
    EXPECT_FLOAT_EQ(flatMatches[0].cosine_score, 0.0f);
    EXPECT_FLOAT_EQ(flatMatches[0].z_score, 0.0f);

    // Distinct indexed listing collapses multi-segment videos into 1 'video' entry
    std::string allListJson = ltm.listIndexedMediaJson("ALL", model_740m, 100);
    nlohmann::json allArr = nlohmann::json::parse(allListJson);
    ASSERT_TRUE(allArr.is_array());
    EXPECT_EQ(allArr.size(), 18u);

    std::string videoListJson = ltm.listIndexedMediaJson("VIDEO", model_740m, 100);
    nlohmann::json vidArr = nlohmann::json::parse(videoListJson);
    ASSERT_TRUE(vidArr.is_array());
    ASSERT_EQ(vidArr.size(), 1u);
    EXPECT_EQ(vidArr[0]["media_path"].get<std::string>(), "/sdcard/DCIM/Camera/garden_clip.mp4");
    EXPECT_EQ(vidArr[0]["modality"].get<std::string>(), "video");
    EXPECT_EQ(vidArr[0]["segment_count"].get<int>(), 2);
}

TEST(LongTermMemorySchemaMigration, SchemaV3ToV4MigratesModalityTagsAndPurgesStaleEntries) {
    const std::string db_path = tempDbPath("schema_v3_to_v4_test");
    std::remove(db_path.c_str());

    // Build an explicit Schema v3 database before instantiating LongTermMemory.
    sqlite3* raw_db = nullptr;
    ASSERT_EQ(sqlite3_open(db_path.c_str(), &raw_db), SQLITE_OK);
    execSql(raw_db, "CREATE TABLE schema_version (version INTEGER);");
    execSql(raw_db, "INSERT INTO schema_version(version) VALUES (3);");
    execSql(raw_db, "CREATE TABLE facts (id INTEGER PRIMARY KEY AUTOINCREMENT, entity TEXT NOT NULL, attribute TEXT NOT NULL, value TEXT NOT NULL, source INTEGER, confidence REAL, surprise_score REAL DEFAULT 0.0, created_at INTEGER, updated_at INTEGER, UNIQUE(entity, attribute));");
    execSql(raw_db, "CREATE TABLE notes (id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT, content TEXT, tags TEXT, created_at INTEGER, updated_at INTEGER);");
    execSql(raw_db, "CREATE TABLE episodes (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp INTEGER, intent TEXT, goal_id TEXT, node_id TEXT, summary TEXT, payload_json TEXT, outcome_enum INTEGER, latency_ms INTEGER, confidence_before REAL, confidence_after REAL);");
    execSql(raw_db, "CREATE TABLE document_embeddings (id INTEGER PRIMARY KEY AUTOINCREMENT, doc_path TEXT NOT NULL, doc_hash TEXT NOT NULL DEFAULT '', chunk_index INTEGER NOT NULL, chunk_text TEXT NOT NULL, embedding_blob BLOB NOT NULL, model_id TEXT NOT NULL DEFAULT 'embeddinggemma-2-270m', dimension INTEGER NOT NULL, created_at INTEGER NOT NULL, UNIQUE(doc_path, model_id, chunk_index) ON CONFLICT REPLACE);");
    execSql(raw_db, "CREATE TABLE media_embeddings (id INTEGER PRIMARY KEY AUTOINCREMENT, media_path TEXT NOT NULL, media_uri TEXT NOT NULL DEFAULT '', modality TEXT NOT NULL, segment_index INTEGER NOT NULL DEFAULT 0, start_ms INTEGER NOT NULL DEFAULT 0, end_ms INTEGER NOT NULL DEFAULT 0, duration_ms INTEGER NOT NULL DEFAULT 0, display_name TEXT NOT NULL DEFAULT '', metadata_text TEXT NOT NULL DEFAULT '', modified_at INTEGER NOT NULL DEFAULT 0, file_size INTEGER NOT NULL DEFAULT 0, model_id TEXT NOT NULL DEFAULT 'embeddinggemma-2-740m', dimension INTEGER NOT NULL, embedding_blob BLOB NOT NULL, created_at INTEGER NOT NULL, UNIQUE(media_path, model_id, modality, segment_index) ON CONFLICT REPLACE);");

    auto insertMediaRow = [&](const char* path, const char* mod, int seg_idx, const std::vector<float>& vec, int declared_dim) {
        sqlite3_stmt* s = nullptr;
        ASSERT_EQ(sqlite3_prepare_v2(
            raw_db,
            "INSERT INTO media_embeddings (media_path, modality, segment_index, model_id, dimension, embedding_blob, created_at) VALUES (?, ?, ?, 'embeddinggemma-2-740m', ?, ?, 100);",
            -1, &s, nullptr), SQLITE_OK);
        sqlite3_bind_text(s, 1, path, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, mod, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(s, 3, seg_idx);
        sqlite3_bind_int(s, 4, declared_dim);
        sqlite3_bind_blob(s, 5, vec.data(), static_cast<int>(vec.size() * sizeof(float)), SQLITE_TRANSIENT);
        ASSERT_EQ(sqlite3_step(s), SQLITE_DONE);
        sqlite3_finalize(s);
    };

    const std::vector<float> v4{1.0f, 0.0f, 0.0f, 0.0f};
    {
        sqlite3_stmt* ds = nullptr;
        ASSERT_EQ(sqlite3_prepare_v2(
            raw_db,
            "INSERT INTO document_embeddings (doc_path, doc_hash, chunk_index, chunk_text, model_id, dimension, embedding_blob, created_at) VALUES ('/docs/spec.md', 'h1', 0, 'spec text', 'embeddinggemma-2-270m', 4, ?, 100);",
            -1, &ds, nullptr), SQLITE_OK);
        sqlite3_bind_blob(ds, 1, v4.data(), static_cast<int>(v4.size() * sizeof(float)), SQLITE_TRANSIENT);
        ASSERT_EQ(sqlite3_step(ds), SQLITE_DONE);
        sqlite3_finalize(ds);
    }

    insertMediaRow("/DCIM/photo_v3.jpg", "IMAGE", 3, {0.9f, 0.1f, 0.0f, 0.0f}, 4);
    insertMediaRow("/Recordings/aud_v3.m4a", "AUDIO", 0, {0.0f, 1.0f, 0.0f, 0.0f}, 4);
    insertMediaRow("/Movies/vid_v3.mp4", "VIDEO_FRAME", 0, {0.0f, 0.0f, 1.0f, 0.0f}, 4);
    insertMediaRow("/Movies/vid_v3.mp4", "VIDEO_AUDIO", 0, {0.0f, 0.0f, 0.0f, 1.0f}, 4);
    // Stale/corrupt entries that v4 migration must purge atomically:
    insertMediaRow("/tmp/unknown_mod.bin", "UNKNOWN_MOD", 0, {1.0f, 0.0f, 0.0f, 0.0f}, 4);
    insertMediaRow("/tmp/corrupt_dim.jpg", "IMAGE", 0, {1.0f, 0.0f}, 4);
    sqlite3_close(raw_db);

    {
        LongTermMemory ltm(db_path);
        ASSERT_NE(ltm.getDatabase(), nullptr);
        ASSERT_EQ(scalarInt(ltm.getDatabase(), "SELECT version FROM schema_version LIMIT 1;"), 5);

        // v4 normalizes tags while v5 purges legacy pipeline_version < 2 media vectors;
        // document_embeddings MUST remain intact across media pipeline invalidation.
        EXPECT_EQ(scalarInt(ltm.getDatabase(), "SELECT COUNT(*) FROM media_embeddings;"), 0);
        EXPECT_EQ(scalarInt(ltm.getDatabase(), "SELECT COUNT(*) FROM media_embeddings WHERE media_path='/tmp/unknown_mod.bin' OR media_path='/tmp/corrupt_dim.jpg';"), 0);
        EXPECT_EQ(scalarInt(ltm.getDatabase(), "SELECT COUNT(*) FROM document_embeddings WHERE modality='text';"), 1);

        execSql(ltm.getDatabase(), "PRAGMA integrity_check;");
    }
    std::remove(db_path.c_str());
}

TEST(LongTermMemoryMediaEmbeddings, Stores768DFloat32LosslesslyAndReturnsUnitSelfSimilarity) {
    LongTermMemory ltm(":memory:");
    const std::string model_id = "embeddinggemma-2-740m";

    std::vector<float> vec(768, 0.0f);
    for (size_t i = 0; i < vec.size(); ++i) {
        vec[i] = static_cast<float>((static_cast<int>(i % 19) - 9)) * 0.05f + 0.01f;
    }

    ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(
        "/DCIM/Camera/bougainvillea_768.jpg",
        "content://media/external/images/media/768",
        "bougainvillea_768.jpg",
        "Camera",
        1732000000,
        420000,
        0,
        model_id,
        {{"image", 0, 0, 0, vec, "image"}}));

    EXPECT_TRUE(ltm.isMediaIndexed("/DCIM/Camera/bougainvillea_768.jpg", 1732000000, model_id));
    EXPECT_EQ(scalarInt(ltm.getDatabase(), "SELECT length(embedding_blob) FROM media_embeddings LIMIT 1;"), 768 * 4);
    EXPECT_EQ(scalarInt(ltm.getDatabase(), "SELECT pipeline_version FROM media_embeddings LIMIT 1;"), LongTermMemory::kMediaPipelineVersion);

    auto matches = ltm.searchMediaEmbeddings(vec, "IMAGE", model_id, "", 5);
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_NEAR(matches[0].cosine_score, 1.0f, 1e-4f);
    EXPECT_NEAR(matches[0].final_score, matches[0].cosine_score, 1e-6f);
}

TEST(LongTermMemoryMediaEmbeddings, SemanticSearchDoesNotBoostFilenameMetadataAndPreservesLexicalFallback) {
    LongTermMemory ltm(":memory:");
    const std::string model_id = "embeddinggemma-2-740m";

    // Orthogonal distractor whose filename matches "bougainvillea", cosine = 0.0
    ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(
        "/DCIM/Camera/bougainvillea.jpg", "", "bougainvillea.jpg", "Camera", 101, 1000, 0, model_id,
        {{"image", 0, 0, 0, {0.0f, 1.0f, 0.0f, 0.0f}, "image"}}));

    // Strong semantic visual match with opaque filename IMG_001.jpg, cosine = 0.65
    ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(
        "/DCIM/Camera/IMG_001.jpg", "", "IMG_001.jpg", "Camera", 102, 1000, 0, model_id,
        {{"image", 0, 0, 0, {0.65f, 0.7599342f, 0.0f, 0.0f}, "image"}}));

    // 1. Semantic search: even if lexical_query is passed, final_score equals cosine_score (zero +0.04..0.08 boost)
    auto semMatches = ltm.searchMediaEmbeddings({1.0f, 0.0f, 0.0f, 0.0f}, "IMAGE", model_id, "bougainvillea", 10);
    ASSERT_EQ(semMatches.size(), 2u);
    EXPECT_EQ(semMatches[0].media_path, "/DCIM/Camera/IMG_001.jpg");
    EXPECT_NEAR(semMatches[0].cosine_score, 0.65f, 1e-4f);
    EXPECT_NEAR(semMatches[0].final_score, semMatches[0].cosine_score, 1e-6f);
    EXPECT_NEAR(semMatches[1].cosine_score, 0.0f, 1e-4f);
    EXPECT_NEAR(semMatches[1].final_score, 0.0f, 1e-6f);

    // 2. Explicit lexical fallback (empty query vector {}): matches filename cleanly
    auto lexMatches = ltm.searchMediaEmbeddings({}, "IMAGE", model_id, "bougainvillea", 10);
    ASSERT_EQ(lexMatches.size(), 1u);
    EXPECT_EQ(lexMatches[0].media_path, "/DCIM/Camera/bougainvillea.jpg");
    EXPECT_GT(lexMatches[0].final_score, 0.0f);
}

TEST(LongTermMemorySchemaMigration, SchemaV4ToV5PurgesStaleMediaPreservesDocumentsAndReindexesCleanly) {
    const std::string db_path = tempDbPath("v4_to_v5_media");
    std::remove(db_path.c_str());

    sqlite3* raw_db = nullptr;
    ASSERT_EQ(sqlite3_open(db_path.c_str(), &raw_db), SQLITE_OK);
    execSql(raw_db, "CREATE TABLE schema_version (version INTEGER);");
    execSql(raw_db, "INSERT INTO schema_version(version) VALUES (4);");
    execSql(raw_db, "CREATE TABLE notes (id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT NOT NULL UNIQUE, content TEXT NOT NULL, tags TEXT DEFAULT '', created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL);");
    execSql(raw_db, "INSERT INTO notes(title, content, tags, created_at, updated_at) VALUES ('keep_note', 'important text', '', 1, 1);");
    execSql(raw_db, "CREATE TABLE document_embeddings (id INTEGER PRIMARY KEY AUTOINCREMENT, doc_path TEXT NOT NULL, doc_hash TEXT NOT NULL DEFAULT '', model_id TEXT NOT NULL DEFAULT 'embeddinggemma-2-270m', modality TEXT NOT NULL DEFAULT 'text', chunk_index INTEGER NOT NULL, chunk_text TEXT NOT NULL, dimension INTEGER NOT NULL, embedding_blob BLOB NOT NULL, created_at INTEGER NOT NULL, UNIQUE(doc_path, model_id, chunk_index) ON CONFLICT REPLACE);");
    execSql(raw_db,
        "CREATE TABLE media_embeddings ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, media_path TEXT NOT NULL, media_uri TEXT NOT NULL DEFAULT '', "
        "modality TEXT NOT NULL, asset_type TEXT NOT NULL DEFAULT 'image', segment_index INTEGER NOT NULL DEFAULT 0, "
        "start_ms INTEGER NOT NULL DEFAULT 0, end_ms INTEGER NOT NULL DEFAULT 0, duration_ms INTEGER NOT NULL DEFAULT 0, "
        "display_name TEXT NOT NULL DEFAULT '', metadata_text TEXT NOT NULL DEFAULT '', modified_at INTEGER NOT NULL DEFAULT 0, "
        "file_size INTEGER NOT NULL DEFAULT 0, model_id TEXT NOT NULL DEFAULT 'embeddinggemma-2-740m', "
        "dimension INTEGER NOT NULL, embedding_blob BLOB NOT NULL, created_at INTEGER NOT NULL, "
        "UNIQUE(media_path, model_id, modality, asset_type, segment_index) ON CONFLICT REPLACE);"
    );

    const std::vector<float> dummy = {1.0f, 0.0f, 0.0f, 0.0f};
    {
        sqlite3_stmt* ds = nullptr;
        ASSERT_EQ(sqlite3_prepare_v2(raw_db, "INSERT INTO document_embeddings(doc_path, doc_hash, model_id, modality, chunk_index, chunk_text, dimension, embedding_blob, created_at) VALUES ('/Docs/keep.txt', 'h', 'embeddinggemma-2-270m', 'text', 0, 'flower doc', 4, ?, 1);", -1, &ds, nullptr), SQLITE_OK);
        sqlite3_bind_blob(ds, 1, dummy.data(), 16, SQLITE_TRANSIENT);
        ASSERT_EQ(sqlite3_step(ds), SQLITE_DONE);
        sqlite3_finalize(ds);

        sqlite3_stmt* ms = nullptr;
        ASSERT_EQ(sqlite3_prepare_v2(raw_db, "INSERT INTO media_embeddings(media_path, media_uri, modality, asset_type, segment_index, display_name, modified_at, model_id, dimension, embedding_blob, created_at) VALUES ('/DCIM/Camera/stale_448.jpg', '', 'image', 'image', 0, 'stale_448.jpg', 555, 'embeddinggemma-2-740m', 4, ?, 1);", -1, &ms, nullptr), SQLITE_OK);
        sqlite3_bind_blob(ms, 1, dummy.data(), 16, SQLITE_TRANSIENT);
        ASSERT_EQ(sqlite3_step(ms), SQLITE_DONE);
        sqlite3_finalize(ms);
    }
    sqlite3_close(raw_db);

    {
        LongTermMemory ltm(db_path);
        ASSERT_NE(ltm.getDatabase(), nullptr);
        ASSERT_EQ(scalarInt(ltm.getDatabase(), "SELECT version FROM schema_version LIMIT 1;"), 5);

        // Legacy v4 448px JPEG-85 vector purged; document_embeddings & notes preserved.
        EXPECT_EQ(scalarInt(ltm.getDatabase(), "SELECT COUNT(*) FROM media_embeddings;"), 0);
        EXPECT_FALSE(ltm.isMediaIndexed("/DCIM/Camera/stale_448.jpg", 555, "embeddinggemma-2-740m"));
        EXPECT_EQ(scalarInt(ltm.getDatabase(), "SELECT COUNT(*) FROM document_embeddings WHERE doc_path='/Docs/keep.txt';"), 1);
        EXPECT_EQ(scalarInt(ltm.getDatabase(), "SELECT COUNT(*) FROM notes WHERE title='keep_note';"), 1);

        // Re-indexing writes pipeline_version = 2 without duplicate rows on repeat stores.
        ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(
            "/DCIM/Camera/stale_448.jpg", "content://media/external/images/media/99", "stale_448.jpg", "Camera", 555, 2048, 0, "embeddinggemma-2-740m",
            {{"image", 0, 0, 0, dummy, "image"}}));
        ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(
            "/DCIM/Camera/stale_448.jpg", "content://media/external/images/media/99", "stale_448.jpg", "Camera", 555, 2048, 0, "embeddinggemma-2-740m",
            {{"image", 0, 0, 0, dummy, "image"}}));

        EXPECT_TRUE(ltm.isMediaIndexed("/DCIM/Camera/stale_448.jpg", 555, "embeddinggemma-2-740m"));
        EXPECT_EQ(scalarInt(ltm.getDatabase(), "SELECT COUNT(*) FROM media_embeddings WHERE media_path='/DCIM/Camera/stale_448.jpg';"), 1);
        EXPECT_EQ(scalarInt(ltm.getDatabase(), "SELECT pipeline_version FROM media_embeddings WHERE media_path='/DCIM/Camera/stale_448.jpg';"), LongTermMemory::kMediaPipelineVersion);
    }
    std::remove(db_path.c_str());
}

TEST(LongTermMemoryMediaEmbeddings, StrictModalityIsolationZeroCrossModalLeakage) {
    LongTermMemory ltm(":memory:");
    const std::string model_id = "embeddinggemma-2-740m";

    // Photo aligned at ~0.90 to +X
    ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(
        "/DCIM/Camera/flower.jpg", "", "flower.jpg", "Camera", 100, 1000, 0, model_id,
        {{"image", 0, 0, 0, {0.90f, 0.435f, 0.0f, 0.0f}, "image"}}));

    // Two video clips with distinct video frame and video audio distributions
    ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(
        "/Movies/clip.mp4", "", "clip.mp4", "Movies", 101, 5000, 10000, model_id,
        {{"image", 0, 0, 5000, {1.0f, 0.0f, 0.0f, 0.0f}, "video"},
         {"audio", 0, 0, 10000, {0.0f, 1.0f, 0.0f, 0.0f}, "video"}}));
    ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(
        "/Movies/clip2.mp4", "", "clip2.mp4", "Movies", 102, 5000, 10000, model_id,
        {{"image", 0, 0, 5000, {0.6f, 0.8f, 0.0f, 0.0f}, "video"},
         {"audio", 0, 0, 10000, {0.6f, 0.8f, 0.0f, 0.0f}, "video"}}));

    // Standalone audio aligned at 1.0 to +X (must NEVER leak into IMAGE search!)
    ASSERT_TRUE(ltm.storeMediaEmbeddingsAtomic(
        "/Recordings/voice.m4a", "", "voice.m4a", "Recordings", 103, 2000, 8000, model_id,
        {{"audio", 0, 0, 8000, {1.0f, 0.0f, 0.0f, 0.0f}, "audio"}}));

    // Document chunk aligned at 1.0 to +X (stored strictly in document_embeddings with modality='text')
    ASSERT_TRUE(ltm.storeDocumentChunksAtomic(
        "/Docs/notes.txt", "hash1", model_id, {{0, "bougainvillea flower notes", {1.0f, 0.0f, 0.0f, 0.0f}}}));

    // Reject invalid modality writes
    EXPECT_FALSE(ltm.storeMediaEmbeddingsAtomic(
        "/DCIM/bad.jpg", "", "bad.jpg", "", 104, 100, 0, model_id,
        {{"INVALID_MOD", 0, 0, 0, {1.0f, 0.0f, 0.0f, 0.0f}, "image"}}));

    // Strict IMAGE query along +X returns ONLY standalone photo
    auto imgMatches = ltm.searchMediaEmbeddings({1.0f, 0.0f, 0.0f, 0.0f}, "IMAGE", model_id, "", 10);
    ASSERT_EQ(imgMatches.size(), 1u);
    EXPECT_EQ(imgMatches[0].media_path, "/DCIM/Camera/flower.jpg");
    EXPECT_EQ(imgMatches[0].modality, "image");
    EXPECT_EQ(imgMatches[0].asset_type, "image");
    EXPECT_EQ(imgMatches[0].corpus_size, 1);

    // VIDEO query normalizes Z-scores independently per ('image','video') and ('audio','video') bucket
    auto vidMatches = ltm.searchMediaEmbeddings({1.0f, 0.0f, 0.0f, 0.0f}, "VIDEO", model_id, "", 10);
    ASSERT_EQ(vidMatches.size(), 4u);
    for (const auto& vm : vidMatches) {
        EXPECT_EQ(vm.asset_type, "video");
        EXPECT_EQ(vm.corpus_size, 2); // 2 items in each video sub-bucket (image vs audio), not 4
    }

    // Strict AUDIO query returns ONLY standalone audio
    auto audMatches = ltm.searchMediaEmbeddings({1.0f, 0.0f, 0.0f, 0.0f}, "AUDIO", model_id, "", 10);
    ASSERT_EQ(audMatches.size(), 1u);
    EXPECT_EQ(audMatches[0].media_path, "/Recordings/voice.m4a");
    EXPECT_EQ(audMatches[0].modality, "audio");
    EXPECT_EQ(audMatches[0].asset_type, "audio");

    // Document search returns ONLY text docs
    auto docMatches = ltm.hybridSearch("bougainvillea", {1.0f, 0.0f, 0.0f, 0.0f}, "", 5, model_id);
    ASSERT_EQ(docMatches.size(), 1u);
    EXPECT_EQ(docMatches[0].modality, "text");
}



