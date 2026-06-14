#include <gtest/gtest.h>
#include "execution_context.h"
#include "execution_budget.h"
#include "jni_gateway.h"
#include "agent_session.h"
#include "ronin_log.h"
#include <thread>
#include <chrono>

using namespace Ronin::Kernel::Execution;
using namespace Ronin::Kernel::JNI;
using namespace Ronin::Kernel;

TEST(GovernanceTest, BudgetAllocationAndConsumption) {
    auto& controller = ExecutionBudgetController::getInstance();
    std::string exec_id = "test-exec-1";
    
    controller.allocateBudget(exec_id, 1000);
    EXPECT_EQ(controller.getRemaining(exec_id), 1000);
    
    EXPECT_TRUE(controller.consumeBudget(exec_id, 400));
    EXPECT_EQ(controller.getRemaining(exec_id), 600);
    
    EXPECT_FALSE(controller.consumeBudget(exec_id, 700));
    EXPECT_EQ(controller.getRemaining(exec_id), 0);
    
    controller.revokeBudget(exec_id);
    EXPECT_EQ(controller.getRemaining(exec_id), 0);
}

TEST(GovernanceTest, CancellationPropagation) {
    auto& gateway = JniExecutionGateway::getInstance();
    auto ctx = std::make_shared<ExecutionContext>();
    ctx->session_id = "s1";
    ctx->execution_id = "e1";
    
    gateway.registerExecution(ctx);
    EXPECT_FALSE(ctx->cancel_token->isCancelled());
    
    gateway.propagateCancellation("e1");
    EXPECT_TRUE(ctx->cancel_token->isCancelled());
}

TEST(GovernanceTest, SafeModeTrigger) {
    auto& gateway = JniExecutionGateway::getInstance();
    auto ctx1 = std::make_shared<ExecutionContext>();
    ctx1->session_id = "s1"; ctx1->execution_id = "e1";
    
    auto ctx2 = std::make_shared<ExecutionContext>();
    ctx2->session_id = "s2"; ctx2->execution_id = "e2";
    
    gateway.registerExecution(ctx1);
    gateway.registerExecution(ctx2);
    
    gateway.triggerSafeMode();
    
    EXPECT_TRUE(ctx1->cancel_token->isCancelled());
    EXPECT_TRUE(ctx2->cancel_token->isCancelled());
    EXPECT_EQ(gateway.getContext("e1"), nullptr);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
