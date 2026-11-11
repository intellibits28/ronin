#include "intent_slot_extractors.h"
#include "myanmar_linguistic_normalizer.h"
#include <algorithm>
#include <regex>
#include <sstream>

namespace Ronin::Kernel::Intent {

std::string IntentSlotExtractors::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n'\",:;");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n'\",:;");
    return str.substr(first, (last - first + 1));
}

SmsSlotData IntentSlotExtractors::extractSmsSlots(const std::string& input) {
    SmsSlotData slots;
    std::string norm = NLP::MyanmarLinguisticNormalizer::normalizeDigits(input);
    std::transform(norm.begin(), norm.end(), norm.begin(), ::tolower);

    slots.attach_location = (norm.find("location") != std::string::npos ||
                             norm.find("တည်နေရာ") != std::string::npos ||
                             norm.find("နေရာ") != std::string::npos);

    // 1. Check for phone number pattern (e.g. 09..., +959..., 09-...)
    std::regex phone_re(R"((?:\+?959|09)[\d\-]{7,11})");
    std::smatch phone_match;
    if (std::regex_search(norm, phone_match, phone_re)) {
        slots.recipient = phone_match.str();
    }

    // 2. Extract recipient by Myanmar grammar: "<name> ဆီ" or English: "to <name>"
    if (slots.recipient.empty()) {
        size_t to_pos = norm.find("to ");
        if (to_pos != std::string::npos) {
            size_t start_p = to_pos + 3;
            size_t end_p = norm.find(" via", start_p);
            if (end_p == std::string::npos) end_p = norm.find(" with", start_p);
            if (end_p == std::string::npos) end_p = norm.find(" message", start_p);
            if (end_p == std::string::npos) end_p = norm.find(" sms", start_p);
            if (end_p == std::string::npos) end_p = norm.find(" ", start_p + 20);

            if (end_p != std::string::npos && end_p > start_p) {
                slots.recipient = trim(norm.substr(start_p, end_p - start_p));
            } else {
                slots.recipient = trim(norm.substr(start_p));
            }
        } else {
            size_t si_pos = norm.find("ဆီ");
            if (si_pos != std::string::npos && si_pos > 0) {
                size_t search_end = si_pos;
                while (search_end > 0 && norm[search_end - 1] == ' ') search_end--;
                size_t start_p = norm.rfind(" ", search_end > 0 ? search_end - 1 : 0);
                if (start_p != std::string::npos && start_p < search_end) {
                    slots.recipient = trim(norm.substr(start_p + 1, search_end - (start_p + 1)));
                } else {
                    slots.recipient = trim(norm.substr(0, search_end));
                }
            }
        }
    }

    // Remove connector words from recipient
    if (!slots.recipient.empty()) {
        size_t p_pos = slots.recipient.rfind("ပြီး ");
        if (p_pos != std::string::npos) slots.recipient = trim(slots.recipient.substr(p_pos + 6));
        p_pos = slots.recipient.rfind("ပြီး");
        if (p_pos != std::string::npos) slots.recipient = trim(slots.recipient.substr(p_pos + 4));
        slots.is_valid = true;
    }

    // 3. Extract message content: between quotes or before "လို့"
    size_t q1 = input.find("\"");
    size_t q2 = (q1 != std::string::npos) ? input.find("\"", q1 + 1) : std::string::npos;
    if (q1 != std::string::npos && q2 != std::string::npos) {
        slots.message = input.substr(q1 + 1, q2 - q1 - 1);
    } else {
        size_t loh_pos = input.find("လို့");
        if (loh_pos != std::string::npos) {
            // Text before လို့
            size_t start = 0;
            if (!slots.recipient.empty()) {
                size_t rec_p = input.find(slots.recipient);
                if (rec_p != std::string::npos) start = rec_p + slots.recipient.length();
            }
            if (loh_pos > start) {
                slots.message = trim(input.substr(start, loh_pos - start));
            }
        }
    }

    return slots;
}

AlarmSlotData IntentSlotExtractors::extractAlarmSlots(const std::string& input) {
    AlarmSlotData slots;
    std::string norm = NLP::MyanmarLinguisticNormalizer::normalize(input).raw_normalized_text;

    // Date recognition
    if (norm.find("မနက်ဖြန်") != std::string::npos || norm.find("tomorrow") != std::string::npos) {
        slots.date_str = "tomorrow";
    } else {
        slots.date_str = "today";
    }

    // Time pattern matching: e.g. 7:00, 07:30, 7:30 am, 19:00
    std::regex time_re(R"((\d{1,2})[:.](\d{2}))");
    std::smatch time_match;
    if (std::regex_search(norm, time_match, time_re)) {
        int hour = std::stoi(time_match[1].str());
        int minute = std::stoi(time_match[2].str());

        // Check AM/PM context in Myanmar (မနက် = AM, ညနေ/ည = PM)
        bool is_pm = (norm.find("ညနေ") != std::string::npos || norm.find("ည") != std::string::npos || norm.find("pm") != std::string::npos);
        if (is_pm && hour < 12) hour += 12;

        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
        slots.time_str = buf;
        slots.is_valid = true;
    }

    return slots;
}

