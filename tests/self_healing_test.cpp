#include <gtest/gtest.h>
#include "runtime_healing_controller.h"
#include "adaptive_budget_controller.h"
#include "failure_telemetry_bus.h"
#include "execution_checkpoint_store.h"
#include "execution_context.h"
#include <thread>
#include <chrono>

using namespace Ronin::Kernel::Execution;
using namespace Ronin::Kernel;

TEST(SelfHealingTest, RecoveryCheckpointing) {
    auto& store = ExecutionCheckpointStore::getInstance();
    auto ctx = std::make_shared<ExecutionContext>();
    ctx->session_id = "s1";
    ctx->execution_id = "e1";
    
    EXPECT_TRUE(store.saveCheckpoint(ctx, "{}"));
    EXPECT_EQ(store.getPendingExecutions().size(), 1);
}

TEST(SelfHealingTest, AdaptiveBudgetTuning) {
    auto& budget = AdaptiveBudgetController::getInstance();
    auto& telemetry = FailureTelemetryBus::getInstance();
    std::string node_id = "heavy_node";
    
    // Initial budget
    uint32_t b1 = budget.getAdaptedBudget("e1", node_id);
    EXPECT_EQ(b1, 15000);
    
    // Simulate some failures to increase risk
    for (int i = 0; i < 5; ++i) {
        telemetry.logFailure("e", node_id, FailureType::TIMEOUT, "TEST");
    }
    
    uint32_t b2 = budget.getAdaptedBudget("e2", node_id);
    // 15000 * (1.0 + (5 * 0.05)) = 15000 * 1.25 -> clamped to 1.20 -> 18000
    EXPECT_EQ(b2, 18000);
}

TEST(SelfHealingTest, FailureLearning) {
    auto& telemetry = FailureTelemetryBus::getInstance();
    std::string node_id = "learn_node";
    
    telemetry.logFailure("e1", node_id, FailureType::JNI_EXCEPTION, "RETRY_SUCCESS");
    
    auto failures = telemetry.getRecentFailures(node_id, 1);
    ASSERT_FALSE(failures.empty());
    EXPECT_EQ(failures[0].node_id, node_id);
    EXPECT_EQ(failures[0].type, FailureType::JNI_EXCEPTION);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
