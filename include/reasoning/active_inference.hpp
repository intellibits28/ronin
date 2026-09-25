#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <cmath>
#include <algorithm>
#include <string>

namespace Ronin::Kernel::Reasoning {

/**
 * Number of state/observation modalities in the Level 1 Active Inference Core:
 * 0: Structural Modal Frequency (f0 in Hz)
 * 1: Device Energy/Thermal Viability Index ([0, 1])
 * 2: Intent Certainty / Slot Completeness ([0, 1])
 * 3: Noise Floor / Environmental Disturbance ([0, 1])
 */
inline constexpr size_t MODALITY_COUNT = 4;

/**
 * Adaptive Sensory Gaze States based on Free Energy F:
 * - QUIESCENT: F < 0.8 (10 Hz sampling, quiescent power ~1.2 mA)
 * - VIGILANT: 0.8 <= F < 2.5 (50 Hz sampling, ~4.5 mA)
 * - ACTIVE_INVESTIGATION: F >= 2.5 (200 Hz sampling + full Welch PSD, ~14.0 mA)
 */
enum class SensoryGazeState : uint8_t {
    QUIESCENT = 0,
    VIGILANT = 1,
    ACTIVE_INVESTIGATION = 2
};

/**
 * Discrete Candidate Policies for Level 2 Expected Free Energy (EFE):
 */
enum class PolicyPrimitive : uint8_t {
    IDLE = 0,
    SAMPLE_HIRES = 1,
    CLARIFY_USER = 2,
    INSPECT_DOC = 3,
    EXEC_TOOL = 4,
    SELF_HEAL = 5
};

/**
 * Hidden State Vector s in R^4
 */
struct StateVector {
    float shm_freq_hz{0.0f};       // s[0]: Modal frequency f0 (Hz)
    float resource_index{1.0f};    // s[1]: Battery/Thermal viability [0, 1]
    float intent_certainty{1.0f};  // s[2]: User goal clarity [0, 1]
    float env_disturbance{0.0f};   // s[3]: Ambient disturbance [0, 1]

    constexpr float operator[](size_t idx) const {
        switch (idx) {
            case 0: return shm_freq_hz;
            case 1: return resource_index;
            case 2: return intent_certainty;
            case 3: return env_disturbance;
            default: return 0.0f;
        }
    }

    constexpr float& operator[](size_t idx) {
        switch (idx) {
            case 0: return shm_freq_hz;
            case 1: return resource_index;
            case 2: return intent_certainty;
            default: return env_disturbance;
        }
    }
};

/**
 * Observation Vector o in R^4
 */
struct ObservationVector {
    float shm_freq_hz{0.0f};       // o[0]: Peak resonance frequency from Welch PSD (Hz)
    float resource_metric{1.0f};   // o[1]: Real-time battery/thermal observation [0, 1]
    float intent_metric{1.0f};     // o[2]: Slot completeness & intent confidence [0, 1]
    float noise_metric{0.0f};      // o[3]: PSD noise floor / accelerometer energy [0, 1]

    constexpr float operator[](size_t idx) const {
        switch (idx) {
            case 0: return shm_freq_hz;
            case 1: return resource_metric;
            case 2: return intent_metric;
            case 3: return noise_metric;
            default: return 0.0f;
        }
    }

    constexpr float& operator[](size_t idx) {
        switch (idx) {
            case 0: return shm_freq_hz;
            case 1: return resource_metric;
            case 2: return intent_metric;
            default: return noise_metric;
        }
    }
};

/**
 * Empirical Baseline Calibration Profile
 */
struct BaselineProfile {
    float mean_shm_hz{10.0f};          // Calibrated f0 baseline
    float sigma_shm_hz{0.25f};         // Baseline standard deviation (default 0.25 Hz)
    float sigma_resource{0.05f};       // Baseline resource variation (5%)
    float sigma_intent{0.10f};         // Baseline intent variation (10%)
    float sigma_noise{0.05f};          // Baseline ambient noise variation

    // Clamping limits for numerical stability
    static constexpr float MIN_SIGMA = 1e-3f;
    static constexpr float MIN_PRECISION = 1e-3f;
    static constexpr float MAX_PRECISION = 1e+3f;
    static constexpr float EPSILON = 1e-6f;

    // Calibration accumulator
    uint32_t sample_count{0};
    double accum_shm_sum{0.0};
    double accum_shm_sq_sum{0.0};
    bool is_calibrated{false};

    void resetCalibration();
    void recordCalibrationSample(float shm_hz);
    bool finalizeCalibration(uint32_t min_samples = 30);
};

/**
 * Output of Level 1 Free Energy Computation
 */
struct FreeEnergyState {
    float variational_free_energy{0.0f};                 // F (scalar)
    std::array<float, MODALITY_COUNT> norm_errors{};     // Dimensionless prediction error eps_i
    std::array<float, MODALITY_COUNT> precisions{};      // Clamped precision Pi_i
    SensoryGazeState gaze_state{SensoryGazeState::QUIESCENT};
    uint32_t target_sample_rate_hz{10};
};

/**
 * Core Level 1 Active Inference Microkernel
 * Zero dynamic heap allocations in hot-loop (< 20 SIMD instructions).
 */
class ActiveInferenceCore {
public:
    explicit ActiveInferenceCore(BaselineProfile baseline = BaselineProfile{});

    // Baseline management
    void setBaseline(const BaselineProfile& baseline);
    const BaselineProfile& getBaseline() const;
    BaselineProfile& getBaseline();

    // Hot-Loop Level 1 Step
    FreeEnergyState computeFreeEnergy(
        const StateVector& state,
        const ObservationVector& observation,
        const std::array<float, MODALITY_COUNT>& realtime_variances
    ) const;

    // Gaze State Evaluation
    SensoryGazeState evaluateGaze(float free_energy) const;
    static uint32_t getSampleRateForGaze(SensoryGazeState gaze);

    // Mathematical Helpers
    static float clampPrecision(float variance);
    static float computeDimensionlessError(float observation, float prediction, float baseline_sigma);

    // Thresholds
    static constexpr float THRESHOLD_QUIESCENT = 0.8f;
    static constexpr float THRESHOLD_VIGILANT = 2.5f;

private:
    BaselineProfile m_baseline;
};

} // namespace Ronin::Kernel::Reasoning
