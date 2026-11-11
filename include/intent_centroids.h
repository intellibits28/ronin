#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace Ronin::Kernel::Intent {

enum class LanguageRegister {
    EN = 0,
    MM = 1,
    MIX = 2
};

inline std::string languageRegisterToString(LanguageRegister reg) {
    switch (reg) {
        case LanguageRegister::EN: return "EN";
        case LanguageRegister::MM: return "MM";
        case LanguageRegister::MIX: return "MIX";
    }
    return "UNKNOWN";
}

struct IntentSubCluster {
    std::string intent_name;      // e.g. "VAULT_LOOKUP", "VAULT_UPDATE", "VAULT_SAVE"
    LanguageRegister reg;         // EN, MM, MIX
    std::string register_suffix;  // "_EN", "_MM", "_MIX"
    std::vector<std::string> exemplars;
};

/**
 * Returns the curated 3-register intent exemplar registry (Refinement 2).
 */
inline std::vector<IntentSubCluster> getCuratedIntentClusters() {
    return {
        // --- 1. VAULT_LOOKUP ---
        {"VAULT_LOOKUP", LanguageRegister::EN, "_EN", {
            "show my api key", "get my secret key", "lookup vault password", "what is my api key",
            "show my gemini api key", "view vault secret", "retrieve my password", "find my api token",
            "show my secret", "what is my vault password", "get gemini api key"
        }},
        {"VAULT_LOOKUP", LanguageRegister::MM, "_MM", {
            "လျှို့ဝှက်ကုဒ် ပြပါ", "စကားဝှက် ရှာပေးပါ", "လျှို့ဝှက်ချက် ပြပါ", "စကားဝှက် ဘာလဲ",
            "လုံခြုံရေး ကုဒ် ပြပေးပါ", "လျှို့ဝှက်ချက် ဘာလဲ", "လျှို့ဝှက်ချက် ပြပေးပါ"
        }},
        {"VAULT_LOOKUP", LanguageRegister::MIX, "_MIX", {
            "gemini api key ပြပါ", "my api key ပြပေးပါ", "vault ထဲက key ပြပါ", "gemini api key ဘာလဲ",
            "openai key ရှာပေးပါ", "gemini key ပြပါ", "api key ပြပေးပါ"
        }},

        // --- 2. VAULT_UPDATE ---
        {"VAULT_UPDATE", LanguageRegister::EN, "_EN", {
            "update my gemini api key to", "change password to", "replace secret key with",
            "modify api key", "update vault entry to", "reset my secret token to", "change my api key",
            "update gemini api key", "replace api key"
        }},
        {"VAULT_UPDATE", LanguageRegister::MM, "_MM", {
            "လျှို့ဝှက်ကုဒ် အသစ် ပြောင်းပေးပါ", "စကားဝှက် ပြင်ပါ", "လျှို့ဝှက်ချက် အသစ် လဲပါ",
            "စကားဝှက် အသစ် ပြောင်းပါ", "လျှို့ဝှက်ကုဒ် ပြင်ဆင်ပါ", "စကားဝှက် ပြင်ပေးပါ"
        }},
        {"VAULT_UPDATE", LanguageRegister::MIX, "_MIX", {
            "gemini api key ကို အဖြစ် ပြောင်းပေးပါ", "api key အသစ် update လုပ်ပေးပါ", "vault ထဲက key ပြင်ပါ",
            "gemini key အသစ် လဲပေးပါ", "gemini api key update လုပ်မယ်", "api key ပြင်ပေးပါ"
        }},

        // --- 3. VAULT_SAVE ---
        {"VAULT_SAVE", LanguageRegister::EN, "_EN", {
            "save to vault", "store secret key in vault", "save password as", "remember secret in vault",
            "store credentials in vault", "save api key as", "store in vault"
        }},
        {"VAULT_SAVE", LanguageRegister::MM, "_MM", {
            "လုံခြုံရေး vault ထဲ သိမ်းပါ", "လျှို့ဝှက်ချက် အဖြစ် သိမ်းထားပါ", "စကားဝှက် သိမ်းပေးပါ",
            "လျှို့ဝှက်ကုဒ် မှတ်ထားပါ", "vault ထဲ သိမ်းပါ", "စကားဝှက် သိမ်းပါ"
        }},
        {"VAULT_SAVE", LanguageRegister::MIX, "_MIX", {
            "gemini api key ကို vault ထဲ သိမ်းပါ", "api key အသစ် သိမ်းထားပါ", "password ကို vault ထဲ မှတ်ထားပါ",
            "token ကို vault ထဲ ထည့်ပါ", "ကားနံပါတ် vault ထဲသိမ်းပါ", "secret ကို vault ထဲ သိမ်းပါ"
        }},

        // --- 4. LOCATION ---
        {"LOCATION", LanguageRegister::EN, "_EN", {
            "where am i", "my location", "show my location", "get gps coordinates", "show location",
            "open map", "current position", "navigate to current position", "where is my position"
        }},
        {"LOCATION", LanguageRegister::MM, "_MM", {
            "ငါ အခု ဘယ်မှာလဲ", "ငါ ဘယ်ရောက်နေလဲ", "လက်ရှိ တည်နေရာ ပြပါ", "မြေပုံ ဖွင့်ပါ",
            "လက်ရှိ နေရာ ဘယ်မှာလဲ", "မြေပုံ ကြည့်မယ်", "တည်နေရာ ပြပေးပါ"
        }},
        {"LOCATION", LanguageRegister::MIX, "_MIX", {
            "current location ပြပါ", "gps coordinates တောင်းပါ", "map ဖွင့်ပေးပါ", "my location ပြပါ",
            "current position ဘယ်မှာလဲ", "location ကြည့်မယ်"
        }},

        // --- 5. DEVICE_SETTINGS ---
        {"DEVICE_SETTINGS", LanguageRegister::EN, "_EN", {
            "turn on wifi", "turn off wifi", "toggle wifi", "enable wifi", "disable wifi",
            "turn on bluetooth", "turn off bluetooth", "toggle bluetooth"
        }},
        {"DEVICE_SETTINGS", LanguageRegister::MM, "_MM", {
            "ဝိုင်ဖိုင် ဖွင့်", "ဝိုင်ဖိုင် ပိတ်", "ဘလူးတု ဖွင့်", "ဘလူးတု ပိတ်",
            "ဝိုင်ဖိုင် ဖွင့်ပေးပါ", "ဘလူးတု ပိတ်လိုက်တော့"
        }},
        {"DEVICE_SETTINGS", LanguageRegister::MIX, "_MIX", {
            "wifi ဖွင့်ပါ", "wifi ပိတ်ပါ", "bluetooth ဖွင့်ပေး", "bluetooth ပိတ်ပေး", "wifi toggle လုပ်ပါ"
        }},

        // --- 6. FILE_SEARCH ---
        {"FILE_SEARCH", LanguageRegister::EN, "_EN", {
            "find pdf files", "search documents", "find file", "locate invoice", "search storage",
            "find python script", "find images"
        }},
        {"FILE_SEARCH", LanguageRegister::MM, "_MM", {
            "ဖိုင် ရှာ", "စာရွက်စာတမ်း ရှာ", "ဖုန်းထဲက ဖိုင် ရှာ", "စာရွက်စာတမ်းတွေ ရှာပေးပါ", "ဖိုင် ရှာပါ"
        }},
        {"FILE_SEARCH", LanguageRegister::MIX, "_MIX", {
            "pdf ဖိုင် ရှာပေးပါ", "invoice file ရှာပါ", "document တွေ ရှာပေး", "txt ဖိုင် ရှာပါ"
        }},

        // --- 7. SEND_SMS ---
        {"SEND_SMS", LanguageRegister::EN, "_EN", {
            "send sms to", "send text message", "sms to contact", "send message to brother", "text message to"
        }},
        {"SEND_SMS", LanguageRegister::MM, "_MM", {
            "မက်ဆေ့ခ်ျ ပို့", "စာပို့", "ဖုန်းမက်ဆေ့ခ်ျ ပို့", "ဆီ စာပို့ပါ", "မက်ဆေ့ခ်ျ ပို့ပေးပါ"
        }},
        {"SEND_SMS", LanguageRegister::MIX, "_MIX", {
            "sms ပို့ပါ", "ဆီ sms ပို့ပေးပါ", "ဆီ message ပို့ပါ", "sms ပို့"
        }},

        // --- 8. SET_ALARM ---
        {"SET_ALARM", LanguageRegister::EN, "_EN", {
            "set alarm at", "wake me up tomorrow", "alarm at 7am", "set a reminder alarm", "alarm for tomorrow morning"
        }},
        {"SET_ALARM", LanguageRegister::MM, "_MM", {
            "မနက်ဖြန် နှိုး", "နှိုးစက် ပေး", "မနက် နှိုးစက် ထား", "အချိန် နှိုးပေးပါ", "မနက်ဖြန် 7 နာရီ နှိုး"
        }},
        {"SET_ALARM", LanguageRegister::MIX, "_MIX", {
            "alarm 7am ထားပါ", "မနက်ဖြန် alarm ပေးပါ", "မနက်ဖြန် 7:00 နှိုး", "alarm ပေးပါ"
        }},

        // --- 9. SHM_VIBRATION ---
        {"SHM_VIBRATION", LanguageRegister::EN, "_EN", {
            "analyze vibration", "check structural health", "resonance frequency", "vibration monitoring",
            "building stability check", "modal frequency test"
        }},
        {"SHM_VIBRATION", LanguageRegister::MM, "_MM", {
            "တုန်ခါမှု စစ်ဆေး", "အဆောက်အအုံ ကြံ့ခိုင်မှု", "တုန်ခါမှု တိုင်းတာ", "ကြံ့ခိုင်မှု စစ်ဆေးပါ"
        }},
        {"SHM_VIBRATION", LanguageRegister::MIX, "_MIX", {
            "shm diagnostics လုပ်ပါ", "building resonance စစ်ပါ", "vibration analyze လုပ်ပေးပါ", "တုန်ခါမှု စစ်ဆေးပေးပါ"
        }},

        // --- 10. PITCH_ANALYSIS ---
        {"PITCH_ANALYSIS", LanguageRegister::EN, "_EN", {
            "guitar tuner", "tune my guitar", "run guitar tuner", "pitch analysis", "note mapper",
            "instrument tuner", "tune violin", "tune ukulele"
        }},
        {"PITCH_ANALYSIS", LanguageRegister::MM, "_MM", {
            "ဂစ်တာ အသံညှိ", "ဂစ်တာ ကြိုးညှိ", "တူရိယာ အသံညှိ", "ကြိုးညှိ", "အသံညှိမယ်"
        }},
        {"PITCH_ANALYSIS", LanguageRegister::MIX, "_MIX", {
            "guitar tuner ဖွင့်ပါ", "ဂစ်တာ tuner", "guitar ကြိုးညှိပေးပါ", "guitar tuning လုပ်မယ်"
        }},

        // --- 11. CHAT_QUERY ---
        {"CHAT_QUERY", LanguageRegister::EN, "_EN", {
            "hello", "hi", "how are you", "who are you", "explain how to", "tell me about",
            "what can you do", "help me"
        }},
        {"CHAT_QUERY", LanguageRegister::MM, "_MM", {
            "မင်္ဂလာပါ", "နေကောင်းလား", "မင်းဘယ်သူလဲ", "မင်းဘာလုပ်နိုင်လဲ", "ဘာတွေလုပ်နိုင်လဲ", "ရှင်းပြပါ", "ကူညီပါ"
        }},
        {"CHAT_QUERY", LanguageRegister::MIX, "_MIX", {
            "hi ronin", "hello ronin", "ronin က ဘာတွေလုပ်နိုင်လဲ", "ဘာလုပ်ပေးနိုင်လဲ"
        }},

        // --- 12. FLASHLIGHT ---
        {"FLASHLIGHT", LanguageRegister::EN, "_EN", {
            "turn on flashlight", "turn off flashlight", "switch on flashlight", "switch off flashlight",
            "flashlight on", "flashlight off", "torch on", "torch off", "light"
        }},
        {"FLASHLIGHT", LanguageRegister::MM, "_MM", {
            "ဓာတ်မီး ဖွင့်", "ဓာတ်မီး ပိတ်", "မီးဖွင့်", "မီးပိတ်", "မီးထိုး", "မီးရောင်ဖွင့်", "မီးရောင်ပိတ်",
            "မီးပိတ်လိုက်တော့", "ဓာတ်မီး ထိုးပေး", "ဓာတ်မီး ထိုး"
        }},
        {"FLASHLIGHT", LanguageRegister::MIX, "_MIX", {
            "flashlight ဖွင့်", "flashlight ပိတ်", "torch ဖွင့်ပေး", "torch ပိတ်", "flashlight on ပေးပါ"
        }}
    };
}

/**
 * Normalizes a dense float vector strictly to unit L2 length (||v||_2 = 1.0).
 */
inline void l2Normalize(std::vector<float>& vec) {
    float sum_sq = 0.0f;
    for (float v : vec) sum_sq += v * v;
    if (sum_sq > 1e-12f) {
        float inv_norm = 1.0f / std::sqrt(sum_sq);
        for (float& v : vec) v *= inv_norm;
    }
}

/**
 * Computes exact cosine similarity between two strictly unit-normalized vectors.
 */
inline float dotProductCosine(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    float dot = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
    }
    return dot;
}

} // namespace Ronin::Kernel::Intent
