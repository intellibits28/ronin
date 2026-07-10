#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <random>
#include <fstream>
#include <iomanip>
#include "dsp/vibe_monitor.h"
#include <nlohmann/json.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace Ronin::Kernel::DSP;

struct SensorData {
    std::vector<float> x;
    std::vector<float> y;
    std::vector<float> z;
};

SensorData generateSensorWindow(float fx, float fy, float fz, float amp, float sigma, bool is_impulse = false, float decay_rate = 8.0f) {
    SensorData data;
    data.x.resize(1024);
    data.y.resize(1024);
    data.z.resize(1024);
    static double phase_z = 0.0;
    if (fx == 0.0f && fy == 0.0f && fz == 0.0f) {
        phase_z = 0.0;
        return data;
    }
    static std::mt19937 gen(42);
    std::normal_distribution<float> dist(0.0f, sigma);
    float fs = 100.0f;
    double dt = 1.0 / fs;
    for (int i = 0; i < 1024; ++i) {
        float t = static_cast<float>(i) / fs;
        float noise_x = dist(gen);
        float noise_y = dist(gen);
        float noise_z = dist(gen);
        data.x[i] = noise_x;
        data.y[i] = noise_y;
        if (is_impulse) {
            data.z[i] = 12.0f * std::exp(-decay_rate * t) * std::sin(2.0f * M_PI * fz * t) + noise_z;
        } else {
            phase_z += 2.0 * M_PI * fz * dt;
            data.z[i] = amp * std::sin(phase_z) + noise_z;
        }
    }
    return data;
}

