#include "intent_engine.h"
#include <fstream>
#include <sstream>
#include <vector>

#ifdef __aarch64__
#include <arm_neon.h>
#include <sys/auxv.h>
#include <asm/hwcap.h>

// Fallback for older NDK headers
#ifndef HWCAP_ASIMDDP
#define HWCAP_ASIMDDP (1 << 20)
#endif
#endif

#include <iostream>
#include <cmath>
#include <algorithm>
#include "ronin_log.h"

#define TAG "RoninIntentEngine"

namespace Ronin::Kernel::Intent {

// Initialize to NORMAL by default
ThermalState g_thermal_state = ThermalState::NORMAL;

// Helper to strip non-alphanumeric chars for tokenizer
static std::string strip_punctuation(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::isalnum(c) || std::isspace(c)) out += c;
    }
    return out;
}

// Helper to trim leading/trailing whitespace
static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

void IntentEngine::loadCapabilities(const std::string& json_path) {
    std::ifstream file(json_path);
    if (!file.is_open()) {
        LOGE(TAG, "Failed to open capability manifest: %s", json_path.c_str());
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    LOGI(TAG, "Loading dynamic manifest: v3.9.5-STABLE");

    // Minimalist string-based "JSON" parser for our specific format
    size_t pos = 0;
    while ((pos = content.find("{\"id\":", pos)) != std::string::npos) {
        Ronin::Kernel::CapabilityEntry cap;
        
        // Extract ID
        size_t id_start = content.find(":", pos) + 1;
        cap.id = std::stoul(content.substr(id_start));

        // Extract Name
        size_t name_start = content.find("\"name\": \"", pos) + 9;
        size_t name_end = content.find("\"", name_start);
        cap.name = content.substr(name_start, name_end - name_start);

        // Extract Subjects (naive array parsing)
        size_t sub_start = content.find("\"subjects\": [", pos) + 12;
        size_t sub_end = content.find("]", sub_start);
        std::string subs = content.substr(sub_start, sub_end - sub_start);
        std::stringstream ss_sub(subs);
        std::string s;
        while (std::getline(ss_sub, s, ',')) {
            size_t s_start = s.find("\"") + 1;
            size_t s_end = s.find("\"", s_start);
            if (s_start != std::string::npos && s_end != std::string::npos) {
                cap.subjects.push_back(trim(s.substr(s_start, s_end - s_start)));
            }
        }

        // Extract Actions
        size_t act_start = content.find("\"actions\": [", pos) + 11;
        size_t act_end = content.find("]", act_start);
        std::string acts = content.substr(act_start, act_end - act_start);
        std::stringstream ss_act(acts);
        while (std::getline(ss_act, s, ',')) {
            size_t a_start = s.find("\"") + 1;
            size_t a_end = s.find("\"", a_start);
            if (a_start != std::string::npos && a_end != std::string::npos) {
                cap.actions.push_back(trim(s.substr(a_start, a_end - a_start)));
            }
        }

        // Extract Threshold
        size_t conf_start = content.find("\"confidence_threshold\":", pos) + 23;
        cap.confidence_threshold = std::stof(content.substr(conf_start));

        m_capabilities.push_back(cap);
        pos = act_end;
    }
    LOGI(TAG, "Dynamic manifest loaded: %zu capabilities registered.", m_capabilities.size());
}

std::vector<std::string> IntentEngine::tokenize(const std::string& input) {
    std::string clean = strip_punctuation(input);
    std::transform(clean.begin(), clean.end(), clean.begin(), ::tolower);
    
    std::vector<std::string> tokens;
    std::stringstream ss(clean);
    std::string token;
    while (ss >> token) tokens.push_back(token);
    return tokens;
}

bool IntentEngine::isFuzzyMatch(const std::string& word, const std::string& target) {
    if (word == target) return true;
    if (std::abs((int)word.length() - (int)target.length()) > 1) return false;
    
    int diff = 0;
    size_t len = std::min(word.length(), target.length());
    for (size_t i = 0; i < len; ++i) {
        if (word[i] != target[i]) diff++;
    }
    diff += std::abs((int)word.length() - (int)target.length());
    return diff <= 1;
}

CognitiveIntent IntentEngine::process(const std::string& input, const std::string& context_subject) {
    auto tokens = tokenize(input);
    if (tokens.empty()) return {1, 0.0f, true};

    // Tier 1: Greetings Guardrail (Exact Match)
    std::string first = tokens[0];
    if (first == "hi" || first == "hello" || first == "hey" || first == "mingalaba") {
        LOGI(TAG, "> Tier 1 Match: Greeting detected.");
        return {1, 1.0f, true}; // ChatNode (ID 1)
    }

    // Safety-First Negation Logic (v3.9.4)
    bool isOff = (input.find("off") != std::string::npos || 
                  input.find("stop") != std::string::npos || 
                  input.find("disable") != std::string::npos);

    // Tier 2: Dynamic Matcher (Subject + Action)
    for (const auto& cap : m_capabilities) {
        // ... (existing matcher logic)
        bool subject_found = false;
        bool action_found = false;

        // Check if any subject from the manifest exists in the input tokens
        // OR if the context_subject matches one of this capability's subjects
        for (const auto& sub : cap.subjects) {
            if (!context_subject.empty() && isFuzzyMatch(context_subject, sub)) {
                subject_found = true;
                break;
            }
            for (const auto& token : tokens) {
                if (isFuzzyMatch(token, sub)) { 
                    subject_found = true; 
                    break; 
                }
            }
            if (subject_found) break;
        }

        // Check if any action from the manifest exists in the input tokens or as a substring
        // OR if it's an affirmative generic trigger and context_subject is relevant to this cap
        for (const auto& act : cap.actions) {
            // Affirmative Mapping ( ok, yes, sure, do it )
            bool isAffirmative = (first == "ok" || first == "yes" || first == "sure" || input.find("do it") != std::string::npos);
            
            if (isAffirmative && subject_found) {
                action_found = true;
                break;
            }

            // Check full string for multi-word actions or explicit word boundaries
            if (act.length() > 3) {
                if (input.find(act) != std::string::npos) {
                    action_found = true;
                    break;
                }
            } else {
                // For short actions like "on", "off", check tokens only (already handled below)
            }

            // Check individual tokens
            for (const auto& token : tokens) {
                if (isFuzzyMatch(token, act)) {
                    action_found = true;
                    break;
                }
            }
            if (action_found) break;
        }

        if (subject_found && action_found) {
            LOGI(TAG, "> Dynamic Match: Found intent for %s (ID %u) [v3.9.5-STABLE]", cap.name.c_str(), cap.id);
            return {cap.id, cap.confidence_threshold, !isOff};
        }
    }

    // Tier 3: ONNX Inference Fallback (v3.9.4 Revival)
    if (m_inference_engine && m_inference_engine->isLoaded()) {
        auto intent = m_inference_engine->predict(input);
        
        LOGI(TAG, ">>> Tier 3 (ONNX) Verification: Input='%s' | Predicted_ID=%u | Confidence=%.2f", 
             input.c_str(), intent.id, intent.confidence);

        if (intent.confidence >= 0.6f) {
            LOGI(TAG, "> Tier 3 Match: ONNX Model confirmed ID %u", intent.id);
            intent.intent_param = !isOff;
            return intent;
        } else if (intent.confidence == 0.0f) {
            LOGE(TAG, "> CRITICAL: ONNX Model returned 0.0 confidence for input: '%s'", input.c_str());
        }
    }

    // Tier 4: Default Fallback
    return {1, 0.5f, true};
}

/**
 * Scalar fallback: Calculating dot product to minimize power usage in SEVERE thermal state.
 * Also used as host-side implementation for CI/CD on x86_64 or older ARM devices.
 */
static float compute_similarity_scalar(const int8_t* a, const int8_t* b) {
    int32_t dot_product = 0;
    for (int i = 0; i < 128; ++i) {
        dot_product += static_cast<int32_t>(a[i]) * static_cast<int32_t>(b[i]);
    }
    // Normalize based on INT8 max (127^2) to keep range [-1, 1]
    return static_cast<float>(dot_product) / 16129.0f;
}

#ifdef __aarch64__
/**
 * Internal helper to check if the current CPU supports ARMv8.2 Dot Product extension.
 */
static bool supports_dot_product() {
    static bool checked = false;
    static bool supported = false;
    if (!checked) {
        unsigned long hwcaps = getauxval(AT_HWCAP);
        supported = (hwcaps & HWCAP_ASIMDDP);
        checked = true;
        if (supported) {
            LOGI(TAG, "Hardware support detected: ASIMD Dot Product extension active.");
        } else {
            LOGI(TAG, "Hardware support not found: ASIMD Dot Product extension disabled.");
        }
    }
    return supported;
}
#endif

/**
 * Intent Similarity calculation.
 * Uses ARM64 NEON SIMD on mobile, with a scalar fallback for thermal throttling,
 * older hardware, or non-ARM hosts.
 */
float compute_intent_similarity_neon(const int8_t* a, const int8_t* b) {
    // 1. Non-ARM host fallback
#ifndef __aarch64__
    return compute_similarity_scalar(a, b);
#else
    // 2. Runtime Hardware Check: vdotq_s32 requires ARMv8.2-A DotProd
    // 3. Thermal Check: Fallback if SEVERE
    if (!supports_dot_product() || g_thermal_state == ThermalState::SEVERE) {
        return compute_similarity_scalar(a, b);
    }

    // Initialize accumulators with 0s
    int32x4_t acc = vdupq_n_s32(0);

    // Process 128 elements in chunks of 16 (128 / 16 = 8 iterations)
    for (int i = 0; i < 128; i += 16) {
        int8x16_t va = vld1q_s8(a + i);
        int8x16_t vb = vld1q_s8(b + i);
        // vdotq_s32 is extremely efficient on modern ARMv8-A (Cortex-A78/A55)
        acc = vdotq_s32(acc, va, vb);
    }

    // Convert int32x4_t to float32x4_t and perform final reduction
    float32x4_t f_acc = vcvtq_f32_s32(acc);
    float final_sum = vaddvq_f32(f_acc);

    return final_sum / 16129.0f;
#endif
}

float compute_cosine_similarity_neon(const float* a, const float* b, size_t length) {
#ifndef __aarch64__
    float dot = 0.0f, mag_a = 0.0f, mag_b = 0.0f;
    for (size_t i = 0; i < length; ++i) {
        dot += a[i] * b[i];
        mag_a += a[i] * a[i];
        mag_b += b[i] * b[i];
    }
    float denominator = std::sqrt(mag_a) * std::sqrt(mag_b);
    return (denominator < 1e-9f) ? 0.0f : (dot / denominator);
#else
    float32x4_t dot_vec = vdupq_n_f32(0.0f);
    float32x4_t mag_a_vec = vdupq_n_f32(0.0f);
    float32x4_t mag_b_vec = vdupq_n_f32(0.0f);

    for (size_t i = 0; i < length; i += 4) {
        float32x4_t va = vld1q_f32(a + i);
        float32x4_t vb = vld1q_f32(b + i);
        dot_vec = vmlaq_f32(dot_vec, va, vb);
        mag_a_vec = vmlaq_f32(mag_a_vec, va, va);
        mag_b_vec = vmlaq_f32(mag_b_vec, vb, vb);
    }

    float dot = vaddvq_f32(dot_vec);
    float mag_a = vaddvq_f32(mag_a_vec);
    float mag_b = vaddvq_f32(mag_b_vec);

    float denominator = std::sqrt(mag_a) * std::sqrt(mag_b);
    return (denominator < 1e-9f) ? 0.0f : (dot / denominator);
#endif
}

} // namespace Ronin::Kernel::Intent
