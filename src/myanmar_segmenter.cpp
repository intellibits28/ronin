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
    if (!file.is_open()) return false;
    std::string line;
    int count = 0;
    while (std::getline(file, line)) {
        size_t sep = line.find('|');
        if (sep != std::string::npos) {
            insert(line.substr(0, sep), (line.substr(sep + 1) == "s"));
            count++;
        }
    }
    LOGI(TAG, "Loaded %d words.", count);
    return true;
}

// --- Phase 1: Syllable Breaking (Rule-based State Machine) ---
std::vector<std::u32string> MyanmarSegmenter::breakSyllables(const std::u32string& input) {
    std::vector<std::u32string> syllables;
    std::u32string current;
    
    for (size_t i = 0; i < input.length(); ++i) {
        char32_t c = input[i];
        
        // simple rule: break before any consonant (U+1000 to U+1021) 
        // if not preceded by Virama (U+1039)
        bool is_consonant = (c >= 0x1000 && c <= 0x1021);
        bool prev_is_virama = (i > 0 && input[i-1] == 0x1039);
        
        if (is_consonant && !prev_is_virama && !current.empty()) {
            syllables.push_back(current);
            current.clear();
        }
        current += c;
    }
    if (!current.empty()) syllables.push_back(current);
    return syllables;
}

// --- Phase 2: Forward MaxMatch ---
std::vector<std::string> MyanmarSegmenter::forwardMaxMatch(const std::vector<std::u32string>& syllables) {
    std::vector<std::string> result;
    size_t i = 0;
    while (i < syllables.size()) {
        TrieNode* curr = m_root.get();
        size_t longest_match = 0;
        bool is_stop = false;
        
        std::u32string combined;
        for (size_t j = i; j < syllables.size(); ++j) {
            combined += syllables[j];
            bool possible = true;
            TrieNode* temp_curr = curr;
            for (char32_t c : syllables[j]) {
                if (temp_curr->children.find(c) == temp_curr->children.end()) {
                    possible = false; break;
                }
                temp_curr = temp_curr->children[c].get();
            }
            if (!possible) break;
            curr = temp_curr;
            if (curr->is_end) {
                longest_match = j - i + 1;
                is_stop = curr->is_stopword;
            }
        }
        
        if (longest_match > 0) {
            std::u32string word;
            for (size_t k = 0; k < longest_match; ++k) word += syllables[i + k];
            if (!is_stop) result.push_back(utf32_to_utf8(word));
            i += longest_match;
        } else {
            // OOV: Keep as syllable if not whitespace
            std::string s = utf32_to_utf8(syllables[i]);
            if (s != " " && s != "\n") result.push_back(s);
            i++;
        }
    }
    return result;
}

// --- Phase 2: Reverse MaxMatch ---
std::vector<std::string> MyanmarSegmenter::reverseMaxMatch(const std::vector<std::u32string>& syllables) {
    std::vector<std::string> result;
    int i = static_cast<int>(syllables.size()) - 1;
    while (i >= 0) {
        size_t longest_match = 0;
        bool is_stop = false;
        
        for (int j = 0; j <= i; ++j) {
            std::u32string combined;
            for (int k = j; k <= i; ++k) combined += syllables[k];
            
            TrieNode* curr = m_root.get();
            bool match = true;
            for (char32_t c : combined) {
                if (curr->children.find(c) == curr->children.end()) { match = false; break; }
                curr = curr->children[c].get();
            }
            if (match && curr->is_end) {
                size_t len = i - j + 1;
                if (len > longest_match) {
                    longest_match = len;
                    is_stop = curr->is_stopword;
                }
            }
        }
        
        if (longest_match > 0) {
            std::u32string word;
            for (size_t k = 0; k < longest_match; ++k) word += syllables[i - longest_match + 1 + k];
            if (!is_stop) result.insert(result.begin(), utf32_to_utf8(word));
            i -= longest_match;
        } else {
            result.insert(result.begin(), utf32_to_utf8(syllables[i]));
            i--;
        }
    }
    return result;
}

// --- Phase 3: Disambiguation (Heuristic) ---
std::vector<std::string> MyanmarSegmenter::resolveAmbiguity(const std::vector<std::string>& fmm, const std::vector<std::string>& rmm) {
    // Standard Heuristic: Prefer the segmentation with fewer tokens
    if (fmm.size() < rmm.size()) return fmm;
    if (rmm.size() < fmm.size()) return rmm;
    
    // If same size, check for single-character tokens (prefer fewer of them)
    auto count_single = [](const std::vector<std::string>& tokens) {
        int count = 0;
        for (const auto& t : tokens) if (t.length() <= 3) count++; // UTF-8 Myanmar chars are 3 bytes
        return count;
    };
    
    if (count_single(fmm) < count_single(rmm)) return fmm;
    return rmm;
}

std::string MyanmarSegmenter::segment(const std::string& input) {
    if (input.empty()) return "";
    
    auto u32input = utf8_to_utf32(input);
    auto syllables = breakSyllables(u32input);
    
    auto fmm = forwardMaxMatch(syllables);
    auto rmm = reverseMaxMatch(syllables);
    
    auto final_tokens = resolveAmbiguity(fmm, rmm);
    
    std::string result;
    for (size_t k = 0; k < final_tokens.size(); ++k) {
        result += final_tokens[k];
        if (k < final_tokens.size() - 1) result += " ";
    }
    return result;
}

} // namespace Ronin::Kernel::NLP
