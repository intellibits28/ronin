#pragma once

#include <string>
#include <vector>

namespace Ronin::Kernel::NLP {

/**
 * Normalizes Myanmar and bilingual text for intent classification and semantic routing.
 * Performs digit normalization, particle/suffix stripping, time conversion, and whitespace cleanup.
 */
class MyanmarLinguisticNormalizer {
public:
    struct NormalizedResult {
        std::string cleaned_text;        // Fully normalized and particle-stripped text
        std::string raw_normalized_text; // Normalized (digits, whitespace) but preserving particles
        std::vector<std::string> stripped_particles;
        bool is_question = false;
    };

    /**
     * Normalizes input text:
     * 1. Converts Burmese numerals (၀-၉) and number words (တစ်, နှစ်...) to Arabic digits (0-9).
     * 2. Normalizes time expressions (e.g. ၇ နာရီခွဲ -> 7:30, ၇ နာရီ -> 7:00).
     * 3. Strips zero-width characters and normalizes punctuation.
     * 4. Strips polite/request suffixes and question particles from clause endings.
     */
    static NormalizedResult normalize(const std::string& input);

    /**
     * Converts Burmese digits (၀-၉) to ASCII digits (0-9).
     */
    static std::string normalizeDigits(const std::string& text);

    /**
     * Normalizes written Burmese number words into digits.
     */
    static std::string normalizeNumberWords(const std::string& text);

    /**
     * Normalizes colloquial Myanmar time phrases to standard time notation.
     */
    static std::string normalizeTimeExpressions(const std::string& text);

    /**
     * Strips common polite, request, and question particles from sentence/clause endings.
     */
    static std::string stripPoliteParticles(const std::string& text, bool& out_is_question);
};

} // namespace Ronin::Kernel::NLP
