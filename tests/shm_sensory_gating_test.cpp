#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <string>
#include "dsp/vibe_monitor.h"
#include <nlohmann/json.hpp>

using namespace Ronin::Kernel::DSP;
using namespace Ronin::Kernel::Reasoning;

static std::vector<float> generateSineSignal(float freq_hz, float sample_rate_hz = 100.0f, uint32_t samples = 1024) {
    std::vector<float> sig(samples);
    for (uint32_t i = 0; i < samples; ++i) {
        float t = static_cast<float>(i) / sample_rate_hz;
        sig[i] = 1.0f * std::sin(2.0f * static_cast<float>(M_PI) * freq_hz * t);
    }
    return sig;
}

TEST(ShmSensoryGatingTest, BaselineGazeIsQuiescent) {
    auto& engine = VibeMonitorEngine::getInstance();
    engine.resetForTest();

    // Configure structural profile
    AdaptiveSamplingProfile prof;
    prof.profile_name = "STRUCTURAL_RESONANCE";
    prof.sample_rate_hz = 100.0f;
    prof.window_size = 1024;
    prof.mode = AnalysisMode::FREQUENCY_DOMAIN;
    prof.high_pass_cutoff_hz = 1.0f;
    prof.dynamic_std_dev_multiplier = 2.0f;
    engine.getController().setProfile(prof);
    engine.getController().captureBaseline(10.0f);

    // Baseline baseline profile in Active Inference
    BaselineProfile bp;
    bp.mean_shm_hz = 10.0f;
    bp.sigma_shm_hz = 0.25f;
    engine.getActiveInferenceCore().setBaseline(bp);

    // Warm up filter to bypass SETTLING_SAMPLES
    auto warm_sig = generateSineSignal(10.0f, 100.0f, 1024);
    engine.analyzePipeline(warm_sig, warm_sig, warm_sig);

    // Run clean baseline 10Hz signal
    auto sig = generateSineSignal(10.0f, 100.0f, 1024);
    auto res = engine.analyzePipeline(sig, sig, sig);

    // Free energy should be low (< 0.8) and gaze QUIESCENT (10 Hz)
    EXPECT_LT(res.free_energy, ActiveInferenceCore::THRESHOLD_QUIESCENT);
    EXPECT_EQ(res.gaze_state, SensoryGazeState::QUIESCENT);
    EXPECT_EQ(res.active_gaze_rate_hz, 10u);
}

TEST(ShmSensoryGatingTest, ModerateShiftTriggersVigilantGaze) {
    auto& engine = VibeMonitorEngine::getInstance();
    engine.resetForTest();

    AdaptiveSamplingProfile prof;
    prof.profile_name = "STRUCTURAL_RESONANCE";
    prof.sample_rate_hz = 100.0f;
    prof.window_size = 1024;
    prof.mode = AnalysisMode::FREQUENCY_DOMAIN;
    prof.high_pass_cutoff_hz = 1.0f;
    prof.dynamic_std_dev_multiplier = 2.0f;
    engine.getController().setProfile(prof);
    engine.getController().captureBaseline(10.0f);

    BaselineProfile bp;
    bp.mean_shm_hz = 10.0f;
    bp.sigma_shm_hz = 0.25f;
    engine.getActiveInferenceCore().setBaseline(bp);

    // Warm up filter
    auto warm_sig = generateSineSignal(10.0f, 100.0f, 1024);
    engine.analyzePipeline(warm_sig, warm_sig, warm_sig);

    auto shift_sig = generateSineSignal(10.38f, 100.0f, 1024);
    auto res = engine.analyzePipeline(shift_sig, shift_sig, shift_sig);

    EXPECT_GE(res.free_energy, ActiveInferenceCore::THRESHOLD_QUIESCENT);
    EXPECT_LT(res.free_energy, ActiveInferenceCore::THRESHOLD_VIGILANT);
    EXPECT_EQ(res.gaze_state, SensoryGazeState::VIGILANT);
    EXPECT_EQ(res.active_gaze_rate_hz, 50u);
}

TEST(ShmSensoryGatingTest, SevereShiftTriggersActiveInvestigation) {
    auto& engine = VibeMonitorEngine::getInstance();
    engine.resetForTest();

    AdaptiveSamplingProfile prof;
    prof.profile_name = "STRUCTURAL_RESONANCE";
    prof.sample_rate_hz = 100.0f;
    prof.window_size = 1024;
    prof.mode = AnalysisMode::FREQUENCY_DOMAIN;
    prof.high_pass_cutoff_hz = 1.0f;
    prof.dynamic_std_dev_multiplier = 2.0f;
    engine.getController().setProfile(prof);
    engine.getController().captureBaseline(10.0f);

    BaselineProfile bp;
    bp.mean_shm_hz = 10.0f;
    bp.sigma_shm_hz = 0.25f;
    engine.getActiveInferenceCore().setBaseline(bp);

    // Warm up filter
    auto warm_sig = generateSineSignal(10.0f, 100.0f, 1024);
    engine.analyzePipeline(warm_sig, warm_sig, warm_sig);

    // Severe shift by 1.8 Hz (> 7 sigma)
    auto severe_sig = generateSineSignal(11.8f, 100.0f, 1024);
    auto res = engine.analyzePipeline(severe_sig, severe_sig, severe_sig);

    EXPECT_GE(res.free_energy, ActiveInferenceCore::THRESHOLD_VIGILANT);
    EXPECT_EQ(res.gaze_state, SensoryGazeState::ACTIVE_INVESTIGATION);
    EXPECT_EQ(res.active_gaze_rate_hz, 200u);
}

TEST(ShmSensoryGatingTest, JsonTelemetryIncludesActiveInference) {
    auto& engine = VibeMonitorEngine::getInstance();
    engine.resetForTest();

    std::string json_cmd = R"({"sensor_type": "RESONANCE", "mode": "FREQUENCY_DOMAIN"})";
    std::string out_json = engine.executeCommandJson(json_cmd);

    auto parsed = nlohmann::json::parse(out_json);
    EXPECT_TRUE(parsed.contains("free_energy"));
    EXPECT_TRUE(parsed.contains("gaze_state"));
    EXPECT_TRUE(parsed.contains("active_gaze_rate_hz"));

    uint32_t rate = parsed["active_gaze_rate_hz"].get<uint32_t>();
    EXPECT_TRUE(rate == 10u || rate == 50u || rate == 200u);
}
