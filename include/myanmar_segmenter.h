#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace Ronin::Kernel::NLP {

struct TrieNode {
    std::unordered_map<char32_t, std::unique_ptr<TrieNode>> children;
    bool is_end = false;
    bool is_stopword = false;
};

class MyanmarSegmenter {
public:
    MyanmarSegmenter();
    ~MyanmarSegmenter();

    // Dictionary Management
    bool loadDictionary(const std::string& path);
    void insert(const std::string& word, bool is_stopword);

    // Core Segmentation (Hybrid Pipeline v4.5)
    // Returns a space-separated string of keywords
    std::string segment(const std::string& input);

private:
    std::unique_ptr<TrieNode> m_root;

    // Phase 1: Syllable Breaking (Rule-based)
    std::vector<std::u32string> breakSyllables(const std::u32string& input);

    // Phase 2: Bi-directional MaxMatch on Syllable Clusters
    std::vector<std::string> forwardMaxMatch(const std::vector<std::u32string>& syllables);
    std::vector<std::string> reverseMaxMatch(const std::vector<std::u32string>& syllables);
    
    // Phase 3: Disambiguation & Particle Heuristics
    std::vector<std::string> resolveAmbiguity(const std::vector<std::string>& fmm, const std::vector<std::string>& rmm);

    // Unicode Helpers
    std::u32string utf8_to_utf32(const std::string& str);
    std::string utf32_to_utf8(const std::u32string& str);
};

} // namespace Ronin::Kernel::NLP
