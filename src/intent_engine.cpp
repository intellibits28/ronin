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
        ss << "Health: " << std::fixed << std::setprecision(1) << Ronin::Kernel::Capability::HardwareBridge::getTemperature() << "deg C | ";
        ss << std::setprecision(2) << Ronin::Kernel::Capability::HardwareBridge::getRamUsed() << "/" << Ronin::Kernel::Capability::HardwareBridge::getRamTotal() << "GB | ";
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
        std::string depthError = "Guard-rail: MAX_TOOL_CALL_DEPTH reached.";
        LOGW(TAG, "%s", depthError.c_str());
        return "Error: Maximum tool call depth reached. Termination forced.";
    }

    auto it = m_skill_registry.find(nodeId);
    if (it != m_skill_registry.end()) {
        m_current_tool_depth++;
        
        if (nodeId == 5 && g_thermal_state == ThermalState::SEVERE) {
            LOGW(TAG, "Thermal SEVERE: Using cached GPS.");
            return "Current Location (Cached): (" + std::to_string(m_last_lat) + ", " + std::to_string(m_last_lon) + ")";
        }

        std::string result = it->second->execute(param);

        // Phase 4: Recursive Tool Dispatching for Chat
        if (nodeId == 1) {
            std::string toolResult = dispatchToolCall(result);
            if (toolResult != result) return toolResult;
        }

        return result;
    }
    return "Error: Modular skill not found.";
}

void IntentEngine::loadCapabilities(const std::string& json_path) {
    std::ifstream file(json_path);
    if (!file.is_open()) {
        LOGE(TAG, "Failed to load capabilities from %s", json_path.c_str());
        return;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    m_capabilities.clear();
    
    // Improved JSON-ish parser for subjects, actions, and ids
    size_t pos = 0;
    while ((pos = content.find("\"id\"", pos)) != std::string::npos) {
        CapabilityEntry cap;
        size_t id_start = content.find(":", pos) + 1;
        cap.id = std::stoi(content.substr(id_start, content.find(",", id_start) - id_start));
        
        auto parse_array = [&](const std::string& field, std::vector<std::string>& out) {
            size_t f_pos = content.find("\"" + field + "\"", pos);
            if (f_pos != std::string::npos) {
                size_t start_arr = content.find("[", f_pos) + 1;
                size_t end_arr = content.find("]", start_arr);
                std::string raw = content.substr(start_arr, end_arr - start_arr);
                std::stringstream ss(raw);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    size_t q1 = item.find("\"");
                    size_t q2 = item.find("\"", q1 + 1);
                    if (q1 != std::string::npos && q2 != std::string::npos) {
                        out.push_back(item.substr(q1 + 1, q2 - q1 - 1));
                    }
                }
            }
        };

        parse_array("subjects", cap.subjects);
        parse_array("actions", cap.actions);
        
        m_capabilities.push_back(cap);
        pos = content.find("}", pos);
    }
    LOGI(TAG, "Loaded %zu capabilities for Dual-Condition Matching.", m_capabilities.size());
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
    return false;
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

    if (target_id > 0) {
        return "[TOOL_RESULT] " + executeSkill(target_id, arg);
    }

    return llm_output;
}

bool IntentEngine::updateMetadata(const std::string& json_metadata) {
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

    // Phase 11.0: Dual-Condition Intent Routing (Subject + Action)
    // To prevent false positives like "Where" -> "GPS" in general questions.
    for (const auto& cap : m_capabilities) {
        // Chat (ID 1) always handled by LLM if no hardware action matched
        if (cap.id == 1) continue; 

        bool subject_match = false;
        for (const auto& s : cap.subjects) {
            if (input_lower.find(s) != std::string::npos) {
                subject_match = true;
                break;
            }
        }

        if (subject_match) {
            // Check for explicit action verbs (on, show, find, etc.)
            bool action_match = false;
            for (const auto& a : cap.actions) {
                if (input_lower.find(a) != std::string::npos) {
                    action_match = true;
                    break;
                }
            }

            if (action_match) {
                LOGI(TAG, "Hardware Intent Matched (Subject+Action): ID %u", cap.id);
                return {cap.id, 1.0f, true};
            }
        }
    }

    // Default: If no hardware intent (Subject+Action) matched, let Gemma 4 reason (ID 1)
    return {1, 1.0f, true}; 
}

} // namespace Ronin::Kernel::Intent
