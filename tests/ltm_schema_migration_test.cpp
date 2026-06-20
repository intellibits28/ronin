#include <gtest/gtest.h>
#include "long_term_memory.h"
#include <cstdio>
#include <sqlite3.h>
#include <string>

using Ronin::Kernel::Memory::LongTermMemory;

namespace {

std::string tempDbPath(const char* name) {
    return std::string("/tmp/ronin_") + name + ".db";
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

    EXPECT_EQ(scalarInt(ltm.getDatabase(), "SELECT version FROM schema_version LIMIT 1;"), 2);
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

