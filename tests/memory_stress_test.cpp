#include <gtest/gtest.h>
#include "memory_manager.h"
#include <vector>
#include <iostream>

using namespace Ronin::Kernel::Memory;

/**
 * Memory Stress Test: Verifies that the Tri-Anchor model handles high token
 * volume by correctly pruning Anchor 3 and compressing into Anchor 2.
 */
TEST(MemoryManagerStress, PruningAndContinuity) {
    const size_t window_size = 10;
    MemoryManager mm(window_size);

    // 1. Set Prefix (Anchor 1)
    std::vector<Token> prefix;
    for (uint32_t i = 0; i < 5; ++i) {
        prefix.push_back({i, 1.0f, {0.1f, 0.2f, 0.3f}});
    }
    mm.setPrefix(prefix);

    // 2. Flood with tokens to trigger pruning (Anchor 3 -> Anchor 2)
    // We add 50 tokens, so 40 should be compressed into Anchor 2
    for (uint32_t i = 100; i < 150; ++i) {
        Token t = {i, static_cast<float>(i % 10) / 10.0f, {0.5f, 0.6f, 0.7f}};
        mm.addRecentToken(t);
    }

    // 3. Verify Context Reconstruction (Intent Continuity)
    // The total tokens should be 5 (prefix) + 50 (added) = 55
    std::vector<uint32_t> context = mm.reconstructContext();
    ASSERT_EQ(context.size(), 55);

    // Verify prefix is still there
    for (uint32_t i = 0; i < 5; ++i) {
        ASSERT_EQ(context[i], i);
    }

    // 4. Simulate LMK Memory Pressure
    // This should trigger Anchor 2 management without crashing
    EXPECT_NO_THROW(mm.onMemoryPressure());

    // 5. Verify that we can still reconstruct after pressure
    std::vector<uint32_t> context_after = mm.reconstructContext();
    ASSERT_EQ(context_after.size(), 55);
    
    std::cout << "Memory Stress Test: Successfully managed 55 tokens with pruning and pressure." << std::endl;
}
