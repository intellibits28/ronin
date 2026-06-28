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

#include "intent_engine.h"

using namespace Ronin::Kernel::Intent;

TEST_F(EvolutionTest, FastPathSemanticRouterBypass) {
    TaskPlanner planner(nullptr, nullptr, ltm.get());
    
    // 1. Alarm test
    {
        AgentPlan alarm_plan = planner.createPlan("မနက် ၆ နာရီ နှိုးစက်ပေးပါ");
        EXPECT_EQ(alarm_plan.intent_name, "SET_ALARM");
        EXPECT_FALSE(alarm_plan.plan_steps.empty());
        EXPECT_EQ(alarm_plan.plan_steps[0], "SET_ALARM");
        EXPECT_EQ(alarm_plan.parameters["time"], "06:00");
    }

    // 2. Map test
    {
        AgentPlan map_plan = planner.createPlan("get my location");
        EXPECT_EQ(map_plan.intent_name, "LOCATION");
        EXPECT_FALSE(map_plan.plan_steps.empty());
        EXPECT_EQ(map_plan.plan_steps[0], "GET_LOCATION");
    }

    // 3. Vault test
    {
        AgentPlan vault_plan = planner.createPlan("lookup gemini api key from vault");
        EXPECT_EQ(vault_plan.intent_name, "LOOKUP_VAULT");
        EXPECT_FALSE(vault_plan.plan_steps.empty());
        EXPECT_EQ(vault_plan.plan_steps[0], "LOOKUP_VAULT");
        EXPECT_EQ(vault_plan.parameters["vault_title"], "gemini api key");
    }

    // 4. File search test
    {
        AgentPlan file_plan = planner.createPlan("find file invoice.pdf");
        EXPECT_EQ(file_plan.intent_name, "FILE_SEARCH");
        EXPECT_FALSE(file_plan.plan_steps.empty());
        EXPECT_EQ(file_plan.plan_steps[0], "FILE_SEARCH");
        EXPECT_EQ(file_plan.parameters["query"], "invoice.pdf");
    }
}

#include "capabilities/tool_registry.h"
#include <nlohmann/json.hpp>

TEST_F(EvolutionTest, ToolRegistryAndDSPCapabilities) {
    // IntentEngine constructor registers everything
    IntentEngine engine(ltm.get());

    auto& registry = Capability::ToolRegistry::getInstance();

    // Verify default tools are registered
    auto file_tool = registry.getSkill("file_search");
    EXPECT_NE(file_tool, nullptr);

    auto flash_tool = registry.getSkill("flashlight");
    EXPECT_NE(flash_tool, nullptr);

    // Verify DSP tools are searchable
    auto dsp_list = registry.searchTools("fft");
    EXPECT_FALSE(dsp_list.empty());
    EXPECT_EQ(dsp_list[0].name, "fft");

    // Test FFT Execution
    {
        nlohmann::json jInput = {0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, -1.0f}; // Simple sine wave segment
        std::string res = registry.execute("fft", jInput.dump());
        EXPECT_FALSE(res.empty());
        EXPECT_EQ(res.find("Error"), std::string::npos);

        nlohmann::json jOut = nlohmann::json::parse(res);
        EXPECT_TRUE(jOut.contains("frequencies"));
        EXPECT_TRUE(jOut.contains("magnitudes"));
    }

    // Test Lowpass Filter Execution
    {
        nlohmann::json jInput;
        jInput["array"] = std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        jInput["cutoff_hz"] = 5.0f;
        jInput["sample_rate"] = 100.0f;

        std::string res = registry.execute("lowpass", jInput.dump());
        EXPECT_FALSE(res.empty());
        EXPECT_EQ(res.find("Error"), std::string::npos);

        nlohmann::json jOut = nlohmann::json::parse(res);
        EXPECT_TRUE(jOut.contains("filtered"));
        EXPECT_EQ(jOut["filtered"].size(), 5);
    }

    // Test Peak Detection Execution
    {
        nlohmann::json jInput;
        jInput["array"] = std::vector<float>{0.1f, 0.2f, 1.5f, 0.2f, 0.1f, 1.8f, 0.3f};
        jInput["threshold"] = 0.5f;

        std::string res = registry.execute("detect_peaks", jInput.dump());
        nlohmann::json jOut = nlohmann::json::parse(res);
        EXPECT_EQ(jOut["count"], 2); // indices 2 (1.5) and 5 (1.8)
    }

    // Test Zero Crossing Execution
    {
        std::vector<float> input = {1.0f, -1.0f, 1.0f, -1.0f};
        std::string res = registry.execute("zero_crossing", nlohmann::json(input).dump());
        nlohmann::json jOut = nlohmann::json::parse(res);
        EXPECT_EQ(jOut["crossings"], 3);
    }

    // Test RMS Execution
    {
        std::vector<float> input = {1.0f, 1.0f, 1.0f, 1.0f};
        std::string res = registry.execute("rms", nlohmann::json(input).dump());
        nlohmann::json jOut = nlohmann::json::parse(res);
        EXPECT_FLOAT_EQ(jOut["rms"], 1.0f);
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