struct BenchmarkMetrics {
    std::vector<double> latencies_ms;
    std::vector<double> tracking_errors;
    int total_outliers = 0;
    int gated_outliers = 0;
    int alarm_latency_windows = -1;
    double alarm_latency_seconds = -1.0;
    bool scenario1_pass = false;
    bool scenario2_pass = false;
    bool scenario3_pass = false;
    bool scenario4_pass = false;
    bool scenario5_pass = false;
};
int main() {
    std::cout << "======================================================================" << std::endl;
    std::cout << "          RONIN SHM STRESS EVALUATION & BENCHMARK RUNNER              " << std::endl;
    std::cout << "======================================================================" << std::endl;
    VibeMonitorEngine& engine = VibeMonitorEngine::getInstance();
    BenchmarkMetrics metrics;
    auto run_pipeline = [&](const SensorData& data, float expected_freq, bool is_outlier) -> VibeMonitorResult {
        auto start = std::chrono::high_resolution_clock::now();
        VibeMonitorResult res = engine.analyzePipeline(data.x, data.y, data.z);
        auto end = std::chrono::high_resolution_clock::now();
        double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
        metrics.latencies_ms.push_back(elapsed_ms);
        if (is_outlier) {
            metrics.total_outliers++;
            if (!res.selection_accepted) {
                metrics.gated_outliers++;
            }
        }
        if (res.selection_accepted && res.state != "IMPULSE_MODE" && expected_freq > 0.0f) {
            double err = std::abs(res.filtered_resonance_freq_hz - expected_freq);
            metrics.tracking_errors.push_back(err);
        }
        return res;
    };
    // Scenario 1: Baseline Verification
    std::cout << "\n[SCENARIO 1] Baseline Verification..." << std::endl;
    engine.resetForTest();
    generateSensorWindow(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    engine.getController().transitionToState(KernelSensorState::STARTUP);
    AdaptiveSamplingProfile prof = engine.getController().getActiveProfile();
    prof.profile_name = "STRUCTURAL_RESONANCE";
    prof.window_size = 1024;
    prof.mode = AnalysisMode::FREQUENCY_DOMAIN;
    prof.high_pass_cutoff_hz = 0.5f;
    prof.sample_rate_hz = 100.0f;
    engine.getController().setProfile(prof);
    VibeMonitorResult res1;
    bool baseline_tracked_correctly = false;
    bool baseline_healthy = false;
    for (int w = 1; w <= 10; ++w) {
        SensorData data = generateSensorWindow(5.0f, 5.15f, 5.3f, 1.5f, 0.05f);
        res1 = run_pipeline(data, 5.3f, false);
        std::cout << "  Window " << w << ": state=" << res1.state
                  << ", tracked=" << res1.resonance_freq_hz
                  << " Hz, filtered=" << res1.filtered_resonance_freq_hz
                  << " Hz, health=" << res1.health_index_pct
                  << "%, risk=" << res1.risk_level_str << std::endl;
        if (w >= 6) {
            if (std::abs(res1.filtered_resonance_freq_hz - 5.3f) < 0.2f) {
                baseline_tracked_correctly = true;
            }
            if (res1.risk_level == ShmRiskLevel::HEALTHY) {
                baseline_healthy = true;
            }
        }
    }
    metrics.scenario1_pass = baseline_tracked_correctly && baseline_healthy;
    std::cout << "Scenario 1 Status: " << (metrics.scenario1_pass ? "PASSED" : "FAILED") << std::endl;
    // Scenario 2: Transient Outlier Gating
    std::cout << "\n[SCENARIO 2] Transient Outlier Gating..." << std::endl;
    SensorData spike_data = generateSensorWindow(18.0f, 18.0f, 18.0f, 1.5f, 0.05f);
    VibeMonitorResult res2 = run_pipeline(spike_data, 5.3f, true);
    std::cout << "  Spike Window: accepted=" << (res2.selection_accepted ? "true" : "false")
              << ", filtered=" << res2.filtered_resonance_freq_hz
              << " Hz, outlier_state=" << res2.selection_outlier_state << std::endl;
    bool spike_gated = !res2.selection_accepted;
    bool spike_freq_stable = std::abs(res2.filtered_resonance_freq_hz - 5.3f) < 0.2f;
    for (int w = 1; w <= 2; ++w) {
        SensorData data = generateSensorWindow(5.0f, 5.15f, 5.3f, 1.5f, 0.05f);
        run_pipeline(data, 5.3f, false);
    }
    metrics.scenario2_pass = spike_gated && spike_freq_stable;
    std::cout << "Scenario 2 Status: " << (metrics.scenario2_pass ? "PASSED" : "FAILED") << std::endl;
    // Scenario 3: Candidate Shift
    std::cout << "\n[SCENARIO 3] Candidate Shift..." << std::endl;
    bool entered_candidate_state = false;
    bool candidate_freq_stable = true;
    for (int w = 1; w <= 3; ++w) {
        SensorData cand_data = generateSensorWindow(18.0f, 18.0f, 18.0f, 1.5f, 0.05f);
        VibeMonitorResult res3 = run_pipeline(cand_data, 5.3f, true);
        std::cout << "  Cand Window " << w << ": accepted=" << (res3.selection_accepted ? "true" : "false")
                  << ", filtered=" << res3.filtered_resonance_freq_hz
                  << " Hz, outlier_state=" << res3.selection_outlier_state
                  << ", streak=" << res3.selection_hysteresis_streak << std::endl;
        if (w == 3 && res3.selection_outlier_state == "HYSTERESIS_CANDIDATE") {
            entered_candidate_state = true;
        }
        if (std::abs(res3.filtered_resonance_freq_hz - 5.3f) > 0.2f) {
            candidate_freq_stable = false;
        }
    }
    for (int w = 1; w <= 4; ++w) {
        SensorData data = generateSensorWindow(5.0f, 5.15f, 5.3f, 1.5f, 0.05f);
        run_pipeline(data, 5.3f, false);
    }
    metrics.scenario3_pass = entered_candidate_state && candidate_freq_stable;
    std::cout << "Scenario 3 Status: " << (metrics.scenario3_pass ? "PASSED" : "FAILED") << std::endl;
    // Scenario 4: Structural Failure
    std::cout << "\n[SCENARIO 4] Structural Failure..." << std::endl;
    std::cout << "  Triggering impulse excitation..." << std::endl;
    engine.getController().transitionToState(KernelSensorState::IMPULSE_MODE);
    SensorData impulse_win = generateSensorWindow(5.0f, 5.15f, 5.3f, 1.5f, 0.05f, true);
    VibeMonitorResult res_imp = run_pipeline(impulse_win, 5.3f, false);
    std::cout << "    Impulse Window: state=" << res_imp.state
              << ", impact_detected=" << (res_imp.impact_detected ? "true" : "false")
              << ", settle_count=" << engine.getController().getPostImpulseSettleCount() << std::endl;
    bool shift_detected = false;
    bool kalman_confirmed = false;
    bool risk_degraded_critical = false;
    int alarm_win_idx = -1;
    for (int w = 1; w <= 7; ++w) {
        SensorData shift_data;
        if (w == 1) {
            shift_data = generateSensorWindow(5.0f, 5.15f, 5.3f, 1.5f, 0.05f);
        } else {
            shift_data = generateSensorWindow(1.5f, 1.5f, 1.5f, 1.5f, 0.05f);
        }
        VibeMonitorResult res4 = run_pipeline(shift_data, (w == 7) ? 1.5f : 5.3f, w > 1);
        std::cout << "  Shift Window " << w << ": state=" << res4.state
                  << ", accepted=" << (res4.selection_accepted ? "true" : "false")
                  << ", tracked=" << res4.resonance_freq_hz
                  << " Hz, filtered=" << res4.filtered_resonance_freq_hz
                  << " Hz, outlier_state=" << res4.selection_outlier_state
                  << ", structural_shift=" << (res4.structural_shift_detected ? "true" : "false")
                  << ", health=" << res4.health_index_pct
                  << "%, risk=" << res4.risk_level_str << std::endl;
        if (res4.structural_shift_detected) {
            shift_detected = true;
        }
        if (w == 7 && res4.selection_outlier_state == "HYSTERESIS_CONFIRMED_SHIFT" && std::abs(res4.filtered_resonance_freq_hz - 1.5f) < 0.2f) {
            kalman_confirmed = true;
        }
        if (res4.risk_level == ShmRiskLevel::DEGRADED || res4.risk_level == ShmRiskLevel::CRITICAL) {
            if (!risk_degraded_critical) {
                risk_degraded_critical = true;
                alarm_win_idx = w;
            }
        }
    }
    if (alarm_win_idx != -1) {
        metrics.alarm_latency_windows = alarm_win_idx;
        metrics.alarm_latency_seconds = alarm_win_idx * 10.24;
    }
    metrics.scenario4_pass = shift_detected && kalman_confirmed && risk_degraded_critical;
    std::cout << "Scenario 4 Status: " << (metrics.scenario4_pass ? "PASSED" : "FAILED") << std::endl;
    // Scenario 5: High Noise
    std::cout << "\n[SCENARIO 5] High Noise..." << std::endl;
    engine.resetForTest();
    generateSensorWindow(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    engine.getController().transitionToState(KernelSensorState::STARTUP);
    engine.getController().setProfile(prof);
    for (int w = 1; w <= 6; ++w) {
        SensorData data = generateSensorWindow(5.0f, 5.15f, 5.3f, 1.5f, 0.05f);
        run_pipeline(data, 5.3f, false);
    }
    bool confidence_dropped = false;
    for (int w = 1; w <= 3; ++w) {
        SensorData noise_data = generateSensorWindow(5.0f, 5.15f, 5.3f, 1.5f, 1.2f);
        VibeMonitorResult res5 = run_pipeline(noise_data, 5.3f, false);
        std::cout << "  Noise Window " << w << ": state=" << res5.state
                  << ", tracked=" << res5.resonance_freq_hz
                  << " Hz, filtered=" << res5.filtered_resonance_freq_hz
                  << " Hz, snr=" << res5.snr_db
                  << " dB, modal_confidence=" << res5.modal_confidence_pct
                  << "%" << std::endl;
        if (res5.modal_confidence_pct < 40.0f) {
            confidence_dropped = true;
        }
    }
    metrics.scenario5_pass = confidence_dropped;
    std::cout << "Scenario 5 Status: " << (metrics.scenario5_pass ? "PASSED" : "FAILED") << std::endl;
    // Calculate Summary Metrics
    double min_lat = *std::min_element(metrics.latencies_ms.begin(), metrics.latencies_ms.end());
    double max_lat = *std::max_element(metrics.latencies_ms.begin(), metrics.latencies_ms.end());
    double sum_lat = std::accumulate(metrics.latencies_ms.begin(), metrics.latencies_ms.end(), 0.0);
    double mean_lat = sum_lat / metrics.latencies_ms.size();
    double tmp_sum = 0.0;
    for (double val : metrics.latencies_ms) {
        tmp_sum += (val - mean_lat) * (val - mean_lat);
    }
    double stddev_lat = std::sqrt(tmp_sum / metrics.latencies_ms.size());
    double mae_tracking = 0.0;
    if (!metrics.tracking_errors.empty()) {
        double sum_err = std::accumulate(metrics.tracking_errors.begin(), metrics.tracking_errors.end(), 0.0);
        mae_tracking = sum_err / metrics.tracking_errors.size();
    }
    double gating_rate = 0.0;
    if (metrics.total_outliers > 0) {
        gating_rate = static_cast<double>(metrics.gated_outliers) / metrics.total_outliers * 100.0;
    }
    std::cout << "\n======================================================================" << std::endl;
    std::cout << "                         METRICS REPORT TABLE                         " << std::endl;
    std::cout << "======================================================================" << std::endl;
    std::cout << std::left << std::setw(40) << "Metric Name" << "Value" << std::endl;
    std::cout << "----------------------------------------------------------------------" << std::endl;
    std::cout << std::left << std::setw(40) << "Min Pipeline Latency (ms):" << min_lat << std::endl;
    std::cout << std::left << std::setw(40) << "Max Pipeline Latency (ms):" << max_lat << std::endl;
    std::cout << std::left << std::setw(40) << "Mean Pipeline Latency (ms):" << mean_lat << std::endl;
    std::cout << std::left << std::setw(40) << "StdDev Pipeline Latency (ms):" << stddev_lat << std::endl;
    std::cout << std::left << std::setw(40) << "Kalman Tracking MAE (Hz):" << mae_tracking << std::endl;
    std::cout << std::left << std::setw(40) << "Gating Success Rate (%):" << gating_rate << "% (" << metrics.gated_outliers << "/" << metrics.total_outliers << ")" << std::endl;
    std::cout << std::left << std::setw(40) << "Alarm Latency (windows):" << metrics.alarm_latency_windows << std::endl;
    std::cout << std::left << std::setw(40) << "Alarm Latency (seconds):" << metrics.alarm_latency_seconds << " s" << std::endl;
    std::cout << "======================================================================" << std::endl;
    nlohmann::json report;
    report["latency_metrics"] = {
        {"min_ms", min_lat},
        {"max_ms", max_lat},
        {"mean_ms", mean_lat},
        {"stddev_ms", stddev_lat}
    };
    report["kalman_tracking_error_mae_hz"] = mae_tracking;
    report["gating_success_rate_pct"] = gating_rate;
    report["alarm_latency_windows"] = metrics.alarm_latency_windows;
    report["alarm_latency_seconds"] = metrics.alarm_latency_seconds;
    report["scenarios"] = {
        {"scenario_1_baseline_verification", metrics.scenario1_pass ? "PASSED" : "FAILED"},
        {"scenario_2_transient_outlier_gating", metrics.scenario2_pass ? "PASSED" : "FAILED"},
        {"scenario_3_candidate_shift", metrics.scenario3_pass ? "PASSED" : "FAILED"},
        {"scenario_4_structural_failure", metrics.scenario4_pass ? "PASSED" : "FAILED"},
        {"scenario_5_high_noise", metrics.scenario5_pass ? "PASSED" : "FAILED"}
    };
    report["all_scenarios_passed"] = (metrics.scenario1_pass && metrics.scenario2_pass && metrics.scenario3_pass && metrics.scenario4_pass && metrics.scenario5_pass);

    std::string report_path = "/data/data/com.termux/files/home/play-ground/ronin/shm_benchmark_report.json";
    std::ofstream out(report_path);
    if (out.is_open()) {
        out << report.dump(4);
        out.close();
        std::cout << "JSON report written to: " << report_path << std::endl;
    }
    
    std::string local_report_path = "shm_benchmark_report.json";
    std::ofstream out_local(local_report_path);
    if (out_local.is_open()) {
        out_local << report.dump(4);
        out_local.close();
        std::cout << "JSON report written to local path: " << local_report_path << std::endl;
    }

    std::string shm_stress_results_path = "shm_stress_results.json";
    std::ofstream out_stress(shm_stress_results_path);
    if (out_stress.is_open()) {
        out_stress << report.dump(4);
        out_stress.close();
        std::cout << "JSON report written to stress results: " << shm_stress_results_path << std::endl;
    }

    bool all_passed = (metrics.scenario1_pass && metrics.scenario2_pass && metrics.scenario3_pass && metrics.scenario4_pass && metrics.scenario5_pass);
    return all_passed ? 0 : 1;
}
