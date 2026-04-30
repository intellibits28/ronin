#include <gtest/gtest.h>
#include "long_term_memory.h"
#include <vector>
#include <string>
#include <filesystem>

using namespace Ronin::Kernel::Memory;
namespace fs = std::filesystem;

class SearchCapabilityTest : public ::testing::Test {
protected:
    std::string db_path;
    
    void SetUp() override {
        db_path = (fs::temp_directory_path() / "test_search.db").string();
        std::remove(db_path.c_str());
    }

    void TearDown() override {
        std::remove(db_path.c_str());
    }
};

/**
 * Verifies that SQLite FTS5 correctly indexes and retrieves file metadata.
 */
TEST_F(SearchCapabilityTest, VerifyFTS5IndexingAndSearch) {
    LongTermMemory ltm(db_path);

    // 1. Mock Indexing
    ASSERT_TRUE(ltm.indexFile("project_manifest.pdf", "/data/docs/manifest.pdf", ".pdf", 12345678));
    ASSERT_TRUE(ltm.indexFile("ronin_core.cpp", "/src/kernel/ronin_core.cpp", ".cpp", 12345679));
    ASSERT_TRUE(ltm.indexFile("checkpoint_schema.fbs", "/schema/checkpoint.fbs", ".fbs", 12345680));

    // 2. Perform Search Query
    // v2.6 searchFiles uses LIKE %query%
    auto results = ltm.searchFiles("ronin");
    
    // 3. Verify Results
    // ronin_core.cpp matches %ronin%
    ASSERT_EQ(results.size(), 1);
    ASSERT_EQ(results[0], "/src/kernel/ronin_core.cpp");

    // 4. Test Partial Match
    // project_manifest.pdf matches %manifest%
    auto partial_results = ltm.searchFiles("manifest");
    ASSERT_EQ(partial_results.size(), 1);
    ASSERT_EQ(partial_results[0], "/data/docs/manifest.pdf");
}
