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
#include <set>
#include "ronin_log.h"
#include "capabilities/hardware_nodes.h"
#include "capabilities/file_search_node.h"
#include "capabilities/chat_skill.h"
#include "capabilities/memory_search_skill.h"
#include "capabilities/archive_memory_skill.h"
#include "capabilities/hardware_bridge.h"

#define TAG "RoninIntentEngine"

namespace Ronin::Kernel::Intent {

using json = nlohmann::json;

// --- TaskPlanner Implementation ---

TaskPlanner::TaskPlanner(Model::InferenceEngine* engine) : m_engine(engine) {}

bool TaskPlanner::parsePlan(const std::string& json_str, AgentPlan& out_plan) {
    try {
        auto j = json::parse(json_str);
        
        out_plan.intent_name = j.value("intent", "unknown");
        out_plan.raw_json = json_str;
        
        if (j.contains("required_tools") && j["required_tools"].is_array()) {
            for (const auto& t : j["required_tools"]) out_plan.required_tools.push_back(t.get<std::string>());
        }
        
        if (j.contains("required_permissions") && j["required_permissions"].is_array()) {
            for (const auto& p : j["required_permissions"]) out_plan.required_permissions.push_back(p.get<std::string>());
        }

        if (j.contains("plan") && j["plan"].is_array()) {
            for (const auto& s : j["plan"]) out_plan.plan_steps.push_back(s.get<std::string>());
        }

        if (j.contains("parameters") && j["parameters"].is_object()) {
            for (auto& [key, value] : j["parameters"].items()) {
                if (value.is_string()) out_plan.parameters[key] = value.get<std::string>();
            }
        }
        
        LOGI("RoninPlanner", "v7.2 Plan parsed: %s with %zu tools and %zu steps.", 
             out_plan.intent_name.c_str(), out_plan.required_tools.size(), out_plan.plan_steps.size());
        return true;
    } catch (const std::exception& e) {
        LOGE("RoninPlanner", "JSON Parse Error: %s", e.what());
        return false;
    }
}

AgentPlan TaskPlanner::createPlan(const std::string& input) {
    AgentPlan plan;
    if (!m_engine) return plan;

    // v11.3.1: Hardened Orchestration Prompt (Strict Precision)
    std::string system_prompt = 
        "[INTERNAL] You are the Ronin Cognitive Controller. Output ONLY valid JSON. "
        "Identity: Deterministic Hardware/Knowledge Orchestrator. "
        "Mode: Non-conversational. Zero Hallucination. "
        "Rule 1 (Storage): If user says 'remember', 'save', or 'store', use intent 'ADD_FACT' or 'ADD_NOTE'. "
        "Rule 2 (Vault): If input contains 'key', 'password', 'token', or 'AIza', ALWAYS use intent 'ADD_VAULT', steps ['SAVE_VAULT']. "
        "Rule 3 (Retrieval): If user asks 'what is', 'show me', or 'where', use intent 'LOOKUP_FACT', 'SEARCH_NOTES', or 'SEARCH_EPISODES'. "
        "Rule 4 (Facts): Structured info (e.g. car plate, medicine) -> 'ADD_FACT', steps ['SAVE_FACT']. Params: 'entity', 'attribute', 'value'. "
        "Rule 5 (Notes): General thoughts -> 'ADD_NOTE', steps ['SAVE_NOTE']. Params: 'note_title', 'note_content'. "
        "Rule 6 (Planning): Use exact steps. Show Map -> ['GET_LOCATION', 'OPEN_MAP']. Send SMS -> ['GET_LOCATION', 'RESOLVE_CONTACT', 'SEND_SMS']. "
        "Rule 7 (Mandatory): You MUST extract and populate the required parameters from the user's input. Never leave 'value' or 'content' empty if the user provided it. "
        "Parameters: 'note_title', 'note_content', 'entity', 'attribute', 'value', 'vault_title', 'vault_content', 'query', 'recipient_name'. "
        "Schema: {\"intent\": \"...\", \"required_tools\": [], \"plan\": [], \"parameters\": {}}";

    // Requesting a reasoning cycle from the engine
    std::string llm_json = m_engine->runLiteRTReasoning(input, system_prompt); 
    
    LOGI("RoninPlanner", "v9.1 Raw Output: %s", llm_json.c_str());
    
    // Extract JSON block
    size_t start = llm_json.find("{");
    size_t end = llm_json.rfind("}");
    if (start != std::string::npos && end != std::string::npos) {
        llm_json = llm_json.substr(start, end - start + 1);
    }
    
    LOGI("RoninPlanner", "v9.1 Extracted JSON: %s", llm_json.c_str());

    if (!parsePlan(llm_json, plan)) {
        LOGE("RoninPlanner", "v9.1 Parser Failed. Setting fallback.");
        plan.intent_name = "fallback_chat";
    }
    
    return plan;
}

CapabilityType TaskPlanner::mapIntentToCapability(const std::string& intent_name) {
    if (intent_name == "show_location" || intent_name == "get_location" || intent_name == "where_am_i") {
        return CapabilityType::LOCATION;
    }
    if (intent_name == "send_sms" || intent_name == "send_message" || intent_name == "sms_location") {
        return CapabilityType::SMS;
    }
    if (intent_name == "measure_resonance" || intent_name == "vibration_analysis") {
        return CapabilityType::SENSOR;
    }
    return CapabilityType::NONE;
}

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

