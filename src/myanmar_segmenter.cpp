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

// --- Phase 1: Syllable Breaking (Hardened v4.7) ---
std::vector<std::u32string> MyanmarSegmenter::breakSyllables(const std::u32string& input) {
    std::vector<std::u32string> syllables;
    std::u32string current;
    
    auto is_myanmar = [](char32_t c) { return (c >= 0x1000 && c <= 0x109F); };
    auto is_whitespace = [](char32_t c) { return (c == U' ' || c == U'\n' || c == U'\t' || c == U'\r'); };

    for (size_t i = 0; i < input.length(); ++i) {
        char32_t c = input[i];
        
        bool current_is_mm = is_myanmar(c);
        bool current_is_ws = is_whitespace(c);
        
        if (!current.empty()) {
            char32_t prev = current.back();
            bool prev_is_mm = is_myanmar(prev);
            bool prev_is_ws = is_whitespace(prev);
            
            // Break transitions: MM to Non-MM, Non-MM to MM, or any to Whitespace
            bool script_transition = (prev_is_mm != current_is_mm) || (prev_is_ws != current_is_ws);
            
            // Myanmar internal syllable break rule
            bool is_consonant = (c >= 0x1000 && c <= 0x1021);
            bool prev_is_virama = (prev == 0x1039);
            bool mm_break = current_is_mm && prev_is_mm && is_consonant && !prev_is_virama;

            if (script_transition || mm_break) {
                syllables.push_back(current);
                current.clear();
            }
        }
        current += c;
    }
    if (!current.empty()) syllables.push_back(current);
    return syllables;
}

// --- Phase 2: Forward MaxMatch (v4.7 with OOV Grouping) ---
std::vector<std::string> MyanmarSegmenter::forwardMaxMatch(const std::vector<std::u32string>& syllables) {
    std::vector<std::string> result;
    size_t i = 0;
    while (i < syllables.size()) {
        // Skip whitespace clusters
        if (syllables[i][0] == U' ' || syllables[i][0] == U'\n' || syllables[i][0] == U'\t' || syllables[i][0] == U'\r') {
            i++; continue;
        }

        TrieNode* curr = m_root.get();
        size_t longest_match = 0;
        bool is_stop = false;
        
        for (size_t j = i; j < syllables.size(); ++j) {
            bool possible = true;
            TrieNode* temp_curr = curr;
            for (char32_t c : syllables[j]) {
                if (temp_curr->children.find(c) == temp_curr->children.end()) { possible = false; break; }
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
            if (!is_stop) {
                std::u32string word;
                for (size_t k = 0; k < longest_match; ++k) word += syllables[i + k];
                result.push_back(utf32_to_utf8(word));
            }
            i += longest_match;
        } else {
            // OOV Span-based Grouping
            std::u32string oov_span;
            size_t k = i;
            while (k < syllables.size()) {
                char32_t first = syllables[k][0];
                if (first == U' ' || first == U'\n' || first == U'\t' || first == U'\r') break;

                // Stop if a dictionary word starts at this syllable
                TrieNode* test = m_root.get();
                bool word_starts = false;
                for (char32_t c : syllables[k]) {
                    if (test->children.count(c)) { test = test->children[c].get(); if (test->is_end) word_starts = true; }
                    else break;
                }
                if (word_starts && k > i) break;

                oov_span += syllables[k];
                k++;
                if (oov_span.length() > 100) break;
            }
            if (!oov_span.empty()) result.push_back(utf32_to_utf8(oov_span));
            i = (k > i) ? k : i + 1;
        }
    }
    return result;
}

// --- Phase 2: Reverse MaxMatch (v4.7 with OOV Grouping) ---
std::vector<std::string> MyanmarSegmenter::reverseMaxMatch(const std::vector<std::u32string>& syllables) {
    std::vector<std::string> result;
    int i = static_cast<int>(syllables.size()) - 1;
    while (i >= 0) {
        if (syllables[i][0] == U' ' || syllables[i][0] == U'\n' || syllables[i][0] == U'\t' || syllables[i][0] == U'\r') {
            i--; continue;
        }

        size_t longest_match = 0;
        bool is_stop = false;
        
        for (int j = i; j >= 0; --j) {
            std::u32string combined;
            for (int k = j; k <= i; ++k) combined += syllables[k];
            
            TrieNode* curr = m_root.get();
            bool match = true;
            for (char32_t c : combined) {
                if (curr->children.find(c) == curr->children.end()) { match = false; break; }
                curr = curr->children[c].get();
            }
            if (match && curr->is_end) {
                longest_match = i - j + 1;
                is_stop = curr->is_stopword;
            }
        }
        
        if (longest_match > 0) {
            std::u32string word;
            for (size_t k = 0; k < longest_match; ++k) word += syllables[i - longest_match + 1 + k];
            if (!is_stop) result.insert(result.begin(), utf32_to_utf8(word));
            i -= longest_match;
        } else {
            std::u32string oov_span;
            int k = i;
            while (k >= 0) {
                if (syllables[k][0] == U' ' || syllables[k][0] == U'\n' || syllables[k][0] == U'\t' || syllables[k][0] == U'\r') break;
                oov_span.insert(0, syllables[k]);
                k--;
                if (oov_span.length() > 100) break;
            }
            if (!oov_span.empty()) result.insert(result.begin(), utf32_to_utf8(oov_span));
            i = k;
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
