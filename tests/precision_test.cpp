#include <gtest/gtest.h>
#include "thompson_sampler.h"
#include <vector>
#include <numeric>
#include <cmath>
#include <iostream>

using namespace Ronin::Kernel::Reasoning;

/**
 * Precision Test: Verifies that our ThompsonSampler's discrete approximation 
 * matches the theoretical mean and variance of the Beta distribution.
 * 
 * Theoretical Mean: alpha / (alpha + beta)
 * Theoretical Variance: (alpha * beta) / ((alpha + beta)^2 * (alpha + beta + 1))
 */
TEST(ThompsonSamplerPrecision, BetaDistributionAccuracy) {
    ThompsonSampler sampler;
    const uint32_t alpha = 10;
    const uint32_t beta = 5;
    const int num_samples = 10000;

    std::vector<float> samples;
    samples.reserve(num_samples);

    for (int i = 0; i < num_samples; ++i) {
        samples.push_back(sampler.sampleBeta(alpha - 1, beta - 1));
    }

    // Calculate Empirical Mean
    float sum = std::accumulate(samples.begin(), samples.end(), 0.0f);
    float empirical_mean = sum / num_samples;

    // Calculate Empirical Variance
    float sq_sum = std::inner_product(samples.begin(), samples.end(), samples.begin(), 0.0f);
    float empirical_variance = (sq_sum / num_samples) - (empirical_mean * empirical_mean);

    // Theoretical Values
    float theoretical_mean = static_cast<float>(alpha) / (alpha + beta);
    float theoretical_variance = static_cast<float>(alpha * beta) / 
                                 (std::pow(alpha + beta, 2) * (alpha + beta + 1));

    // Error Margin Check (Less than 1% for Mean)
    float mean_error = std::abs(empirical_mean - theoretical_mean) / theoretical_mean;
    float variance_error = std::abs(empirical_variance - theoretical_variance) / theoretical_variance;

    std::cout << "Empirical Mean: " << empirical_mean << ", Theoretical Mean: " << theoretical_mean << std::endl;
    std::cout << "Mean Error: " << (mean_error * 100.0f) << "%" << std::endl;
    
    ASSERT_LT(mean_error, 0.01f) << "Mean error exceeded 1%";
    // Variance is more sensitive to sampling, allowing a slightly higher margin (5%) for variance
    ASSERT_LT(variance_error, 0.05f) << "Variance error exceeded 5%";
}
