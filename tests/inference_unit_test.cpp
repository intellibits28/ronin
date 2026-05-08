#include <gtest/gtest.h>
#include "hal/shared_memory_bridge.h"
#include "models/hydration_manager.h"
#include <fstream>
#include <cstdio>

using namespace Ronin::Kernel::HAL;
using namespace Ronin::Kernel::Model;

class InferenceModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a dummy model file for HydrationManager tests
        dummy_model_path = "test_model.bin";
        std::ofstream ofs(dummy_model_path, std::ios::binary);
        std::vector<char> data(1024 * 1024, 0x42); // 1MB of dummy data
        ofs.write(data.data(), data.size());
        ofs.close();

        // Base path for SHM
        base_path = "."; 
    }

    void TearDown() override {
        std::remove(dummy_model_path.c_str());
        std::remove("test_spine.shm");
    }

    std::string dummy_model_path;
    std::string base_path;
};

// Test 1: SpineRingBuffer SPSC Logic
TEST_F(InferenceModuleTest, RingBufferPushPop) {
    SpineRingBuffer rb;
    std::memset(&rb, 0, sizeof(rb));
    
    InferencePacket p1 = {1, 101, "မင်္ဂလာပါ", 0.99f, false};
    InferencePacket p2 = {1, 102, "စမ်းသပ်မှု", 0.98f, true};

    EXPECT_TRUE(rb.push(p1));
    EXPECT_TRUE(rb.push(p2));

    InferencePacket out;
    EXPECT_TRUE(rb.pop(out));
    EXPECT_EQ(out.token_id, 101);
    EXPECT_STREQ(out.fragment, "မင်္ဂလာပါ");

    EXPECT_TRUE(rb.pop(out));
    EXPECT_EQ(out.token_id, 102);
    EXPECT_TRUE(out.is_final);
}

// Test 2: SharedMemoryBridge Portability & Connectivity
TEST_F(InferenceModuleTest, SHMBridgeConnectivity) {
    SharedMemoryBridge<SpineRingBuffer> producer("test_spine");
    EXPECT_TRUE(producer.create(base_path, true));

    SharedMemoryBridge<SpineRingBuffer> consumer("test_spine");
    EXPECT_TRUE(consumer.create(base_path, false));

    SpineRingBuffer* rb_p = producer.get();
    SpineRingBuffer* rb_c = consumer.get();

    InferencePacket p = {2, 201, "Streaming...", 1.0f, false};
    EXPECT_TRUE(rb_p->push(p));

    InferencePacket out;
    EXPECT_TRUE(rb_c->pop(out));
    EXPECT_EQ(out.token_id, 201);
}

// Test 3: HydrationManager mmap & Pointer Integrity
TEST_F(InferenceModuleTest, HydrationManagerMmap) {
    HydrationManager hm;
    EXPECT_TRUE(hm.hydrate(dummy_model_path));
    
    EXPECT_NE(hm.getModelPtr(), (void*)-1);
    EXPECT_EQ(hm.getModelSize(), 1024 * 1024);

    // Verify content (0x42 we wrote in SetUp)
    unsigned char* ptr = static_cast<unsigned char*>(hm.getModelPtr());
    EXPECT_EQ(ptr[0], 0x42);
    EXPECT_EQ(ptr[hm.getModelSize() - 1], 0x42);

    hm.dehydrate();
    EXPECT_EQ(hm.getModelPtr(), (void*)-1);
}

// Test 4: RAM Monitor Logic
TEST_F(InferenceModuleTest, RAMMonitorAvailability) {
    uint64_t ram = HydrationManager::getAvailableRAM();
    EXPECT_GT(ram, 0);
    // In CI (Ubuntu), this should generally be > 0.
}