    if (cmd.starts_with("/debug_intent")) {
        std::string text = (cmd.length() > 14) ? cmd.substr(14) : "";
        auto intent = process(text, "");
        output = "--- Intent Debug ---\nText: " + text + 
                 "\nCategory: " + std::to_string(static_cast<int>(intent.category)) +
                 "\nSkill ID: " + std::to_string(intent.id) +
                 "\nPlanner Ready: " + (m_planner ? "YES" : "NO");
        return true;
    }

    if (cmd == "/test_agent") {
        output = "[DIAGNOSTIC] Triggering Mock Agent Sequence...\nStep 1: JNI Routing test.\nStep 2: Scheduler Test.";
        // We'll hook this to a mock intent in JNI
        return true;
    }

    if (cmd == "/clear") {
        return false; // Handled elsewhere
    }

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
    
    m_skill_registry[2] = std::make_shared<FileSearchNode>(m_ltm);
    m_skill_registry[4] = std::make_shared<FlashlightNode>();
    m_skill_registry[5] = std::make_shared<LocationNode>();
    m_skill_registry[6] = std::make_shared<WifiNode>();
    m_skill_registry[7] = std::make_shared<BluetoothNode>();
    m_skill_registry[8] = std::make_shared<MemorySearchSkill>(m_ltm);
    m_skill_registry[9] = std::make_shared<ArchiveMemorySkill>(m_ltm);
    
    m_planner = std::make_unique<TaskPlanner>(nullptr); // Will be attached later
    
    LOGI(TAG, "IntentEngine: Hardened v4.8 token-based registry initialized.");
}

