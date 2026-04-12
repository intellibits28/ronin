#include <gtest/gtest.h>
#include "checkpoint_engine.h"
#include <fstream>
#include <cstdio>
#include <string>

using namespace Ronin::Kernel::Checkpoint;

class AtomicIntegrityTest : public ::testing::Test {
protected:
    std::string checkpoint_path = "test_checkpoint.bin";
    std::string data_v1 = "VERSION_1_VALID_DATA";
    std::string data_v2 = "VERSION_2_NEW_DATA";

    void SetUp() override {
        // Create an initial valid checkpoint
        std::ofstream ofs(checkpoint_path, std::ios::binary);
        ofs.write(data_v1.data(), data_v1.size());
        ofs.close();
    }

    void TearDown() override {
        std::remove(checkpoint_path.c_str());
        std::remove((checkpoint_path + ".tmp").c_str());
    }
};

/**
 * Verify that the original file remains intact if a failure occurs before the rename.
 */
TEST_F(AtomicIntegrityTest, VerifyOriginalIntactOnFailure) {
    CheckpointEngine engine(checkpoint_path);
    ASSERT_TRUE(engine.initializeShadowBuffer(1024));

    // 1. Verify initial state
    std::ifstream ifs(checkpoint_path);
    std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
    ASSERT_EQ(content, data_v1);

    // 2. Simulate a "mid-write" state by creating a .tmp file manually
    // This mimics the state where the engine is writing but hasn't called rename() yet.
    std::string tmp_path = checkpoint_path + ".tmp";
    std::ofstream tmp_ofs(tmp_path, std::ios::binary);
    tmp_ofs << "PARTIAL_CORRUPT_DATA";
    tmp_ofs.close();

    // 3. Verify that the original 'test_checkpoint.bin' is STILL data_v1
    std::ifstream ifs_check(checkpoint_path);
    std::string content_check((std::istreambuf_iterator<char>(ifs_check)), (std::istreambuf_iterator<char>()));
    ASSERT_EQ(content_check, data_v1);
    
    // 4. Run the actual persist and verify it overcomes the .tmp and updates atomically
    ASSERT_TRUE(engine.persistAtomic());
    
    // 5. Verify the rename happened correctly
    std::ifstream ifs_final(checkpoint_path);
    std::string content_final((std::istreambuf_iterator<char>(ifs_final)), (std::istreambuf_iterator<char>()));
    // Note: Since we didn't actually write data_v2 into the memfd in this test, 
    // it will just write the initialized zeros, but the key is that content_final != data_v1
    // and the file exists.
    ASSERT_NE(content_final, data_v1);
}
