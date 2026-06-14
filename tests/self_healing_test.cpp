#include <gtest/gtest.h>
#include "recovery_manager.h"
#include "adaptive_budget.h"
#include "failure_telemetry.h"
#include "execution_context.h"
#include <thread>
#include <chrono>

using namespace Ronin::Kernel::Execution;
using namespace Ronin::Kernel;

TEST(SelfHealingTest, RecoveryCheckpointing) {
    auto& recovery = RecoveryManager::getInstance();
    auto ctx = std::make_shared<ExecutionContext>();
    ctx->session_id = "s1";
    ctx->execution_id = "e1";
    
    EXPECT_TRUE(recovery.recordCheckpoint(ctx));
    EXPECT_EQ(recovery.getLastValidContext()->session_id, "s1");
}

TEST(SelfHealingTest, AdaptiveBudgetTuning) {
    auto& budget = AdaptiveBudgetController::getInstance();
    auto& telemetry = FailureTelemetryStore::getInstance();
    std::string node_id = "heavy_node";
    
    // Initial budget
    uint32_t b1 = budget.getAdaptedBudget("e1", node_id);
    EXPECT_EQ(b1, 15000);
    
    // Simulate some failures to increase risk
    for (int i = 0; i < 5; ++i) {
        telemetry.recordFailure(node_id, FailureType::TIMEOUT, 0, "TEST");
    }
    
    uint32_t b2 = budget.getAdaptedBudget("e2", node_id);
    // 15000 * (1.0 + (5 * 0.05)) = 15000 * 1.25 -> clamped to 1.20 -> 18000
    EXPECT_EQ(b2, 18000);
}

TEST(SelfHealingTest, FailureLearning) {
    auto& telemetry = FailureTelemetryStore::getInstance();
    std::string node_id = "learn_node";
    
    telemetry.recordFailure(node_id, FailureType::JNI_EXCEPTION, 1, "RETRY_SUCCESS");
    
    auto failures = telemetry.getRecentFailures(1);
    ASSERT_FALSE(failures.empty());
    EXPECT_EQ(failures[0].node_id, node_id);
    EXPECT_EQ(failures[0].type, FailureType::JNI_EXCEPTION);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