std::string IntentSlotExtractors::cleanVaultTitle(const std::string& raw) {
    std::string t = raw;
    std::vector<std::string> verbs = {
        "update", "save", "store", "add", "change", "replace", "modify", "set",
        "lookup", "find", "show", "get", "view", "what", "is", "tell", "check",
        "my", "the", "a", "an", "in", "into", "from", "for", "vault", "secret", "entry", "credential",
        "ကျွန်တော့်", "ကျွန်မ", "ငါ့", "ရဲ့", "ကို", "ထဲ", "ထဲက", "ထဲသို့", "ထဲမှာ",
        "အသစ်", "အတွက်", "အချက်အလက်", "ရှာပေးပါ", "ရှာပေး", "ရှာဖွေ", "ရှာပါ", "ရှာ",
        "ပြပေးပါ", "ပြပါ", "ပြ", "တောင်းပါ", "လုံခြုံရေး", "မှတ်ထား", "သိမ်းပါ", "သိမ်း"
    };
    for (const auto& w : verbs) {
        size_t p = 0;
        std::string wl = w;
        std::transform(wl.begin(), wl.end(), wl.begin(), ::tolower);
        std::string tl = t;
        std::transform(tl.begin(), tl.end(), tl.begin(), ::tolower);
        while ((p = tl.find(wl, p)) != std::string::npos) {
            bool before_ok = (p == 0 || !std::isalnum(static_cast<unsigned char>(tl[p - 1])));
            bool after_ok = (p + wl.length() == tl.length() || !std::isalnum(static_cast<unsigned char>(tl[p + wl.length()])));
            if (before_ok && after_ok) {
                t.replace(p, wl.length(), " ");
                tl = t;
                std::transform(tl.begin(), tl.end(), tl.begin(), ::tolower);
                p += 1;
            } else {
                p += 1;
            }
        }
    }
    std::string res = trim(t);
    for (char& c : res) {
        if (static_cast<unsigned char>(c) < 128) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return res;
}

VaultSlotData IntentSlotExtractors::extractVaultSlots(const std::string& input) {
    VaultSlotData slots;
    std::string input_lower = input;
    for (char& c : input_lower) {
        if (static_cast<unsigned char>(c) < 128) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    std::string norm = NLP::MyanmarLinguisticNormalizer::normalize(input).cleaned_text;

    slots.is_save = (
        input_lower.find("save") != std::string::npos || input_lower.find("store") != std::string::npos ||
        input_lower.find("add") != std::string::npos || input_lower.find("update") != std::string::npos ||
        input_lower.find("change") != std::string::npos || input_lower.find("replace") != std::string::npos ||
        input_lower.find("modify") != std::string::npos || input_lower.find("set") != std::string::npos ||
        input_lower.find("သိမ်း") != std::string::npos || input_lower.find("မှတ်") != std::string::npos ||
        input_lower.find("ပြင်") != std::string::npos || input_lower.find("ပြောင်း") != std::string::npos ||
        input_lower.find("လဲ") != std::string::npos ||
        norm.find("သိမ်း") != std::string::npos || norm.find("မှတ်") != std::string::npos ||
        norm.find("ပြင်") != std::string::npos || norm.find("ပြောင်း") != std::string::npos
    );

    // 1. Check if input has quoted content
    size_t q1 = input.find("\"");
    if (q1 == std::string::npos) q1 = input.find("'");
    size_t q2 = (q1 != std::string::npos) ? (input[q1] == '"' ? input.find("\"", q1 + 1) : input.find("'", q1 + 1)) : std::string::npos;
    if (q1 != std::string::npos && q2 != std::string::npos) {
        slots.content = input.substr(q1 + 1, q2 - q1 - 1);
        std::string left = input.substr(0, q1) + " " + input.substr(q2 + 1);
        slots.title = cleanVaultTitle(left);
        slots.is_valid = !slots.title.empty();
        return slots;
    }

    // 2. Burmese grammatical pattern: <Title> ကို <Value> အဖြစ်/လို့/သို့ ပြောင်းပါ/ပြင်ပါ/သိမ်းပါ
    if (slots.is_save) {
        const std::string ko = " ကို ";
        size_t p_ko = input_lower.find(ko);
        if (p_ko != std::string::npos) {
            const std::vector<std::string> burmese_as = {" အဖြစ် ", " လို့ ", " သို့ ", " အနေနဲ့ ", " အဖြစ်", " လို့", " သို့"};
            for (const auto& as_word : burmese_as) {
                size_t p_as = input_lower.find(as_word, p_ko + ko.length());
                if (p_as != std::string::npos) {
                    slots.title = cleanVaultTitle(input.substr(0, p_ko));
                    slots.content = trim(input.substr(p_ko + ko.length(), p_as - (p_ko + ko.length())));
                    slots.is_valid = !slots.title.empty();
                    return slots;
                }
            }
        }
    }

    // 3. English key-value separators: <Title> to/as/with/:/= <Value>
    if (slots.is_save) {
        const std::vector<std::pair<std::string, size_t>> seps = {
            {" to ", 4}, {" as ", 4}, {" with ", 6}, {" = ", 3}, {": ", 2},
            {" အဖြစ် ", std::string(" အဖြစ် ").length()},
            {" လို့ ", std::string(" လို့ ").length()},
            {" သို့ ", std::string(" သို့ ").length()}
        };
        for (const auto& [sep, len] : seps) {
            size_t p = input_lower.find(sep);
            if (p != std::string::npos) {
                std::string left = input.substr(0, p);
                std::string right = input.substr(p + len);
                
                slots.title = cleanVaultTitle(left);
                
                std::vector<std::string> right_suffixes = {
                    " in vault", " in the vault", " please",
                    "ပြောင်းပေးပါ", "ပြောင်းပါ", "ပြင်ပေးပါ", "ပြင်ပါ",
                    "သိမ်းပေးပါ", "သိမ်းပါ", "မှတ်ပေးပါ", "မှတ်ပါ",
                    "update လုပ်ပေးပါ", "update လုပ်ပါ", "ပါ", "နော်"
                };
                for (const auto& suf : right_suffixes) {
                    size_t sp = right.rfind(suf);
                    if (sp != std::string::npos && sp + suf.length() == right.length()) {
                        right = right.substr(0, sp);
                    }
                }
                slots.content = trim(right);
                slots.is_valid = !slots.title.empty();
                return slots;
            }
        }
    }

    // 4. Fallback title extraction (lookup or save without separator)
    slots.title = cleanVaultTitle(input_lower);
    slots.is_valid = !slots.title.empty();
    return slots;
}

FileSearchSlotData IntentSlotExtractors::extractFileSearchSlots(const std::string& input) {
    FileSearchSlotData slots;
    std::string norm = NLP::MyanmarLinguisticNormalizer::normalize(input).cleaned_text;

    const std::vector<std::string> exts = {"pdf", "docx", "doc", "txt", "png", "jpg", "mp3", "mp4", "apk", "zip"};
    for (const auto& ext : exts) {
        if (norm.find(ext) != std::string::npos) {
            slots.file_extension = ext;
            break;
        }
    }

    std::vector<std::string> to_remove = {
        "file", "files", "find", "search", "lookup", "locate", "document", "documents",
        "ဖိုင်", "ရှာ", "ရှာဖွေ", "စာရွက်စာတမ်း"
    };

    std::string target = norm;
    for (const auto& w : to_remove) {
        size_t p = 0;
        while ((p = target.find(w, p)) != std::string::npos) {
            target.replace(p, w.length(), " ");
            p += 1;
        }
    }

    slots.query = trim(target);
    if (slots.query.empty() && !slots.file_extension.empty()) {
        slots.query = slots.file_extension;
    }
    slots.is_valid = !slots.query.empty();
    return slots;
}

LocationSlotData IntentSlotExtractors::extractLocationSlots(const std::string& input) {
    LocationSlotData slots;
    std::string norm = NLP::MyanmarLinguisticNormalizer::normalize(input).cleaned_text;

    slots.is_map_open = (norm.find("map") != std::string::npos || norm.find("မြေပုံ") != std::string::npos ||
                         norm.find("navigate") != std::string::npos || norm.find("open") != std::string::npos);

    slots.is_saving = (norm.find("save") != std::string::npos || norm.find("remember") != std::string::npos ||
                       norm.find("မှတ်") != std::string::npos || norm.find("သိမ်း") != std::string::npos);

    if (norm.find("home") != std::string::npos || norm.find("အိမ်") != std::string::npos) {
        slots.entity = "Home";
    } else if (norm.find("work") != std::string::npos || norm.find("ရုံး") != std::string::npos) {
        slots.entity = "Work";
    } else {
        slots.entity = "Location";
    }

    return slots;
}

} // namespace Ronin::Kernel::Intent
