#include "myanmar_linguistic_normalizer.h"
#include <algorithm>
#include <sstream>
#include <vector>
#include <cctype>

namespace Ronin::Kernel::NLP {

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n\u200B\u200C\u200D");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n\u200B\u200C\u200D");
    return str.substr(first, (last - first + 1));
}

static void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

std::string MyanmarLinguisticNormalizer::normalizeDigits(const std::string& text) {
    std::string out = text;
    const std::string burmese_digits[] = {"၀", "၁", "၂", "၃", "၄", "၅", "၆", "၇", "၈", "၉"};
    for (int i = 0; i < 10; ++i) {
        replaceAll(out, burmese_digits[i], std::to_string(i));
    }
    return out;
}

std::string MyanmarLinguisticNormalizer::normalizeNumberWords(const std::string& text) {
    std::string out = text;
    // Map word forms to digit forms with surrounding word/phrase boundaries in mind
    const std::pair<std::string, std::string> words[] = {
        {"တစ်ဆယ်", "10"},
        {"ဆယ်", "10"},
        {"တစ်", "1"},
        {"နှစ်", "2"},
        {"သုံး", "3"},
        {"လေး", "4"},
        {"ငါး", "5"},
        {"ခြောက်", "6"},
        {"ခုနစ်", "7"},
        {"ခုန်နစ်", "7"},
        {"ရှစ်", "8"},
        {"ကိုး", "9"}
    };

    for (const auto& [w, num] : words) {
        // Only replace when it precedes time markers or follows quantity markers
        replaceAll(out, w + " နာရီ", num + " နာရီ");
        replaceAll(out, w + "နာရီ", num + " နာရီ");
    }
    return out;
}

std::string MyanmarLinguisticNormalizer::normalizeTimeExpressions(const std::string& text) {
    std::string out = text;
    // Normalize "နာရီခွဲ" -> ":30"
    replaceAll(out, "နာရီခွဲ", ":30");
    replaceAll(out, " နာရီခွဲ", ":30");
    // Normalize "နာရီ" -> ":00" when preceded by a digit
    // Check if preceded by digit
    size_t pos = 0;
    const std::string target = "နာရီ";
    while ((pos = out.find(target, pos)) != std::string::npos) {
        // Find preceding non-space char
        size_t prev = pos;
        while (prev > 0 && out[prev - 1] == ' ') {
            prev--;
        }
        if (prev > 0 && std::isdigit(static_cast<unsigned char>(out[prev - 1]))) {
            out.replace(pos, target.length(), ":00");
            pos += 3;
        } else {
            pos += target.length();
        }
    }
    // Clean up spaces around colon, e.g. "7 :30" -> "7:30", "3 :00" -> "3:00"
    replaceAll(out, " :", ":");
    replaceAll(out, ": ", ":");
    return out;
}

std::string MyanmarLinguisticNormalizer::stripPoliteParticles(const std::string& text, bool& out_is_question) {
    std::string out = trim(text);
    out_is_question = false;

    // Remove sentence punctuation first (။, ၊, ?, .)
    while (!out.empty() && (out.back() == '?' || out.back() == '.' || out.back() == '!' || out.back() == ',')) {
        if (out.back() == '?') out_is_question = true;
        out.pop_back();
        out = trim(out);
    }
    // Check Burmese punctuation (။ is 3 bytes in UTF-8: E1 81 84, ၊ is E1 81 85)
    auto removeEndIfMatch = [](std::string& s, const std::string& suffix) -> bool {
        if (s.length() >= suffix.length() && s.rfind(suffix) == (s.length() - suffix.length())) {
            s.erase(s.length() - suffix.length());
            return true;
        }
        return false;
    };

    removeEndIfMatch(out, "။");
    removeEndIfMatch(out, "၊");
    out = trim(out);

    // Question particles
    const std::vector<std::string> question_particles = {
        "ပါသလား", "ပါသလဲ", "သလား", "သလဲ", "လို့ရမလား", "ရမလား", "လား", "လဲ"
    };

    for (const auto& qp : question_particles) {
        if (removeEndIfMatch(out, qp)) {
            out_is_question = true;
            out = trim(out);
            break;
        }
    }

    // Polite and request suffixes ordered by length (longest match first)
    const std::vector<std::string> polite_suffixes = {
        "ပေးပါရစေဦး", "ပေးပါရစေ", "ပါရစေဦး", "ပါရစေ", "ပေးပါဦး", "ပေးပါဗျာ",
        "ပေးပါခင်ဗျာ", "ပေးပါရှင်", "ပေးပါတော့", "ပေးပါ", "လိုက်တော့", "လိုက်ပါ", "လိုက်",
        "ချင်တယ်ဗျာ", "ချင်တယ်ခင်ဗျာ", "ချင်တယ်ရှင်", "ချင်ပါသည်", "ချင်တယ်", "ချင်လို့",
        "ပါဦး", "ပါရစေ", "ပါတော့", "ကြစို့", "ရအောင်", "ကြရအောင်", "ပါခင်ဗျာ", "ပါဗျာ",
        "ပါရှင်", "ပါသည်", "မယ်", "မယ်နော်", "နော်", "ပါ", "ပေး"
    };

    bool stripped = true;
    while (stripped) {
        stripped = false;
        out = trim(out);
        for (const auto& suffix : polite_suffixes) {
            if (removeEndIfMatch(out, suffix)) {
                stripped = true;
                out = trim(out);
                break;
            }
        }
    }

    return out;
}

MyanmarLinguisticNormalizer::NormalizedResult MyanmarLinguisticNormalizer::normalize(const std::string& input) {
    NormalizedResult res;
    if (input.empty()) return res;

    std::string text = input;

    // 1. Strip zero-width characters (U+200B, U+200C, U+200D)
    replaceAll(text, "\xE2\x80\x8B", "");
    replaceAll(text, "\xE2\x80\x8C", "");
    replaceAll(text, "\xE2\x80\x8D", "");

    // 2. Convert to lowercase for English portions
    for (char& c : text) {
        if (static_cast<unsigned char>(c) < 128) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    // 3. Normalize digits and written numbers
    text = normalizeDigits(text);
    text = normalizeNumberWords(text);

    // 4. Normalize time expressions
    text = normalizeTimeExpressions(text);

    res.raw_normalized_text = trim(text);

    // 5. Strip polite and question particles for clean intent classification
    res.cleaned_text = stripPoliteParticles(res.raw_normalized_text, res.is_question);

    return res;
}

} // namespace Ronin::Kernel::NLP
