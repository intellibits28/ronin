#include "semantic_router.h"
#include "myanmar_linguistic_normalizer.h"
#include <cmath>
#include <sstream>
#include <algorithm>

namespace Ronin::Kernel::Intent {

SemanticRouter::SemanticRouter() {
    initializeDefaultRoutes();
}

uint32_t SemanticRouter::hashFeature(const std::string& feat) {
    // FNV-1a hash
    uint32_t hash = 2166136261u;
    for (unsigned char c : feat) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

SemanticRouter::SparseVector SemanticRouter::vectorize(const std::string& text) const {
    SparseVector vec;
    auto norm_res = NLP::MyanmarLinguisticNormalizer::normalize(text);
    std::string clean = norm_res.cleaned_text;
    if (clean.empty()) return vec;

    std::unordered_map<uint32_t, float> feat_map;

    // 1. Word unigrams
    std::stringstream ss(clean);
    std::string word;
    while (ss >> word) {
        uint32_t h = hashFeature("w:" + word);
        feat_map[h] += 2.0f; // Give higher weight to whole words
    }

    // 2. Character 3-grams and 4-grams (captures Myanmar morphs and English stems)
    if (clean.length() >= 3) {
        for (size_t i = 0; i + 3 <= clean.length(); ++i) {
            uint32_t h3 = hashFeature("3g:" + clean.substr(i, 3));
            feat_map[h3] += 1.0f;
        }
    }
    if (clean.length() >= 4) {
        for (size_t i = 0; i + 4 <= clean.length(); ++i) {
            uint32_t h4 = hashFeature("4g:" + clean.substr(i, 4));
            feat_map[h4] += 1.0f;
        }
    }

    // Convert to sorted vector and calculate L2 norm
    vec.features.reserve(feat_map.size());
    float sum_sq = 0.0f;
    for (const auto& [id, val] : feat_map) {
        vec.features.push_back({id, val});
        sum_sq += val * val;
    }
    std::sort(vec.features.begin(), vec.features.end(), [](const Feature& a, const Feature& b) {
        return a.id < b.id;
    });
    vec.norm = std::sqrt(sum_sq);

    return vec;
}

float SemanticRouter::cosineSimilarity(const SparseVector& a, const SparseVector& b) {
    if (a.norm <= 1e-6f || b.norm <= 1e-6f) return 0.0f;

    float dot = 0.0f;
    size_t ia = 0, ib = 0;
    const size_t na = a.features.size();
    const size_t nb = b.features.size();

    while (ia < na && ib < nb) {
        if (a.features[ia].id < b.features[ib].id) {
            ia++;
        } else if (b.features[ib].id < a.features[ia].id) {
            ib++;
        } else {
            dot += a.features[ia].weight * b.features[ib].weight;
            ia++;
            ib++;
        }
    }

    return dot / (a.norm * b.norm);
}

void SemanticRouter::addRoute(const RouteDefinition& route) {
    m_routes.push_back(route);
    m_indexed = false;
}

void SemanticRouter::buildIndex() {
    m_indexed_routes.clear();
    m_indexed_routes.reserve(m_routes.size());

    for (const auto& r : m_routes) {
        RouteIndex r_idx;
        r_idx.name = r.name;
        r_idx.threshold = r.threshold;
        r_idx.exemplar_vectors.reserve(r.utterances.size());

        for (const auto& utt : r.utterances) {
            SparseVector u_vec = vectorize(utt);
            if (u_vec.norm > 1e-6f) {
                r_idx.exemplar_vectors.push_back(std::move(u_vec));
            }
        }
        m_indexed_routes.push_back(std::move(r_idx));
    }
    m_indexed = true;
}

RouteMatch SemanticRouter::route(const std::string& query) const {
    RouteMatch best_match;
    if (!m_indexed || m_indexed_routes.empty()) return best_match;

    SparseVector q_vec = vectorize(query);
    if (q_vec.norm <= 1e-6f) return best_match;

    float global_max_sim = -1.0f;
    size_t best_idx = 0;

    for (size_t i = 0; i < m_indexed_routes.size(); ++i) {
        const auto& r_idx = m_indexed_routes[i];
        float max_route_sim = 0.0f;

        for (const auto& ex_vec : r_idx.exemplar_vectors) {
            float sim = cosineSimilarity(q_vec, ex_vec);
            if (sim > max_route_sim) {
                max_route_sim = sim;
            }
        }

        if (max_route_sim > global_max_sim) {
            global_max_sim = max_route_sim;
            best_idx = i;
        }
    }

    if (global_max_sim >= 0.0f) {
        best_match.route_name = m_indexed_routes[best_idx].name;
        best_match.confidence = global_max_sim;
        best_match.is_confident = (global_max_sim >= m_indexed_routes[best_idx].threshold);
    }

    return best_match;
}

void SemanticRouter::initializeDefaultRoutes() {
    m_routes.clear();

    // 1. FLASHLIGHT
    addRoute({
        "FLASHLIGHT",
        {
            "turn on flashlight", "turn off flashlight", "switch on flashlight", "switch off flashlight",
            "flashlight on", "flashlight off", "torch on", "torch off", "light",
            "ဓာတ်မီး ဖွင့်", "ဓာတ်မီး ပိတ်", "မီးဖွင့်", "မီးပိတ်", "မီးထိုး", "မီးရောင်ဖွင့်", "မီးရောင်ပိတ်",
            "မီးပိတ်လိုက်တော့", "ဓာတ်မီး ထိုးပေး", "ဓာတ်မီး ထိုး"
        },
        0.35f
    });

    // 2. PITCH_ANALYSIS (Guitar & Instrument Tuner)
    addRoute({
        "PITCH_ANALYSIS",
        {
            "guitar tuner", "tune my guitar", "run guitar tuner", "instrument tuner",
            "pitch analysis", "note mapper", "frequency tuner", "tune violin", "tune ukulele",
            "ဂစ်တာ အသံညှိ", "ဂစ်တာ ကြိုးညှိ", "ဂစ်တာ tuner", "တူရိယာ အသံညှိ", "ကြိုးညှိ",
            "ဂစ်တာတီးဖို့ ကြိုးညှိ", "အသံညှိမယ်", "ဂစ်တာ အသံညှိပေးပါ", "ဂစ်တာ ကြိုးညှိမယ်"
        },
        0.35f
    });

    // 3. SHM_VIBRATION (Structural Health Monitoring & Resonance)
    addRoute({
        "SHM_VIBRATION",
        {
            "analyze vibration", "check structural health", "resonance frequency", "vibration monitoring",
            "building stability check", "bridge vibration test", "shm diagnostics", "modal frequency",
            "building resonance test", "building resonance",
            "တုန်ခါမှု စစ်ဆေး", "အဆောက်အအုံ ကြံ့ခိုင်မှု", "တုန်ခါမှု တိုင်းတာ", "အဆောက်အအုံ တုန်ခါမှု",
            "ကြံ့ခိုင်မှု စစ်ဆေး", "တုန်ခါမှု အချက်အလက်", "အဆောက်အအုံ ကြံ့ခိုင်မှု တိုင်းတာ", "တုန်ခါမှု စစ်ဆေးပေးပါ"
        },
        0.35f
    });

    // 4. LOCATION & MAP
    addRoute({
        "LOCATION",
        {
            "where am i", "my location", "show my location", "get gps coordinates", "show location", "open map",
            "navigate to", "location on map", "current position",
            "ငါ အခု ဘယ်မှာလဲ", "ငါ ဘယ်ရောက်နေလဲ", "ငါ အခု ဘယ်ရောက်နေလဲ", "တည်နေရာ ပြ", "တည်နေရာ ရှာ", "မြေပုံ ဖွင့်",
            "လက်ရှိ နေရာ", "မြေပုံ ကြည့်", "တည်နေရာ ပြပေးပါ"
        },
        0.32f
    });

    // 5. DEVICE_SETTINGS (Wi-Fi, Bluetooth)
    addRoute({
        "DEVICE_SETTINGS",
        {
            "turn on wifi", "turn off wifi", "toggle wifi", "enable wifi", "disable wifi",
            "turn on bluetooth", "turn off bluetooth", "toggle bluetooth", "bluetooth connect",
            "ဝိုင်ဖိုင် ဖွင့်", "ဝိုင်ဖိုင် ပိတ်", "wifi ဖွင့်", "wifi ပိတ်",
            "ဘလူးတု ဖွင့်", "ဘလူးတု ပိတ်", "bluetooth ဖွင့်", "bluetooth ပိတ်",
            "ဝိုင်ဖိုင် ဖွင့်ပါ", "ဘလူးတု ပိတ်ပေး"
        },
        0.35f
    });

    // 6. FILE_SEARCH
    addRoute({
        "FILE_SEARCH",
        {
            "find pdf files", "search documents", "find file", "locate invoice", "search storage",
            "find images", "search music file", "find python script",
            "ဖိုင် ရှာ", "စာရွက်စာတမ်း ရှာ", "pdf ရှာ", "ဖုန်းထဲက ဖိုင် ရှာ", "စာရွက်စာတမ်းတွေ ရှာ",
            "ဖိုင် ရှာပေးပါ", "စာရွက်စာတမ်း ရှာပါ"
        },
        0.32f
    });

    // 7. SEND_SMS
    addRoute({
        "SEND_SMS",
        {
            "send sms to", "send text message", "sms to contact", "send message to",
            "send sms to brother", "send sms",
            "မက်ဆေ့ခ်ျ ပို့", "sms ပို့", "စာပို့", "ဖုန်းမက်ဆေ့ခ်ျ ပို့",
            "မက်ဆေ့ခ်ျ ပို့ပေးပါ", "ဆီ စာပို့", "ဆီ စာပို့ပါ", "ဆီ sms ပို့"
        },
        0.35f
    });

    // 8. SET_ALARM
    addRoute({
        "SET_ALARM",
        {
            "set alarm at", "wake me up tomorrow", "alarm 7am", "set a reminder alarm", "set alarm at 7am",
            "မနက်ဖြန် နှိုး", "နှိုးစက် ပေး", "မနက် နှိုးစက်", "နှိုးစက် ထား", "အချိန် နှိုး",
            "မနက်ဖြန် 7:00 နှိုး", "မနက်ဖြန် 7 နာရီ နှိုး", "မနက်ဖြန် နှိုးပါ", "နှိုးစက် ပေးပါ"
        },
        0.32f
    });

    // 9. VAULT_MEMORY
    addRoute({
        "VAULT_MEMORY",
        {
            "save to vault", "store secret key", "remember my password", "lookup vault",
            "remember fact", "save note", "recall note", "save password to vault",
            "မှတ်ထား", "သိမ်းထား", "လုံခြုံရေး vault", "လျှို့ဝှက်ချက် သိမ်း", "မှတ်ဉာဏ် ပြန်ရှာ",
            "မှတ်ထားပေးပါ", "လျှို့ဝှက်ချက် သိမ်းထား"
        },
        0.30f
    });

    // 10. CHAT_QUERY
    addRoute({
        "CHAT_QUERY",
        {
            "hello", "hi", "hey ronin", "mingalaba", "how are you", "who are you",
            "explain how to", "tell me about", "what can you do", "help me", "hello ronin",
            "မင်္ဂလာပါ", "ဟယ်လို", "နေကောင်းလား", "မင်းဘယ်သူလဲ", "မင်းဘာလုပ်နိုင်လဲ",
            "ရှင်းပြပါ", "ကူညီပါ", "ဘယ်လိုလုပ်ရမလဲ", "မင်း ဘာတွေလုပ်နိုင်လဲ",
            "ronin က ဘာတွေလုပ်နိုင်လဲ", "ronin ဘာလုပ်နိုင်လဲ", "ဘာတွေလုပ်နိုင်လဲ",
            "စွမ်းဆောင်ရည်တွေ ပြောပြပါ", "ဘာလုပ်ပေးနိုင်လဲ"
        },
        0.28f
    });

    buildIndex();
}

} // namespace Ronin::Kernel::Intent
