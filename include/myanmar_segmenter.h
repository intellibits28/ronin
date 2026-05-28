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

    // Core Segmentation (MaxMatch)
    // Returns a space-separated string of keywords (stop words filtered out)
    std::string segment(const std::string& input);

private:
    std::unique_ptr<TrieNode> m_root;

    // Unicode Helpers
    std::u32string utf8_to_utf32(const std::string& str);
    std::string utf32_to_utf8(const std::u32string& str);
};

} // namespace Ronin::Kernel::NLP
