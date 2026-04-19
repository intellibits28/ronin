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
#include "capabilities/hardware_nodes.h"
#include "capabilities/file_search_node.h"
#include "capabilities/neural_embedding_node.h"
#include "capabilities/chat_skill.h"
#include "capabilities/hardware_bridge.h"

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

IntentEngine::IntentEngine() {
    using namespace Ronin::Kernel::Capability;
    
    // Phase 4.0: Vtable-based Skill Registration (Unified Interface)
    m_skill_registry[1] = std::make_shared<ChatSkill>();
    m_skill_registry[2] = std::make_shared<FileSearchNode>();
    m_skill_registry[3] = std::make_shared<NeuralEmbeddingNode>();
    m_skill_registry[4] = std::make_shared<FlashlightNode>();
    m_skill_registry[5] = std::make_shared<LocationNode>();
    m_skill_registry[6] = std::make_shared<WifiNode>();
    m_skill_registry[7] = std::make_shared<BluetoothNode>();
    
    LOGI(TAG, "IntentEngine: Modular Skill Registry initialized (Phase 4.0).");
}

std::string IntentEngine::executeSkill(uint32_t nodeId, const std::string& param) {
    auto it = m_skill_registry.find(nodeId);
    if (it != m_skill_registry.end()) {
        std::string logMsg = "> Deterministic Match: Routing to " + it->second->getName() + " (ID " + std::to_string(nodeId) + ")";
        LOGI(TAG, "%s", logMsg.c_str());
        Ronin::Kernel::Capability::HardwareBridge::pushMessage(logMsg);
        return it->second->execute(param);
    }
    std::string errorMsg = "Error: Modular skill not found for ID " + std::to_string(nodeId);
    LOGE(TAG, "%s", errorMsg.c_str());
    Ronin::Kernel::Capability::HardwareBridge::pushMessage(errorMsg);
    return errorMsg;
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
    
    LOGI(TAG, "Loading dynamic manifest: v4.0.0-STABLE");

    m_capabilities.clear();

    // Improved minimalist parser (Phase 4.0 stabilization)
    size_t pos = 0;
    while ((pos = content.find("\"id\":", pos)) != std::string::npos) {
        Ronin::Kernel::CapabilityEntry cap;
        
        // Extract ID (skip "id":)
        pos += 5;
        while (pos < content.length() && (std::isspace(content[pos]) || content[pos] == ':')) pos++;
        size_t id_end = content.find_first_of(",} \n\r", pos);
        if (id_end == std::string::npos) break;
        cap.id = std::stoul(content.substr(pos, id_end - pos));

        // Extract Name
        size_t name_tag = content.find("\"name\":", pos);
        if (name_tag == std::string::npos) break;
        size_t name_start = content.find("\"", name_tag + 7);
        if (name_start == std::string::npos) break;
        name_start++; // Skip opening quote
        size_t name_end = content.find("\"", name_start);
        if (name_end == std::string::npos) break;
        cap.name = trim(content.substr(name_start, name_end - name_start));

        // Extract Subjects
        size_t sub_tag = content.find("\"subjects\":", pos);
        if (sub_tag != std::string::npos) {
            size_t sub_start = content.find("[", sub_tag);
            if (sub_start != std::string::npos) {
                sub_start++; // Skip [
                size_t sub_end = content.find("]", sub_start);
                if (sub_end != std::string::npos) {
                    std::string subs = content.substr(sub_start, sub_end - sub_start);
                    std::stringstream ss_sub(subs);
                    std::string s;
                    while (std::getline(ss_sub, s, ',')) {
                        size_t s_start = s.find("\"");
                        if (s_start == std::string::npos) continue;
                        size_t s_end = s.find("\"", s_start + 1);
                        if (s_end != std::string::npos) {
                            cap.subjects.push_back(trim(s.substr(s_start + 1, s_end - s_start - 1)));
                        }
                    }
                }
            }
        }

        // Extract Actions
        size_t act_tag = content.find("\"actions\":", pos);
        if (act_tag != std::string::npos) {
            size_t act_start = content.find("[", act_tag);
            if (act_start != std::string::npos) {
                act_start++; // Skip [
                size_t act_end = content.find("]", act_start);
                if (act_end != std::string::npos) {
                    std::string acts = content.substr(act_start, act_end - act_start);
                    std::stringstream ss_act(acts);
                    std::string s;
                    while (std::getline(ss_act, s, ',')) {
                        size_t a_start = s.find("\"");
                        if (a_start == std::string::npos) continue;
                        size_t a_end = s.find("\"", a_start + 1);
                        if (a_end != std::string::npos) {
                            cap.actions.push_back(trim(s.substr(a_start + 1, a_end - a_start - 1)));
                        }
                    }
                }
            }
        }

        // Extract Threshold
        size_t conf_tag = content.find("\"confidence_threshold\":", pos);
        if (conf_tag != std::string::npos) {
            size_t conf_start = conf_tag + 23;
            while (conf_start < content.length() && (std::isspace(content[conf_start]) || content[conf_start] == ':')) conf_start++;
            size_t conf_end = content.find_first_of(",} \n\r", conf_start);
            if (conf_end != std::string::npos) {
                cap.confidence_threshold = std::stof(content.substr(conf_start, conf_end - conf_start));
            }
        }

        m_capabilities.push_back(cap);
        pos = content.find("}", pos);
        if (pos != std::string::npos) pos++;
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
    std::string first = tokens[0];

    // Tier 1: Dynamic Greeting Detection (Single Source of Truth: ID 1 from manifest)
    for (const auto& cap : m_capabilities) {
        if (cap.id == 1) {
            for (const auto& token : tokens) {
                for (const auto& sub : cap.subjects) {
                    if (isFuzzyMatch(token, sub)) {
                        // Only trigger if it's a pure greeting OR if no other high-confidence match is found later.
                        // For now, maintain Guardrail behavior as requested.
                        LOGI(TAG, "> Tier 1 Match: Greeting '%s' detected via manifest.", token.c_str());
                        return {1, 1.0f, true};
                    }
                }
            }
            break;
        }
    }

    // Safety-First Negation Logic (v3.9.4)
    // Synchronized with manifest actions for WiFi/Bluetooth/Flashlight
    bool isOff = (input.find("off") != std::string::npos || 
                  input.find("stop") != std::string::npos || 
                  input.find("disable") != std::string::npos ||
                  input.find("ပိတ်") != std::string::npos);

    // Tier 2: Dynamic Matcher (Subject + Action)
    for (const auto& cap : m_capabilities) {
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
                    
                    // Phase 4.0 Optimization: Interrogative Bypass
                    // If the subject is a standalone interrogative (where, who, what),
                    // treat it as both subject and action.
                    if (token == "where" || token == "who" || token == "what") {
                        action_found = true;
                        std::string logMsg = "> Interrogative Bypass: '" + token + "' triggered standalone match.";
                        LOGI(TAG, "%s", logMsg.c_str());
                        Ronin::Kernel::Capability::HardwareBridge::pushMessage(logMsg);
                    }
                    break; 
                }
            }
            if (subject_found) break;
        }

        // Check if any action from the manifest exists in the input tokens or as a substring
        // OR if it's an affirmative generic trigger and context_subject is relevant to this cap
        if (!action_found) {
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
        }

        if (subject_found && action_found) {
            std::string logMsg = "> Dynamic Match: Found intent for " + cap.name + " (ID " + std::to_string(cap.id) + ") [v3.9.5-STABLE]";
            LOGI(TAG, "%s", logMsg.c_str());
            Ronin::Kernel::Capability::HardwareBridge::pushMessage(logMsg);
            return {cap.id, cap.confidence_threshold, !isOff};
        }
    }

    // Tier 3: ONNX Inference Fallback (v3.9.4 Revival)
    if (m_inference_engine && m_inference_engine->isLoaded()) {
        auto intent = m_inference_engine->predict(input);
        
        std::string logMsg = ">>> Tier 3 (ONNX) Verification: Input='" + input + "' | Predicted_ID=" + std::to_string(intent.id) + " | Confidence=" + std::to_string(intent.confidence);
        LOGI(TAG, "%s", logMsg.c_str());
        Ronin::Kernel::Capability::HardwareBridge::pushMessage(logMsg);

        if (intent.confidence >= 0.6f) {
            std::string matchMsg = "> Tier 3 Match: ONNX Model confirmed ID " + std::to_string(intent.id);
            LOGI(TAG, "%s", matchMsg.c_str());
            Ronin::Kernel::Capability::HardwareBridge::pushMessage(matchMsg);
            intent.intent_param = !isOff;
            return intent;
        } else if (intent.confidence == 0.0f) {
            LOGE(TAG, "> CRITICAL: ONNX Model returned 0.0 confidence for input: '%s'", input.c_str());
        }
    }

    // Tier 4: Default Fallback
    std::string fallbackMsg = "> Tier 4: No confident match found. Routing to Default Chat Engine.";
    LOGI(TAG, "%s", fallbackMsg.c_str());
    Ronin::Kernel::Capability::HardwareBridge::pushMessage(fallbackMsg);
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