std::string IntentEngine::executeSkill(uint32_t nodeId, const std::string& param, ToolContext* context) {
    if (nodeId == 0) return m_last_command_output;

    if (m_current_tool_depth >= MAX_TOOL_CALL_DEPTH) {
        return "Error: Maximum tool call depth reached.";
    }

    auto it = m_skill_registry.find(nodeId);
    if (it != m_skill_registry.end()) {
        m_current_tool_depth++;
        
        if (nodeId == 5 && g_thermal_state == ThermalState::SEVERE) {
            return "Current Location (Cached): (" + std::to_string(m_last_lat) + ", " + std::to_string(m_last_lon) + ")";
        }

        std::string result = it->second->execute(param, context);

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
    if (!file.is_open()) return;
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    m_capabilities.clear();
    
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
        return {0, 1.0f, true, IntentCategory::TOOL_QUERY};
    }

    // Hardened v4.8+: Hybrid Token Spine (Segmenter + Space-based)
    std::set<std::string> token_set;
    
    // 1. Add standard space-based tokens
    auto base_tokens = tokenize(input_lower);
    token_set.insert(base_tokens.begin(), base_tokens.end());

    // 2. Add Myanmar segmented tokens if segmenter is available
    bool has_mm_tokens = false;
    if (m_ltm && m_ltm->getSegmenter()) {
        std::string segmented = m_ltm->getSegmenter()->segment(input_lower);
        std::stringstream ss(segmented);
        std::string token;
        while (ss >> token) {
            token_set.insert(token);
            has_mm_tokens = true;
        }
        LOGI(TAG, "v6.0 Tokens Injected: %s", segmented.c_str());
    }

    // --- Adaptive Intent Categorization ---
    IntentCategory final_cat = IntentCategory::CHAT_QUERY;
    
    // v7.2: Enhanced Semantic Analysis for AGENT_PLAN
    bool is_complex = (token_set.count("sms") || token_set.count("message") || token_set.count("မက်ဆေ့") || token_set.count("ပို့") || token_set.count("send")) && 
                      (token_set.count("location") || token_set.count("တည်နေရာ") || token_set.count("နေရာ"));
    
    bool is_simple_agent = token_set.count("location") || token_set.count("တည်နေရာ") || 
                           token_set.count("မြေပုံ") || token_set.count("map") ||
                           token_set.count("show") || token_set.count("ပြ") ||
                           token_set.count("navigate") || token_set.count("open") ||
                           token_set.count("ပို့ပေး") || token_set.count("send") ||
                           token_set.count("remember") || token_set.count("မှတ်ထား") ||
                           token_set.count("note") || token_set.count("medicine") ||
                           token_set.count("ဆေး") || token_set.count("fact") ||
                           token_set.count("plate") || token_set.count("license") ||
                           token_set.count("car") || token_set.count("ကား") ||
                           token_set.count("key") || token_set.count("api") ||
                           token_set.count("token") || token_set.count("password") ||
                           token_set.count("သတိပေး") || token_set.count("remind");

    // v6.0 Semantic Guard-rail: Detect Inquiries (Information seeking vs Action)
    bool is_inquiry = token_set.count("ဘာလဲ") || token_set.count("ဘယ်လို") || 
                      token_set.count("နည်းလမ်း") || token_set.count("ရှင်းပြပါ") ||
                      token_set.count("ရှိလဲ") || token_set.count("သိချင်လို့") ||
                      token_set.count("how") || token_set.count("what") || 
                      token_set.count("why") || token_set.count("explain");

    if ((is_complex || is_simple_agent) && !is_inquiry) {
        final_cat = IntentCategory::AGENT_PLAN;
        LOGI(TAG, "v7.0 Agent Request Detected -> Category AGENT_PLAN");
    } else {
        // Check for Memory Query (e.g. "မှတ်မိလား", "အရင်က")
        if (token_set.count("မှတ်မိလား") || token_set.count("အရင်က") || token_set.count("မှတ်ဉာဏ်")) {
            final_cat = IntentCategory::MEMORY_QUERY;
        }
    }

    for (const auto& cap : m_capabilities) {
        if (cap.id == 1) continue; 

        // Semantic Guard: Inquiries should mostly route to CHAT unless specific hardware keywords are dominant
        if (is_inquiry && cap.id != 8 && cap.id != 9) {
            // Allow memory skills but block generic hardware triggers in inquiry context
            continue; 
        }

        bool subject_found = false;
        std::string matched_subject;
        for (const auto& s : cap.subjects) {
            if (token_set.count(s)) { 
                subject_found = true; 
                matched_subject = s;
                break; 
            }
            if (input_lower.find(s) != std::string::npos && s.length() > 6) { 
                subject_found = true; 
                matched_subject = s;
                break; 
            }
        }

        if (subject_found) {
            bool action_found = false;
            std::string matched_action;
            for (const auto& a : cap.actions) {
                if (token_set.count(a)) { 
                    action_found = true; 
                    matched_action = a;
                    break; 
                }
                if (input_lower.find(a) != std::string::npos && a.length() > 5) { 
                    action_found = true; 
                    matched_action = a;
                    break; 
                }
            }

            if (action_found) {
                // v4.0+ Hardened: LocationNode (ID 5) safety
                if (cap.id == 5) {
                    bool strictly_location = (input_lower.find("တည်နေရာ") != std::string::npos) || 
                                           (input_lower.find("gps") != std::string::npos) ||
                                           (input_lower.find("မြေပုံ") != std::string::npos);
                    if (!strictly_location) continue;
                }

                LOGI(TAG, "Hardened Match: ID %u (%s). Subject: '%s', Action: '%s'", 
                     cap.id, cap.name.c_str(), matched_subject.c_str(), matched_action.c_str());
                return {cap.id, 1.0f, true, IntentCategory::TOOL_QUERY};
            }
        }
    }

    // Default to categorized CHAT or MEMORY
    LOGI(TAG, "v6.0 Intent: Category %u", static_cast<uint32_t>(final_cat));
    return {1, 1.0f, true, final_cat}; 
}

} // namespace Ronin::Kernel::Intent
