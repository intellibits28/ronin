#include <gtest/gtest.h>
#include "reflection_engine.h"
#include "dsp/vibe_monitor.h"
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
    auto notes = ltm->searchNotes("SEND_SMS failed");
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

    // 2.b Location saving test
    {
        AgentPlan save_plan = planner.createPlan("Save my current location as Home in memory");
        EXPECT_EQ(save_plan.intent_name, "MEMORY");
        EXPECT_EQ(save_plan.plan_steps.size(), 2);
        EXPECT_EQ(save_plan.plan_steps[0], "GET_LOCATION");
        EXPECT_EQ(save_plan.plan_steps[1], "SAVE_FACT");
        EXPECT_EQ(save_plan.parameters["entity"], "Home");
        EXPECT_EQ(save_plan.parameters["attribute"], "Coordinates");
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

#include "capabilities/discovery_engine.h"

TEST_F(EvolutionTest, CapabilityDiscoveryEngineDAG) {
    IntentEngine engine(ltm.get());
    Reasoning::CapabilityDiscoveryEngine discovery_engine;

    // 1. Resolve capabilities based on requirements
    std::vector<std::string> requirements = {"Need frequency spectrum analysis of the audio"};
    auto resolved = discovery_engine.resolveCapabilities(requirements);

    EXPECT_FALSE(resolved.empty());
    bool found_fft = false;
    for (const auto& tool : resolved) {
        if (tool.name == "fft") {
            found_fft = true;
            break;
        }
    }
    EXPECT_TRUE(found_fft);

    // 2. Add dynamic sensor tool to registry for dependency testing
    auto& registry = Capability::ToolRegistry::getInstance();
    Capability::ToolMetadata mic_meta;
    mic_meta.name = "audio_capture";
    mic_meta.description = "Captures raw mic input array of floats";
    mic_meta.inputs = {};
    mic_meta.outputs = {"float_array"};
    registry.registerTool(mic_meta, [](const std::string&, ToolContext*) {
        return "[0.0, 1.0, 0.0]";
    });

    // 3. Build execution graph with dependency resolution
    std::vector<Capability::ToolMetadata> tools_to_schedule = {
        registry.searchTools("fft")[0],
        registry.searchTools("audio_capture")[0]
    };

    // We start with NO initial inputs. "audio_capture" has no inputs so it runs first,
    // producing "float_array". Then "fft" runs since its input "float_array" is satisfied.
    std::vector<std::string> initial_inputs = {};
    auto graph = discovery_engine.buildExecutionGraph(tools_to_schedule, initial_inputs);

    ASSERT_EQ(graph.size(), 2);
    EXPECT_EQ(graph[0], "audio_capture");
    EXPECT_EQ(graph[1], "fft");
}

#include "capabilities/skill_compiler.h"

TEST_F(EvolutionTest, SkillCompilerPatternPromotion) {
    IntentEngine engine(ltm.get());
    auto& registry = Capability::ToolRegistry::getInstance();

    // 1. Mock 5 successful episodes of audio_capture followed by fft
    // payload_json contains the executed_steps array
    for (int i = 0; i < 5; ++i) {
        std::string payload = "{\"executed_steps\": [\"audio_capture\", \"fft\"]}";
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT INTO episodes (intent, summary, outcome_enum, payload_json, timestamp) VALUES ('TEST_INTENT', 'success', 1, ?, ?);";
        ASSERT_EQ(sqlite3_prepare_v2(ltm->getDatabase(), sql, -1, &stmt, nullptr), SQLITE_OK);
        sqlite3_bind_text(stmt, 1, payload.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, std::time(nullptr) - i);
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_DONE);
        sqlite3_finalize(stmt);
    }

    // 2. Trigger compiler with threshold 5
    Reasoning::SkillCompiler::compileAndPromoteSkills(ltm.get(), 5);

    // 3. Verify that the new macro-skill is compiled and registered
    auto matches = registry.searchTools("macro_skill_audio_capture_fft");
    ASSERT_FALSE(matches.empty());
    EXPECT_EQ(matches[0].name, "macro_skill_audio_capture_fft");

    // 4. Verify that execution runs all chained steps (audio_capture -> fft)
    // audio_capture returns [0.0, 1.0, 0.0] which fft processes
    std::string result = registry.execute("macro_skill_audio_capture_fft", "{}");
    EXPECT_FALSE(result.empty());
    EXPECT_EQ(result.find("Error"), std::string::npos);

    nlohmann::json jOut = nlohmann::json::parse(result);
    EXPECT_TRUE(jOut.contains("frequencies"));
    EXPECT_TRUE(jOut.contains("magnitudes"));

    // 5. Verify that it was stored as a consolidated note/lesson in long-term memory
    auto notes = ltm->searchNotes("macro_skill_audio_capture_fft");
    EXPECT_FALSE(notes.empty());
}

