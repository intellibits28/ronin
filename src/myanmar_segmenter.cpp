#include "myanmar_segmenter.h"
#include <fstream>
#include <sstream>
#include <locale>
#include <codecvt>
#include <algorithm>
#include "ronin_log.h"

#define TAG "MyanmarSegmenter"

namespace Ronin::Kernel::NLP {

MyanmarSegmenter::MyanmarSegmenter() : m_root(std::make_unique<TrieNode>()) {}
MyanmarSegmenter::~MyanmarSegmenter() = default;

std::u32string MyanmarSegmenter::utf8_to_utf32(const std::string& str) {
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
    return conv.from_bytes(str);
}

std::string MyanmarSegmenter::utf32_to_utf8(const std::u32string& str) {
    std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> conv;
    return conv.to_bytes(str);
}

void MyanmarSegmenter::insert(const std::string& word, bool is_stopword) {
    if (word.empty()) return;
    std::u32string u32word = utf8_to_utf32(word);
    TrieNode* curr = m_root.get();
    for (char32_t c : u32word) {
        if (curr->children.find(c) == curr->children.end()) {
            curr->children[c] = std::make_unique<TrieNode>();
        }
        curr = curr->children[c].get();
    }
    curr->is_end = true;
    curr->is_stopword = is_stopword;
}

bool MyanmarSegmenter::loadDictionary(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOGE(TAG, "Failed to open dictionary: %s", path.c_str());
        return false;
    }

    std::string line;
    int count = 0;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        size_t sep = line.find('|');
        if (sep != std::string::npos) {
            std::string word = line.substr(0, sep);
            bool is_stop = (line.substr(sep + 1) == "s");
            insert(word, is_stop);
            count++;
        }
    }
    LOGI(TAG, "Loaded %d words into Trie Segmenter.", count);
    return true;
}

std::string MyanmarSegmenter::segment(const std::string& input) {
    if (input.empty()) return "";
    
    std::u32string u32input = utf8_to_utf32(input);
    std::vector<std::string> keywords;
    size_t i = 0;
    
    while (i < u32input.length()) {
        TrieNode* curr = m_root.get();
        size_t longest_match = 0;
        bool is_stop = false;

        // MaxMatch Logic
        for (size_t j = i; j < u32input.length(); ++j) {
            if (curr->children.find(u32input[j]) == curr->children.end()) break;
            curr = curr->children[u32input[j]].get();
            if (curr->is_end) {
                longest_match = j - i + 1;
                is_stop = curr->is_stopword;
            }
        }

        if (longest_match > 0) {
            if (!is_stop) {
                keywords.push_back(utf32_to_utf8(u32input.substr(i, longest_match)));
            }
            i += longest_match;
        } else {
            // No match in dictionary, treat as single character and skip if it's whitespace
            char32_t unknown = u32input[i];
            if (unknown != U' ' && unknown != U'\n' && unknown != U'\t') {
                // Potential part of a word not in dict, keep it for now
                // keywords.push_back(utf32_to_utf8(u32input.substr(i, 1)));
            }
            i++;
        }
    }

    // Join with spaces
    std::string result;
    for (size_t k = 0; k < keywords.size(); ++k) {
        result += keywords[k];
        if (k < keywords.size() - 1) result += " ";
    }
    return result;
}

} // namespace Ronin::Kernel::NLP
