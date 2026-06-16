#include <gtest/gtest.h>
#include "reflection_engine.h"
#include "long_term_memory.h"
#include "thompson_sampler.h"
#include "ronin_types.hpp"
#include <memory>

using namespace Ronin::Kernel;
using namespace Ronin::Kernel::Reasoning;
using namespace Ronin::Kernel::Memory;

class EvolutionTest : public ::testing::Test {
protected:
    static std::unique_ptr<LongTermMemory> ltm;
    static std::unique_ptr<ThompsonSampler> sampler;

    static void SetUpTestSuite() {
        ltm = std::make_unique<LongTermMemory>(":memory:");
        sampler = std::make_unique<ThompsonSampler>();
    }

    static void TearDownTestSuite() {
        ltm.reset();
        sampler.reset();
    }
};

std::unique_ptr<LongTermMemory> EvolutionTest::ltm = nullptr;
std::unique_ptr<ThompsonSampler> EvolutionTest::sampler = nullptr;

TEST_F(EvolutionTest, NightlyReflectionCycle) {
    ReflectionEngine engine(ltm.get(), sampler.get());
    
    // 1. Mock some unstable activity (intentional failures)
    for (int i = 0; i < 4; ++i) {
        // intent, summary, payload, success, goal_id, node_id, latency, conf_before, conf_after
        // Using a mock to insert directly into episodes table
        char sql[256];
        snprintf(sql, sizeof(sql), "INSERT INTO episodes (intent, summary, outcome_enum, timestamp) VALUES ('SEND_SMS', 'Failed to send', 0, %llu);", 
                 (unsigned long long)std::time(nullptr) - i);
        sqlite3_exec(ltm->getDatabase(), sql, nullptr, nullptr, nullptr);
    }
    
    // 2. Add one success to make it stable-ish but leaning failed
    sqlite3_exec(ltm->getDatabase(), "INSERT INTO episodes (intent, summary, outcome_enum, timestamp) VALUES ('SEND_SMS', 'Sent successfully', 1, 12345);", nullptr, nullptr, nullptr);

    // 3. Run Reflection
    engine.reflectOnRecentTasks();
    
    // 4. Verify consolidation (A note should be stored as a 'Nightly Lesson')
    auto notes = ltm->searchNotes("Intent 'SEND_SMS' is unreliable");
    EXPECT_FALSE(notes.empty());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
