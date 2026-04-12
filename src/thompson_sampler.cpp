#include "thompson_sampler.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace Ronin::Kernel::Reasoning {

ThompsonSampler::ThompsonSampler() {
    std::random_device rd;
    m_rng.seed(rd());
    precomputeBetaTables(20);
}

/**
 * Pre-computes discrete approximations of Beta distributions for alpha, beta up to max_param.
 * This enables O(1) sampling at runtime.
 */
void ThompsonSampler::precomputeBetaTables(uint32_t max_param) {
    const int resolution = 100; // Granularity of our discrete Beta approximation
    
    for (uint32_t a = 1; a <= max_param; ++a) {
        for (uint32_t b = 1; b <= max_param; ++b) {
            std::vector<float> dist;
            float total = 0.0f;
            
            for (int i = 0; i < resolution; ++i) {
                float x = (static_cast<float>(i) + 0.5f) / static_cast<float>(resolution);
                // Beta PDF formula: x^(a-1) * (1-x)^(b-1)
                float p = std::pow(x, a - 1) * std::pow(1.0f - x, b - 1);
                dist.push_back(p);
                total += p;
            }

            // Normalize the distribution
            if (total > 0.0f) {
                for (auto& p : dist) p /= total;
            } else {
                // Fallback to uniform if calculation fails
                std::fill(dist.begin(), dist.end(), 1.0f / static_cast<float>(resolution));
            }

            buildAliasTable(dist, m_precomputed_tables[{a, b}]);
        }
    }
}

/**
 * Vose's Alias Method Table Construction
 */
void ThompsonSampler::buildAliasTable(const std::vector<float>& distribution, AliasTable& table) {
    size_t n = distribution.size();
    table.prob.resize(n);
    table.alias.resize(n);

    std::vector<float> scaled_p(n);
    std::vector<size_t> small, large;

    for (size_t i = 0; i < n; ++i) {
        scaled_p[i] = distribution[i] * static_cast<float>(n);
        if (scaled_p[i] < 1.0f) small.push_back(i);
        else large.push_back(i);
    }

    while (!small.empty() && !large.empty()) {
        size_t s = small.back(); small.pop_back();
        size_t l = large.back(); large.pop_back();

        table.prob[s] = scaled_p[s];
        table.alias[s] = static_cast<uint32_t>(l);

        scaled_p[l] = (scaled_p[l] + scaled_p[s]) - 1.0f;
        if (scaled_p[l] < 1.0f) small.push_back(l);
        else large.push_back(l);
    }

    while (!large.empty()) {
        size_t l = large.back(); large.pop_back();
        table.prob[l] = 1.0f;
    }
    while (!small.empty()) {
        size_t s = small.back(); small.pop_back();
        table.prob[s] = 1.0f;
    }
}

float ThompsonSampler::sampleBeta(uint32_t success, uint32_t failure) {
    uint32_t alpha = std::clamp(success + 1, 1u, 20u);
    uint32_t beta = std::clamp(failure + 1, 1u, 20u);

    const auto& table = m_precomputed_tables[{alpha, beta}];
    return sampleFromAliasTable(table);
}

float ThompsonSampler::sampleFromAliasTable(const AliasTable& table) {
    std::uniform_int_distribution<size_t> index_dist(0, table.prob.size() - 1);
    std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);

    size_t i = index_dist(m_rng);
    if (prob_dist(m_rng) < table.prob[i]) {
        return static_cast<float>(i + 0.5f) / static_cast<float>(table.prob.size());
    } else {
        return static_cast<float>(table.alias[i] + 0.5f) / static_cast<float>(table.prob.size());
    }
}

} // namespace Ronin::Kernel::Reasoning
