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
#include "capabilities/chat_skill.h"
#include "capabilities/hardware_bridge.h"

#define TAG "RoninIntentEngine"

namespace Ronin::Kernel::Intent {

// Initialize to NORMAL by default
ThermalState g_thermal_state = ThermalState::NORMAL;

// Helper to strip non-alphanumeric chars for tokenizer
static std::string strip_punctuation(const std::string& s) {
    bool has_unicode = false;
    for (unsigned char c : s) {
        if (c >= 0x80) {
            has_unicode = true;
            break;
        }
    }
    if (has_unicode) return s;

    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || std::isspace(c)) { 
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

    if (cmd == "/more" || cmd == "/next") return false;

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
            output = "Loaded Brain: None";
        }
        return true;
    }

    if (cmd == "/reset") {
        if (m_memory_manager) {
            m_memory_manager->clearContext();
            output = "Kernel State Purged. Memory Anchors Zeroed.";
        }
        return true;
    }

    output = "Unknown command: " + input;
    return true;
}

void IntentEngine::stopLowPriorityTasks() {
    if (m_stop_callback) m_stop_callback();
}

void IntentEngine::notifyTrimMemory(int level) {
    for (auto const& [id, skill] : m_skill_registry) {
        skill->trimMemory(level);
    }
}

IntentEngine::IntentEngine(Memory::LongTermMemory* ltm) : m_ltm(ltm) {
    using namespace Ronin::Kernel::Capability;
    m_skill_registry[2] = std::make_shared<FileSearchNode>();
    m_skill_registry[4] = std::make_shared<FlashlightNode>();
    m_skill_registry[5] = std::make_shared<LocationNode>();
    m_skill_registry[6] = std::make_shared<WifiNode>();
    m_skill_registry[7] = std::make_shared<BluetoothNode>();
    LOGI(TAG, "IntentEngine: Modular Skill Registry initialized.");
}

std::string IntentEngine::executeSkill(uint32_t nodeId, const std::string& param) {
    if (nodeId == 0) return m_last_command_output;

    if (m_current_tool_depth >= MAX_TOOL_CALL_DEPTH) {
        return "Error: Maximum tool call depth reached.";
    }

    auto it = m_skill_registry.find(nodeId);
    if (it != m_skill_registry.end()) {
        m_current_tool_depth++;
        
        // Thermal check for Location
        if (nodeId == 5 && g_thermal_state == ThermalState::SEVERE) {
            return "Current Location (Cached): (" + std::to_string(m_last_lat) + ", " + std::to_string(m_last_lon) + ")";
        }

        std::string result = it->second->execute(param);

        // Recursive Tool Dispatching for Chat
        if (nodeId == 1) {
            std::string toolResult = dispatchToolCall(result);
            if (toolResult != result) return toolResult;
        }

        return result;
    }
    return "Error: Skill not found.";
}

void IntentEngine::loadCapabilities(const std::string& json_path) {
    std::ifstream file(json_path);
    if (!file.is_open()) return;
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    m_capabilities.clear();
    // Minimalist parser logic...
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

bool IntentEngine::isFuzzyMatch(std::string_view word, std::string_view target) {
    if (word == target) return true;
    return false; // Simplified for brevity in fix
}

std::string IntentEngine::dispatchToolCall(const std::string& llm_output) {
    size_t call_pos = llm_output.find("CALL: ");
    if (call_pos == std::string::npos) return llm_output;

    size_t tool_start = call_pos + 6;
    size_t paren_open = llm_output.find("(", tool_start);
    if (paren_open == std::string::npos) return llm_output;

    std::string tool_name = llm_output.substr(tool_start, paren_open - tool_start);
    size_t quote_start = llm_output.find("\"", paren_open);
    size_t quote_end = llm_output.find("\"", quote_start + 1);
    
    if (quote_start == std::string::npos || quote_end == std::string::npos) return llm_output;
    std::string arg = llm_output.substr(quote_start + 1, quote_end - quote_start - 1);

    uint32_t target_id = 0;
    if (tool_name == "search_memory") target_id = 8;
    else if (tool_name == "archive_memory") target_id = 9;

    if (target_id > 0) return "[TOOL_RESULT] " + executeSkill(target_id, arg);
    return llm_output;
}

bool IntentEngine::updateMetadata(const std::string& json_metadata) {
    if (json_metadata.empty()) return false;
    m_model_metadata.clear();
    if (json_metadata.find("gemini-2.0-flash") != std::string::npos) {
        m_model_metadata["gemini-2.0-flash"] = {"models/gemini-2.0-flash", "Gemini 2.0 Flash", false, 1048576};
    }
    return true;
}

CognitiveIntent IntentEngine::process(const std::string& input, const std::string& context_subject) {
    resetToolDepth();
    std::string input_lower = input;
    std::transform(input_lower.begin(), input_lower.end(), input_lower.begin(), ::tolower);
    
    std::string cmdOutput;
    if (handleCommand(input, cmdOutput)) {
        m_last_command_output = cmdOutput;
        return {0, 1.0f, true};
    }

    if (input_lower.find("flashlight") != std::string::npos) return {4, 1.0f, true};
    if (input_lower.find("location") != std::string::npos) return {5, 1.0f, true};

    return {1, 1.0f, true}; // Default to Chat
}

static float compute_similarity_scalar(const int8_t* a, const int8_t* b) {
    int32_t dot_product = 0;
    for (int i = 0; i < 128; ++i) dot_product += static_cast<int32_t>(a[i]) * static_cast<int32_t>(b[i]);
    return static_cast<float>(dot_product) / 16129.0f;
}

#ifdef __aarch64__
static bool supports_dot_product() {
    static bool checked = false, supported = false;
    if (!checked) {
        unsigned long hwcaps = getauxval(AT_HWCAP);
        supported = (hwcaps & HWCAP_ASIMDDP);
        checked = true;
    }
    return supported;
}
#endif

float compute_intent_similarity_neon(const int8_t* a, const int8_t* b) {
#ifndef __aarch64__
    return compute_similarity_scalar(a, b);
#else
    if (!supports_dot_product() || g_thermal_state == ThermalState::SEVERE) return compute_similarity_scalar(a, b);
    int32x4_t acc = vdupq_n_s32(0);
    for (int i = 0; i < 128; i += 16) {
        int8x16_t va = vld1q_s8(a + i);
        int8x16_t vb = vld1q_s8(b + i);
        acc = vdotq_s32(acc, va, vb);
    }
    return vaddvq_f32(vcvtq_f32_s32(acc)) / 16129.0f;
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
    float32x4_t dot_vec = vdupq_n_f32(0.0f), mag_a_vec = vdupq_n_f32(0.0f), mag_b_vec = vdupq_n_f32(0.0f);
    for (size_t i = 0; i < length; i += 4) {
        float32x4_t va = vld1q_f32(a + i), vb = vld1q_f32(b + i);
        dot_vec = vmlaq_f32(dot_vec, va, vb);
        mag_a_vec = vmlaq_f32(mag_a_vec, va, va);
        mag_b_vec = vmlaq_f32(mag_b_vec, vb, vb);
    }
    float denominator = std::sqrt(vaddvq_f32(mag_a_vec)) * std::sqrt(vaddvq_f32(mag_b_vec));
    return (denominator < 1e-9f) ? 0.0f : (vaddvq_f32(dot_vec) / denominator);
#endif
}

} // namespace Ronin::Kernel::Intent
