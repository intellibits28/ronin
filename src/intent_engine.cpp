#include "intent_engine.h"
#include <algorithm>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include "ronin_log.h"
#include "capabilities/file_search_node.h"
#include "capabilities/hardware_nodes.h"
#include "capabilities/chat_skill.h"

#define TAG "RoninIntent"

namespace Ronin::Kernel::Intent {

TaskPlanner::TaskPlanner(Model::InferenceEngine* engine, Reasoning::BeliefState* belief_state, Memory::LongTermMemory* ltm) 
    : m_engine(engine), m_belief_state(belief_state), m_ltm(ltm) {}

bool TaskPlanner::parsePlan(const std::string& llm_json, AgentPlan& out_plan) {
    try {
        auto j = nlohmann::json::parse(llm_json);
        
        // v10.2.10: Handle array output if Gemma wraps the object
        nlohmann::json root;
        if (j.is_array() && !j.empty()) root = j[0];
        else root = j;

        out_plan.intent_name = root.value("intent", "fallback_chat");
        
        if (root.contains("required_tools") && root["required_tools"].is_array()) {
            for (const auto& t : root["required_tools"]) out_plan.required_tools.push_back(t.get<std::string>());
        }
        
        if (root.contains("required_permissions") && root["required_permissions"].is_array()) {
            for (const auto& p : root["required_permissions"]) out_plan.required_permissions.push_back(p.get<std::string>());
        }

        if (root.contains("plan") && root["plan"].is_array()) {
            for (const auto& s : root["plan"]) {
                if (s.is_string()) {
                    out_plan.plan_steps.push_back(s.get<std::string>());
                } else if (s.is_object() && s.contains("action")) {
                    out_plan.plan_steps.push_back(s["action"].get<std::string>());
                    if (s.contains("parameters") && s["parameters"].is_object()) {
                        for (auto& [key, value] : s["parameters"].items()) {
                            if (value.is_string()) out_plan.parameters[key] = value.get<std::string>();
                        }
                    }
                }
            }
        }

        if (root.contains("parameters") && root["parameters"].is_object()) {
            for (auto& [key, value] : root["parameters"].items()) {
                if (value.is_string()) out_plan.parameters[key] = value.get<std::string>();
            }
        }
        
        // v1.6 Cognitive Hardening: Forceful Intent Correction (Anti-Hallucination)
        std::string original_query = "";
        if (out_plan.parameters.count("original_query")) {
            original_query = out_plan.parameters["original_query"];
        }
        std::string lower_query = original_query;
        std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);
        
        if (lower_query.find("api key") != std::string::npos || lower_query.find("password") != std::string::npos || 
            lower_query.find("secret") != std::string::npos || lower_query.find("vault") != std::string::npos) {
            if (out_plan.intent_name.find("FACT") != std::string::npos) {
                LOGW("RoninPlanner", "v1.6 Hardening: Overriding Hallucinated FACT intent to VAULT for sensitive query.");
                out_plan.intent_name = (out_plan.intent_name == "SAVE_FACT" || out_plan.intent_name == "ADD_FACT") ? "SAVE_VAULT" : "LOOKUP_VAULT";
                if (out_plan.plan_steps.size() > 0) out_plan.plan_steps[0] = out_plan.intent_name;
            }
        }
        
        if (lower_query.find("နှိုး") != std::string::npos || lower_query.find("alarm") != std::string::npos || lower_query.find("wake") != std::string::npos) {
            if (out_plan.intent_name.find("EVENT") != std::string::npos || out_plan.intent_name.find("CALENDAR") != std::string::npos) {
                LOGW("RoninPlanner", "v1.6 Hardening: Overriding Hallucinated EVENT intent to ALARM for wake query.");
                out_plan.intent_name = "SET_ALARM";
                if (out_plan.plan_steps.size() > 0) out_plan.plan_steps[0] = "SET_ALARM";
            }
        }

        // Force SAVE_FACT or SAVE_VAULT if user explicitly asks to save/remember
        if (lower_query.find("မှတ်ထား") != std::string::npos || lower_query.find("သိမ်းထား") != std::string::npos || 
            lower_query.find("save") != std::string::npos || lower_query.find("store") != std::string::npos) {
            
            if (out_plan.intent_name == "LOOKUP_FACT" || out_plan.intent_name == "QUERY_FACT") {
                LOGW("RoninPlanner", "v1.6 Hardening: Overriding Hallucinated LOOKUP intent to SAVE for memory query.");
                out_plan.intent_name = "SAVE_FACT";
                if (out_plan.plan_steps.size() > 0) out_plan.plan_steps[0] = "SAVE_FACT";
            } else if (out_plan.intent_name == "LOOKUP_VAULT" || out_plan.intent_name == "QUERY_VAULT") {
                LOGW("RoninPlanner", "v1.6 Hardening: Overriding Hallucinated LOOKUP_VAULT intent to SAVE_VAULT for vault query.");
                out_plan.intent_name = "SAVE_VAULT";
                if (out_plan.plan_steps.size() > 0) out_plan.plan_steps[0] = "SAVE_VAULT";
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

    // v1.6: Behavioral Evolution - Dynamic Lesson Injection
    std::string lessons_context = "";
    if (m_ltm) {
        auto lessons = m_ltm->searchNotes("lesson");
        if (!lessons.empty()) {
            lessons_context = "\n[LESSONS LEARNED FROM PREVIOUS FAILURES]:\n";
            for (const auto& lesson : lessons) {
                lessons_context += "- " + lesson + "\n";
            }
            lessons_context += "[End of Lessons. DO NOT repeat these mistakes.]\n";
            LOGI("RoninPlanner", "Injected %zu behavioral lessons into prompt.", lessons.size());
        }
        
        // v1.6 Phase 5: Inject Discovered Macro-Skills
        auto macros = m_ltm->searchNotes("macro_skill");
        if (!macros.empty()) {
            lessons_context += "\n[AVAILABLE MACRO-SKILLS]:\n";
            for (const auto& macro : macros) {
                lessons_context += "- " + macro + "\n";
            }
            lessons_context += "You may use these Macro-Skills in your plan.\n";
            LOGI("RoninPlanner", "Injected %zu macro-skills into prompt.", macros.size());
        }
    }

    // v12.14: Constrained Prompting (Rigid JSON Enforcement)
    std::string system_prompt = 
        "Output ONLY JSON. "
        "Rules: "
        "1. ALARM: 'SET_ALARM'. "
        "2. CALENDAR: 'ADD_EVENT', 'READ_CALENDAR'. "
        "3. VAULT/FACT: ONLY use if saving/retrieving personal data. "
        "4. FALLBACK: For general knowledge, definitions, explanations, or essays, set intent to 'fallback_chat'. "
        "5. MAP: 'GET_LOCATION' then 'OPEN_MAP'. "
        "6. SMS: 'GET_LOCATION' then 'SEND_SMS'. "
        "7. SENSOR: 'ANALYZE_VIBRATION'. "
        "8. FILES: 'FILE_SEARCH'. "
        "Schema: {\"intent\":\"...\",\"plan\":[\"...\"],\"parameters\":{...}} "
        "Examples: "
        "User: 'သစ္စာ ၄ပါး ရှင်းပြပါ' -> {\"intent\":\"fallback_chat\",\"plan\":[],\"parameters\":{}} "
        "User: 'Resonance ဆိုတာ ဘာလဲ' -> {\"intent\":\"fallback_chat\",\"plan\":[],\"parameters\":{}} "
        "User: 'Toyota Wish car plate' -> {\"intent\":\"LOOKUP_FACT\",\"plan\":[\"LOOKUP_FACT\"],\"parameters\":{\"entity\":\"Toyota Wish\",\"attribute\":\"license plate\"}} "
        + lessons_context;

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

    // v12.20: Inject original query into parameters for better tool context and anti-hallucination overrides
    plan.parameters["original_query"] = input;

    if (!parsePlan(llm_json, plan)) {
        LOGE("RoninPlanner", "v9.1 Parser Failed for raw output: %s", llm_json.c_str());
        plan.intent_name = "fallback_chat";
    }
    
    return plan;
}

CapabilityType TaskPlanner::mapIntentToCapability(const std::string& intent_name) {
    std::string i_lower = intent_name;
    std::transform(i_lower.begin(), i_lower.end(), i_lower.begin(), ::tolower);

    // v12.15: Strict Intent-to-Capability Mapping
    if (i_lower == "map" || i_lower == "open_map" || i_lower == "location" || i_lower == "get_location") {
        return CapabilityType::LOCATION;
    }
    if (i_lower == "sms" || i_lower == "send_sms" || i_lower == "send_sms_with_location") {
        return CapabilityType::SMS;
    }
    if (i_lower == "sensor_analysis" || i_lower == "get_sensor_analysis") {
        return CapabilityType::SENSOR;
    }
    if (i_lower == "calendar" || i_lower == "add_event" || i_lower == "read_calendar") {
        return CapabilityType::CALENDAR;
    }
    if (i_lower == "file_search" || i_lower == "search_files" || i_lower == "files") {
        return CapabilityType::FILES;
    }
    if (i_lower.find("fact") != std::string::npos || i_lower.find("vault") != std::string::npos || i_lower == "memory") {
        return CapabilityType::MEMORY;
    }
    if (i_lower == "alarm" || i_lower == "set_alarm") {
        return CapabilityType::ALARM;
    }
    
    // v12.16: Substring fallback for LLM hallucinations
    if (i_lower.find("location") != std::string::npos || i_lower.find("map") != std::string::npos) return CapabilityType::LOCATION;
    if (i_lower.find("sms") != std::string::npos) return CapabilityType::SMS;
    if (i_lower.find("sensor") != std::string::npos || i_lower.find("vibration") != std::string::npos) return CapabilityType::SENSOR;
    if (i_lower.find("calendar") != std::string::npos || i_lower.find("meeting") != std::string::npos) return CapabilityType::CALENDAR;

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

void IntentEngine::loadCapabilities(const std::string& json_path) {
    try {
        std::ifstream f(json_path);
        if (!f.is_open()) return;
        auto j = nlohmann::json::parse(f);
        m_capabilities.clear();
        for (const auto& item : j) {
            CapabilityEntry cap;
            cap.id = item["id"];
            cap.name = item["name"];
            for (const auto& s : item["subjects"]) cap.subjects.push_back(s);
            for (const auto& a : item["actions"]) cap.actions.push_back(a);
            cap.confidence_threshold = item["confidence_threshold"];
            m_capabilities.push_back(cap);
        }
        LOGI(TAG, "Loaded %zu capabilities from manifest.", m_capabilities.size());
    } catch (const std::exception& e) {
        LOGE(TAG, "Failed to parse capabilities: %s", e.what());
    }
}

std::string IntentEngine::executeSkill(uint32_t nodeId, const std::string& param, ToolContext* context) {
    auto it = m_skill_registry.find(nodeId);
    if (it != m_skill_registry.end()) {
        LOGI(TAG, "v4.0 Execution: Triggering modular skill ID %u", nodeId);
        return it->second->execute(param, context);
    }
    return "Error: Modular skill not found.";
}

void IntentEngine::setInferenceEngine(std::unique_ptr<Model::InferenceEngine> engine) {
    m_inference_engine = std::move(engine);
    m_planner = std::make_unique<TaskPlanner>(m_inference_engine.get(), m_belief_state);
    
    // v10.2.10: Update ChatSkill engine if it's already registered
    auto skill = getSkill(1); // Node 1 is ChatSkill
    if (skill) {
        auto chat = std::dynamic_pointer_cast<Ronin::Kernel::Capability::ChatSkill>(skill);
        if (chat) {
            registerSkill(1, std::make_shared<Ronin::Kernel::Capability::ChatSkill>(m_inference_engine.get(), m_ltm));
        }
    }
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
        Ronin::Kernel::Capability::HardwareBridge::pushMessage(output);
        return true;
    }

    if (cmd == "/reflect") {
        output = "[SYSTEM] Manual Reflection Triggered. Consolidating memory and synthesizing behavioral lessons...";
        Ronin::Kernel::Capability::HardwareBridge::pushMessage(output);
        if (m_planner) {
            // Internal call to trigger reflection via the engine instance
            // In JNI context, we call the native function directly
        }
        return true;
    }

    if (cmd == "/test_agent") {
        output = "[DIAGNOSTIC] Triggering Mock Agent Sequence...\nStep 1: JNI Routing test.\nStep 2: Scheduler Test.";
        Ronin::Kernel::Capability::HardwareBridge::pushMessage(output);
        return true;
    }

    if (cmd == "/status") {
        std::stringstream ss;
        ss << (m_inference_engine ? m_inference_engine->getRuntimeInfo() : "Runtime: LiteRT-LM") << " | ";
        ss << "Health: " << std::fixed << std::setprecision(1) << Ronin::Kernel::Capability::HardwareBridge::getTemperature() << "°C | ";
        ss << std::setprecision(2) << Ronin::Kernel::Capability::HardwareBridge::getRamUsed() << "/" << Ronin::Kernel::Capability::HardwareBridge::getRamTotal() << "GB";
        output = ss.str();
        Ronin::Kernel::Capability::HardwareBridge::pushMessage("[STATUS] " + output);
        return true;
    } 

    if (cmd == "/skills") {
        std::stringstream ss;
        ss << "Active Nodes: ";
        bool first = true;
        for (auto const& [id, skill] : m_skill_registry) {
            if (!first) ss << ", ";
            ss << skill->getName() << " (" << id << ")";
            first = false;
        }
        output = ss.str();
        Ronin::Kernel::Capability::HardwareBridge::pushMessage("[SKILLS] " + output);
        return true;
    }

    if (cmd == "/model") {
        output = "Active Brain: " + (m_inference_engine ? m_inference_engine->getModelPath() : "None");
        Ronin::Kernel::Capability::HardwareBridge::pushMessage("[MODEL] " + output);
        return true;
    }

    if (cmd == "/reset") {
        output = "[SYSTEM] Internal State Reset requested.";
        Ronin::Kernel::Capability::HardwareBridge::pushMessage(output);
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
    m_skill_registry[1] = std::make_shared<ChatSkill>(nullptr, m_ltm);
    m_planner = std::make_unique<TaskPlanner>(nullptr, nullptr); 
}

CognitiveIntent IntentEngine::process(const std::string& input, const std::string& history) {
    std::string input_lower = input;
    std::transform(input_lower.begin(), input_lower.end(), input_lower.begin(), ::tolower);
    
    // v12.0: Use Trie-based segmenter for precise Myanmar keyword extraction
    std::vector<std::string> tokens;
    if (m_ltm) {
        tokens = m_ltm->segmentText(input_lower);
    } else {
        std::string stripped = strip_punctuation(input_lower);
        std::stringstream ss(stripped);
        std::string t;
        while (ss >> t) tokens.push_back(t);
    }
    
    std::set<std::string> token_set(tokens.begin(), tokens.end());

    IntentCategory final_cat = IntentCategory::CHAT_QUERY;

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
                           token_set.count("သတိပေး") || token_set.count("remind") ||
                           token_set.count("မွေးနေ့") || token_set.count("birthday") ||
                           token_set.count("မှတ်မိ") || token_set.count("သိမ်းထား") ||
                           token_set.count("retrieve") || token_set.count("find") ||
                           token_set.count("search") || token_set.count("lookup") ||
                           token_set.count("ရှာ") || token_set.count("ပြန်ရှာ") ||
                           token_set.count("alarm") || token_set.count("နှိုး") ||
                           token_set.count("calendar") || token_set.count("meeting") ||
                           token_set.count("event") || token_set.count("pdf") ||
                           token_set.count("doc") || token_set.count("txt") ||
                           token_set.count("ဖိုင်") || token_set.count("မနက်ဖြန်") ||
                           token_set.count("ဒီနေ့") || token_set.count("ချိန်း") ||
                           token_set.count("vault") || token_set.count("လုံခြုံ");

    // v12.18: Decouple sensor from simple agent to prevent location->sensor misrouting
    bool is_sensor_req = token_set.count("တုန်ခါမှု") || token_set.count("vibration") || 
                         token_set.count("resonance") || token_set.count("sensor");

    // v12.2: Raw substring fallback for robust detection
    if (!is_simple_agent && !is_sensor_req) {
        is_simple_agent = (input_lower.find("alarm") != std::string::npos) ||
                          (input_lower.find("နှိုး") != std::string::npos) ||
                          (input_lower.find("meeting") != std::string::npos) ||
                          (input_lower.find("မနက်ဖြန်") != std::string::npos) ||
                          (input_lower.find("ဒီနေ့") != std::string::npos) ||
                          (input_lower.find("ချိန်း") != std::string::npos) ||
                          (input_lower.find("vault") != std::string::npos) ||
                          (input_lower.find("လုံခြုံ") != std::string::npos) ||
                          (input_lower.find("ရှာ") != std::string::npos);
    }
    
    if (!is_sensor_req) {
        is_sensor_req = (input_lower.find("တုန်ခါမှု") != std::string::npos) ||
                        (input_lower.find("vibration") != std::string::npos);
    }

    bool is_inquiry = token_set.count("နည်းလမ်း") || token_set.count("ရှင်းပြပါ") ||
                      token_set.count("explain") || token_set.count("how to") || 
                      token_set.count("why");

    // v12.11: Strong action keywords override inquiry
    if (is_simple_agent || is_complex || is_sensor_req) {
        final_cat = IntentCategory::AGENT_PLAN;
    }

    // v12.13: Final Keyword Override (Security Hardening & Point 4 Fallback)
    if (input_lower.find("နှိုး") != std::string::npos || input_lower.find("alarm") != std::string::npos || 
        input_lower.find("meeting") != std::string::npos || input_lower.find("calendar") != std::string::npos ||
        input_lower.find("တုန်ခါမှု") != std::string::npos || input_lower.find("vibration") != std::string::npos ||
        input_lower.find("မှတ်ထား") != std::string::npos || input_lower.find("မှတ်မိ") != std::string::npos ||
        input_lower.find("ရှာပေး") != std::string::npos || input_lower.find("ရှာပါ") != std::string::npos ||
        input_lower.find("pdf") != std::string::npos || input_lower.find("doc") != std::string::npos ||
        input_lower.find("file") != std::string::npos || input_lower.find("ဖိုင်") != std::string::npos ||
        input_lower.find("တည်နေရာ") != std::string::npos || input_lower.find("location") != std::string::npos ||
        input_lower.find("vault") != std::string::npos || input_lower.find("လုံခြုံ") != std::string::npos) {
        final_cat = IntentCategory::AGENT_PLAN;
    }

    // v12.21: Explicit Priority Override to prevent misrouting
    if (input_lower.find("location") != std::string::npos || input_lower.find("တည်နေရာ") != std::string::npos) {
        is_sensor_req = false; // Never route location to sensor
    }
    if (input_lower.find("key") != std::string::npos || input_lower.find("password") != std::string::npos || input_lower.find("api") != std::string::npos) {
        is_sensor_req = false;
    }

    if (final_cat == IntentCategory::AGENT_PLAN) {
        LOGI(TAG, "v7.0 Agent Request Detected -> Category AGENT_PLAN");
        Ronin::Kernel::Capability::HardwareBridge::updateDevHUD("PLANNING", "PENDING", 0.0f, "");
        return {1, 1.0f, true, final_cat}; 
    }

    for (const auto& cap : m_capabilities) {
        if (cap.id == 1) continue; 
        bool subject_found = false;
        for (const auto& s : cap.subjects) {
            if (token_set.count(s) || (input_lower.find(s) != std::string::npos && s.length() > 6)) {
                subject_found = true; break;
            }
        }
        if (subject_found) {
            for (const auto& a : cap.actions) {
                if (token_set.count(a) || (input_lower.find(a) != std::string::npos && a.length() > 5)) {
                    if (cap.id >= 10) return {1, 1.0f, true, IntentCategory::AGENT_PLAN};
                    return {cap.id, 1.0f, true, IntentCategory::TOOL_QUERY};
                }
            }
        }
    }

    LOGI(TAG, "v6.0 Intent: Category %u", static_cast<uint32_t>(final_cat));
    return {1, 1.0f, true, final_cat}; 
}

} // namespace Ronin::Kernel::Intent
