#pragma once

#include <vector>
#include <random>
#include <unordered_map>
#include <functional>

namespace Ronin::Kernel::Reasoning {

/**
 * Thompson Sampler using the Vose's Alias Method for O(1) sampling.
 * Pre-computes tables for Beta distributions (success, failure) <= 20.
 */
class ThompsonSampler {
public:
    ThompsonSampler();

    // Sample from a Beta(success + 1, failure + 1) distribution
    float sampleBeta(uint32_t success, uint32_t failure);

private:
    std::mt19937 m_rng;
    
    // Alias table for a discrete distribution
    struct AliasTable {
        std::vector<float> prob;
        std::vector<uint32_t> alias;
    };

    struct PairHash {
        template <class T1, class T2>
        std::size_t operator() (const std::pair<T1, T2> &pair) const {
            return std::hash<T1>()(pair.first) ^ std::hash<T2>()(pair.second);
        }
    };

    // Pre-computed Alias Tables indexed by (alpha, beta) pairs
    std::unordered_map<std::pair<uint32_t, uint32_t>, AliasTable, PairHash> m_precomputed_tables;

    void precomputeBetaTables(uint32_t max_param);
    void buildAliasTable(const std::vector<float>& distribution, AliasTable& table);
    float sampleFromAliasTable(const AliasTable& table);
};

} // namespace Ronin::Kernel::Reasoning
