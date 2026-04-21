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
#include <iomanip>
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
// PATCH 1: UTF-8 Safeproofing (Preserve multi-byte characters)
static std::string strip_punctuation(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c >= 0x80) { 
            out += (char)c; 
        } else if (std::isalnum(c) || std::isspace(c)) { 
            out += (char)c; 
        }
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

bool IntentEngine::handleCommand(const std::string& input, std::string& output) {
    if (input.empty() || input[0] != '/') return false;

    std::string cmd = trim(input);
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    using namespace Ronin::Kernel::Capability;

    if (cmd == "/status") {
        std::stringstream ss;
        ss << (m_inference_engine ? m_inference_engine->getRuntimeInfo() : "Runtime: LiteRT-LM / Backend: Unknown") << " | ";
        ss << "Health: " << std::fixed << std::setprecision(1) << HardwareBridge::getTemperature() << "°C | ";
        ss << std::setprecision(2) << HardwareBridge::getRamUsed() << "/" << HardwareBridge::getRamTotal() << "GB | ";
        ss << "LMK: " << (m_memory_manager ? m_memory_manager->getPressureScore() : 0) << "%";
        output = ss.str();
        return true;
    } 
    
    if (cmd == "/skills") {
        std::stringstream ss;
        ss << "Registered Skills: ";
        bool first = true;
        for (auto const& [id, skill] : m_skill_registry) {
            if (!first) ss << ", ";
            ss << skill->getName() << " (ID " << id << ")";
            first = false;
        }
        output = ss.str();
        return true;
    }

    if (cmd == "/model") {
        if (m_inference_engine) {
            output = "Loaded Brain: " + m_inference_engine->getModelPath();
        } else {
            output = "Loaded Brain: None (No inference engine attached)";
        }
        return true;
    }

    if (cmd == "/model --verify") {
        if (m_inference_engine) {
            long latency = m_inference_engine->verifyModel();
            if (latency >= 0) {
                output = "[VERIFY] " + m_inference_engine->getRuntimeInfo() + " | Latency: " + std::to_string(latency) + "ms";
            } else {
                output = "[VERIFY] Failed: Engine not loaded.";
            }
        } else {
            output = "[VERIFY] Error: Inference Engine missing.";
        }
        return true;
    }

    if (cmd == "/reset") {
        if (m_memory_manager) {
            m_memory_manager->clearContext();
            
            // Phase 4.4.6: Kernel Purge Recovery with OOM Guard
            if (m_inference_engine) {
                // If LMK pressure was high, re-hydrate with a smaller window
                int currentPressure = m_memory_manager->getPressureScore();
                if (currentPressure > 70) {
                    m_inference_engine->setContextWindow(512); // Minimum survival window
                    output = "Kernel State Purged. Survival Re-hydration: Context window reduced to 512 (OOM Guard active).";
                } else {
                    m_inference_engine->setContextWindow(2048); // Standard window
                    output = "Kernel State Purged. Memory Anchors Zeroed.";
                }
            } else {
                output = "Kernel State Purged. Memory Anchors Zeroed.";
            }
        } else {
            output = "Kernel State Purged. Memory Anchors Zeroed.";
        }
        return true;
    }

    output = "Unknown command: " + input;
    return true;
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

        // RULE 4: Respect g_thermal_state (v4.1 Hardware Reality)
        if (nodeId == 5 && g_thermal_state == ThermalState::SEVERE) {
            LOGW(TAG, "Thermal SEVERE: Falling back to cached GPS coordinates.");
            std::string cachedMsg = "Current Location (Cached): (" + std::to_string(m_last_lat) + ", " + std::to_string(m_last_lon) + ")";
            Ronin::Kernel::Capability::HardwareBridge::pushMessage("> SURVIVAL: Thermal Fallback active. Using cached GPS.");
            return cachedMsg;
        }

        // Phase 4.0: Zero-Stall LoRA Swap
        uint32_t loraMask = 0;
        if (m_lora_dispatcher) {
            uint32_t loraId = it->second->getLoraId();
            if (loraId > 0) {
                m_lora_dispatcher->activateSkill(loraId);
            }
            loraMask = m_lora_dispatcher->getActiveMask();
        }

        // Survival Core: Commit state BEFORE execution (with active LoRA mask and GPS)
        if (m_checkpoint_manager) {
            m_checkpoint_manager->commit("active_" + std::to_string(nodeId), 1ULL << nodeId, nullptr, 0, loraMask, "Running: " + param, m_last_lat, m_last_lon);
        }

        std::string result = it->second->execute(param);

        // Update cached GPS if this was a location request
        if (nodeId == 5 && result.find("(") != std::string::npos) {
            try {
                // Naive parse of "(lat, lon)"
                size_t start = result.find("(") + 1;
                size_t comma = result.find(",");
                size_t end = result.find(")");
                if (comma != std::string::npos && end != std::string::npos) {
                    m_last_lat = std::stod(result.substr(start, comma - start));
                    m_last_lon = std::stod(result.substr(comma + 1, end - comma - 1));
                }
            } catch (...) {
                LOGE(TAG, "Failed to parse GPS coordinates from hardware bridge.");
            }
        }

        // Survival Core: Commit state AFTER successful execution
        if (m_checkpoint_manager) {
            m_checkpoint_manager->commit("completed_" + std::to_string(nodeId), 0, nullptr, 0, loraMask, "Finished: " + param, m_last_lat, m_last_lon);
        }

        return result;
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

// PATCH 2: Byte-safe constraints for UTF-8 Fuzzy Matching
bool IntentEngine::isFuzzyMatch(std::string_view word, std::string_view target) {
    // Fast UTF-8 check: Exact match only for multi-byte characters to prevent corruption
    for (unsigned char c : word) {
        if (c & 0x80) return word == target;
    }
    for (unsigned char c : target) {
        if (c & 0x80) return word == target;
    }

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

// PATCH 3: Enforce Early Exit to prevent Pipeline Leak
CognitiveIntent IntentEngine::process(const std::string& input, const std::string& context_subject) {
    std::string_view sv_input = input;

    // Layer 0: Command Interface Interception (O(1) fast-path)
    std::string cmdOutput;
    if (handleCommand(input, cmdOutput)) {
        Ronin::Kernel::Capability::HardwareBridge::pushMessage("[COMMAND] " + cmdOutput);
        return {0, 1.0f, true}; // ID 0 signal for Command Handled
    }

    // Phase 4.4.9: Hard-Wired Greeting Routing (Logic)
    // Force-route greetings to ChatSkill (ID 1) to ensure LiteRT-LM handles them.
    if (sv_input.find("hi") != std::string_view::npos || 
        sv_input.find("hello") != std::string_view::npos || 
        sv_input.find("ဟေး") != std::string_view::npos || 
        sv_input.find("မင်္ဂလာပါ") != std::string_view::npos) {
        LOGI(TAG, ">>> Routing: Greeting Match (ID 1) bypassing confidence check.");
        return {1, 1.0f, true};
    }

    // Correct 12-byte UTF-8 representation for "ပိတ်" (Turn off)
    // Forced into memory as a raw char array to bypass NDK literal destruction
    const char mm_off[] = { 
        (char)0xE1, (char)0x80, (char)0x95, // ပ
        (char)0xE1, (char)0x80, (char)0xAD, // ိ
        (char)0xE1, (char)0x80, (char)0x90, // တ
        (char)0xE1, (char)0x80, (char)0xBA, // ်
        '\0' 
    };

    bool isOff = (input.find("off") != std::string::npos || 
                  input.find("stop") != std::string::npos || 
                  input.find("disable") != std::string::npos || 
                  input.find(mm_off) != std::string::npos);

    // Layer 0: O(1) Deterministic Match (Bypass Tokenizer & Loop)
    // Fast-path for unambiguous hardware and system commands.
    if (sv_input.find("flashlight") != std::string_view::npos || sv_input.find("torch") != std::string_view::npos || sv_input.find("\xE1\x80\x92\xE1\x80\xB9\xE1\x80\xAC\xE1\x80\x90\xE1\x80\xB9\xE1\x80\x99\xE1\x80\xB8") != std::string_view::npos) {
        LOGI(TAG, ">>> Routing: Deterministic Match (ID 4) bypassing Thompson Sampling.");
        return {4, 1.0f, !isOff};
    }
    if (sv_input.find("wifi") != std::string_view::npos || sv_input.find("\xE1\x80\x8D\xE1\x80\xAD\xE1\x80\xAF\xE1\x80\x84\xE1\x80\xB9\xE1\x80\x96\xE1\x80\xAD\xE1\x80\xAF\xE1\x80\x84\xE1\x80\xB9") != std::string_view::npos) {
        LOGI(TAG, ">>> Routing: Deterministic Match (ID 6) bypassing Thompson Sampling.");
        return {6, 1.0f, !isOff};
    }
    if (sv_input.find("bluetooth") != std::string_view::npos || sv_input.find("bt") != std::string_view::npos || sv_input.find("\xE1\x80\x98\xE1\x80\xAC\xE1\x80\x9C\xE1\x80\xB0\xE1\x80\xB8\xE1\x80\x90\xE1\x80\xAF") != std::string_view::npos) {
        LOGI(TAG, ">>> Routing: Deterministic Match (ID 7) bypassing Thompson Sampling.");
        return {7, 1.0f, !isOff};
    }
    if (sv_input.find("location") != std::string_view::npos || sv_input == "where am i" || sv_input == "where" || sv_input == "gps" || sv_input == "coordinates") {
        LOGI(TAG, ">>> Routing: Deterministic Match (ID 5) bypassing Thompson Sampling.");
        return {5, 1.0f, true};
    }
    if (sv_input.starts_with("search ") || sv_input.starts_with("find ") || sv_input.starts_with("locate ")) {
        LOGI(TAG, ">>> Routing: Deterministic Match (ID 2) bypassing Thompson Sampling.");
        return {2, 1.0f, true};
    }

    auto tokens = tokenize(input);
    if (tokens.empty()) return {1, 0.0f, true};
    std::string_view first = tokens[0];

    // Tier 1: Dynamic Greeting Detection (Single Source of Truth: ID 1 from manifest)
    // Optimized with std::string_view to reduce allocation overhead.
    for (const auto& cap : m_capabilities) {
        if (cap.id == 1) {
            for (std::string_view token : tokens) {
                for (std::string_view sub : cap.subjects) {
                    if (isFuzzyMatch(token, sub)) {
                        LOGI(TAG, "> Tier 1 Match: Greeting detected via manifest.");
                        return {1, 1.0f, true}; // EARLY EXIT
                    }
                }
            }
            break;
        }
    }

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
                        std::string logMsg = "> Interrogative Bypass: '" + std::string(token) + "' triggered standalone match.";
                        LOGI(TAG, "%s", logMsg.c_str());
                        Ronin::Kernel::Capability::HardwareBridge::pushMessage(logMsg);
                        // ENFORCE EARLY EXIT for Interrogative match
                        return {cap.id, cap.confidence_threshold, !isOff};
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

    // Tier 3: NPU-Accelerated Hierarchical Routing (Phase 4.2)
    if (m_inference_engine && m_inference_engine->isLoaded()) {
        // Layer 1: Coarse Classification (ACTION vs INFO)
        int coarse_cat = m_inference_engine->classifyCoarse(input);
        
        // Layer 2: Fine-grained Prediction on NPU
        auto intent = m_inference_engine->predictFine(input, coarse_cat);
        
        if (intent.id > 1) {
            std::string matchMsg = "> NPU Match (Tier 3): Confirmed ID " + std::to_string(intent.id) + 
                                   " with confidence " + std::to_string(intent.confidence);
            LOGI(TAG, "%s", matchMsg.c_str());
            Ronin::Kernel::Capability::HardwareBridge::pushMessage(matchMsg);
            intent.intent_param = !isOff;
            return intent;
        }
    }

    // Tier 4: Default Fallback
    // Phase 4.4.9.5: Router Confidence Boost (Updated 4.5.2)
    // Any input that reaches this tier is routed to ChatSkill (ID 1)
    // with 0.5 confidence to allow Cloud Bridge escalation if online.
    std::string fallbackMsg = "> Tier 4: Fallback Routing. Deferring to Chat Engine (ID 1) [Confidence: 0.5].";
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
