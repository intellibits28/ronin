#include "reasoning/active_inference.hpp"
#include <cmath>
#include <algorithm>

namespace Ronin::Kernel::Reasoning {

void BaselineProfile::resetCalibration() {
    sample_count = 0;
    accum_shm_sum = 0.0;
    accum_shm_sq_sum = 0.0;
    is_calibrated = false;
}

void BaselineProfile::recordCalibrationSample(float shm_hz) {
    if (shm_hz > 0.0f && std::isfinite(shm_hz)) {
        accum_shm_sum += static_cast<double>(shm_hz);
        accum_shm_sq_sum += static_cast<double>(shm_hz) * static_cast<double>(shm_hz);
        sample_count++;
    }
}

bool BaselineProfile::finalizeCalibration(uint32_t min_samples) {
    if (sample_count < min_samples) {
        return false;
    }

    double count_d = static_cast<double>(sample_count);
    double mean = accum_shm_sum / count_d;
    double variance = (accum_shm_sq_sum / count_d) - (mean * mean);
    if (variance < 0.0) {
        variance = 0.0;
    }

    mean_shm_hz = static_cast<float>(mean);
    sigma_shm_hz = std::max(static_cast<float>(std::sqrt(variance)), MIN_SIGMA);
    is_calibrated = true;
    return true;
}

ActiveInferenceCore::ActiveInferenceCore(BaselineProfile baseline)
    : m_baseline(baseline) {}

void ActiveInferenceCore::setBaseline(const BaselineProfile& baseline) {
    m_baseline = baseline;
}

const BaselineProfile& ActiveInferenceCore::getBaseline() const {
    return m_baseline;
}

BaselineProfile& ActiveInferenceCore::getBaseline() {
    return m_baseline;
}

float ActiveInferenceCore::clampPrecision(float variance) {
    float safe_var = (std::isfinite(variance) && variance >= 0.0f) ? variance : 0.0f;
    float denom = safe_var + BaselineProfile::EPSILON;
    float prec = 1.0f / denom;
    return std::clamp(prec, BaselineProfile::MIN_PRECISION, BaselineProfile::MAX_PRECISION);
}

float ActiveInferenceCore::computeDimensionlessError(float observation, float prediction, float baseline_sigma) {
    float safe_sigma = std::max(baseline_sigma, BaselineProfile::MIN_SIGMA);
    return (observation - prediction) / safe_sigma;
}

SensoryGazeState ActiveInferenceCore::evaluateGaze(float free_energy) const {
    if (free_energy >= THRESHOLD_VIGILANT) {
        return SensoryGazeState::ACTIVE_INVESTIGATION;
    }
    if (free_energy >= THRESHOLD_QUIESCENT) {
        return SensoryGazeState::VIGILANT;
    }
    return SensoryGazeState::QUIESCENT;
}

uint32_t ActiveInferenceCore::getSampleRateForGaze(SensoryGazeState gaze) {
    switch (gaze) {
        case SensoryGazeState::QUIESCENT:
            return 10;
        case SensoryGazeState::VIGILANT:
            return 50;
        case SensoryGazeState::ACTIVE_INVESTIGATION:
            return 200;
        default:
            return 10;
    }
}

FreeEnergyState ActiveInferenceCore::computeFreeEnergy(
    const StateVector& state,
    const ObservationVector& observation,
    const std::array<float, MODALITY_COUNT>& realtime_variances
) const {
    FreeEnergyState result{};
    float total_free_energy = 0.0f;

    const std::array<float, MODALITY_COUNT> baseline_sigmas = {
        m_baseline.sigma_shm_hz,
        m_baseline.sigma_resource,
        m_baseline.sigma_intent,
        m_baseline.sigma_noise
    };

    // Hot-loop execution with zero heap allocations
    for (size_t i = 0; i < MODALITY_COUNT; ++i) {
        float norm_err = computeDimensionlessError(observation[i], state[i], baseline_sigmas[i]);
        float precision = clampPrecision(realtime_variances[i]);

        result.norm_errors[i] = norm_err;
        result.precisions[i] = precision;

        // F_i = 0.5 * (Pi_i * eps_i^2 - ln(Pi_i))
        float quadratic_term = precision * norm_err * norm_err;
        float log_term = std::log(precision);
        total_free_energy += 0.5f * (quadratic_term - log_term);
    }

    if (!std::isfinite(total_free_energy) || total_free_energy < 0.0f) {
        total_free_energy = 0.0f;
    }

    result.variational_free_energy = total_free_energy;
    result.gaze_state = evaluateGaze(total_free_energy);
    result.target_sample_rate_hz = getSampleRateForGaze(result.gaze_state);

    return result;
}

} // namespace Ronin::Kernel::Reasoning