TEST_F(EvolutionTest, PerceptionStateInjectionInChat) {
    // 1. Insert a mock walking state in perception_history table
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO perception_history (timestamp, state_type, state_value) VALUES (?, 'physical_activity', 'walking');";
    ASSERT_EQ(sqlite3_prepare_v2(ltm->getDatabase(), sql, -1, &stmt, nullptr), SQLITE_OK);
    sqlite3_bind_int64(stmt, 1, std::time(nullptr));
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_DONE);
    sqlite3_finalize(stmt);

    // 2. Verify state retrieval via getLatestPerceptionState()
    std::string val = ltm->getLatestPerceptionState();
    EXPECT_EQ(val, "walking");
}

TEST_F(EvolutionTest, AdaptiveSensorAnalysisPipeline) {
    auto& engine = Ronin::Kernel::DSP::VibeMonitorEngine::getInstance();

    // 1. Test State Machine Transitions & Adaptive Profiles
    engine.getController().transitionToState(Ronin::Kernel::DSP::KernelSensorState::STARTUP);
    EXPECT_EQ(engine.getController().getActiveProfile().profile_name, "STRUCTURAL_RESONANCE");
    EXPECT_EQ(engine.getController().getActiveProfile().sample_rate_hz, 100.0f);
    EXPECT_EQ(engine.getController().getActiveProfile().window_size, 1024);
    EXPECT_EQ(engine.getController().getActiveProfile().high_pass_cutoff_hz, 1.0f);

    engine.getController().transitionToState(Ronin::Kernel::DSP::KernelSensorState::STABLE);
    EXPECT_EQ(engine.getController().getActiveProfile().profile_name, "MACHINE_DIAGNOSTICS");
    EXPECT_EQ(engine.getController().getActiveProfile().sample_rate_hz, 200.0f);
    EXPECT_EQ(engine.getController().getActiveProfile().window_size, 1024);

    // 2. Test Execution Command JSON switching state
    std::string res_str = engine.executeCommandJson("{\"state\":\"IDLE\"}");
    auto jRes = nlohmann::json::parse(res_str);
    EXPECT_EQ(jRes["state"], "IDLE");
    EXPECT_EQ(jRes["profile_name"], "IDLE_STANDBY");
    EXPECT_EQ(jRes["dc_removed"], true);

    // 3. Test Dynamic Thresholding & Moving StdDev
    engine.getController().resetMetrics();
    engine.getController().pushSignalMetric(10.0f);
    engine.getController().pushSignalMetric(12.0f);
    engine.getController().pushSignalMetric(11.0f);
    EXPECT_NEAR(engine.getController().calculateMovingMean(), 11.0f, 0.01f);
    EXPECT_GT(engine.getController().calculateMovingStdDev(), 0.0f);
    EXPECT_GT(engine.getController().getDynamicThreshold(), 11.0f);

    // 4. Test IMPULSE_MODE burst capture and result format
    std::string imp_str = engine.executeCommandJson("{\"state\":\"IMPULSE_MODE\"}");
    auto jImp = nlohmann::json::parse(imp_str);
    EXPECT_EQ(jImp["impact_detected"], true);
    std::string summary_str = jImp["summary"];
    EXPECT_NE(summary_str.find("[IMPULSE] Impact Detected"), std::string::npos);
}

TEST_F(EvolutionTest, GuitarTunerBandPassAndHPS) {
    auto& engine = Ronin::Kernel::DSP::VibeMonitorEngine::getInstance();

    // 1. Configure E4 String Tuning Profile
    std::string res_json = engine.executeCommandJson("{\"tuner_string\":\"E4\"}");
    auto tp = engine.getController().getActiveTuningProfile();
    EXPECT_EQ(tp.string_name, "E4");
    EXPECT_NEAR(tp.fundamental_hz, 329.63f, 0.01f);
    EXPECT_NEAR(tp.bandpass_low_hz, 300.0f, 0.01f);
    EXPECT_NEAR(tp.bandpass_high_hz, 400.0f, 0.01f);

    // 2. Test BandPassBiquad isolation
    Ronin::Kernel::DSP::BandPassBiquad bp;
    bp.configure(2000.0f, 300.0f, 400.0f);
    // Low frequency ambient noise (50Hz) should be strongly attenuated
    float out_50 = 0.0f;
    for (int i = 0; i < 100; ++i) out_50 = bp.process(std::sin(2.0f * M_PI * 50.0f * i / 2000.0f));
    EXPECT_LT(std::abs(out_50), 0.15f);

    // Turn off tuner profile
    engine.executeCommandJson("{\"tuner_string\":\"NONE\"}");
    EXPECT_EQ(engine.getController().getActiveTuningProfile().fundamental_hz, 0.0f);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
