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

TaskPlanner::TaskPlanner(Model::InferenceEngine* engine) : m_engine(engine) {}

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

    // v11.3.10: Final Multi-Modal Prompt (Security & Precision Mastery)
    std::string system_prompt = 
        "[INTERNAL] You are the Ronin Cognitive Runtime. Output ONLY valid JSON. Skip thinking tags. "
        "Rules: "
        "- Map/SMS: steps ['GET_LOCATION', 'OPEN_MAP'] / ['GET_LOCATION', 'RESOLVE_CONTACT', 'SEND_SMS']. "
        "- Fact/Vault Save: intent 'ADD_FACT' / 'ADD_VAULT'. Steps ['SAVE_FACT'] / ['SAVE_VAULT']. "
        "- Vault Keywords: ALWAYS use Vault for PIN, API key, password, token, or secret. "
        "- Fact/Vault Find: intent 'LOOKUP_FACT' / 'LOOKUP_VAULT'. Steps ['QUERY_FACT'] / ['QUERY_VAULT']. "
        "Semantic Precision: "
        "- Input: 'Aung Aung ရဲ့ မွေးနေ့ မှတ်မိလား' -> entity='Aung Aung', attribute='မွေးနေ့'. "
        "- Constraint: Strip 'ရဲ့', 'မှတ်မိလား', 'ဘာလဲ' from parameters. "
        "Schema: {\"intent\": \"...\", \"plan\": [], \"parameters\": {\"entity\": \"...\", \"attribute\": \"...\", \"value\": \"...\"}}";

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
    std::string i_lower = intent_name;
    std::transform(i_lower.begin(), i_lower.end(), i_lower.begin(), ::tolower);

    if (i_lower.find("location") != std::string::npos || i_lower.find("map") != std::string::npos) {
        return CapabilityType::LOCATION;
    }
    if (i_lower.find("sms") != std::string::npos || i_lower.find("message") != std::string::npos) {
        return CapabilityType::SMS;
    }
    if (i_lower.find("note") != std::string::npos || i_lower.find("fact") != std::string::npos || 
        i_lower.find("vault") != std::string::npos || i_lower.find("lookup") != std::string::npos ||
        i_lower.find("search") != std::string::npos || i_lower.find("remember") != std::string::npos) {
        return CapabilityType::MEMORY;
    }
    if (i_lower.find("resonance") != std::string::npos || i_lower.find("vibration") != std::string::npos) {
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
    m_planner = std::make_unique<TaskPlanner>(m_inference_engine.get());
    
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
        return true;
    }

    if (cmd == "/test_agent") {
        output = "[DIAGNOSTIC] Triggering Mock Agent Sequence...\nStep 1: JNI Routing test.\nStep 2: Scheduler Test.";
        return true;
    }

    if (cmd == "/status") {
        std::stringstream ss;
        ss << (m_inference_engine ? m_inference_engine->getRuntimeInfo() : "Runtime: LiteRT-LM") << " | ";
        ss << "Health: " << std::fixed << std::setprecision(1) << Ronin::Kernel::Capability::HardwareBridge::getTemperature() << "°C | ";
        ss << std::setprecision(2) << Ronin::Kernel::Capability::HardwareBridge::getRamUsed() << "/" << Ronin::Kernel::Capability::HardwareBridge::getRamTotal() << "GB";
        output = ss.str();
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
        return true;
    }

    if (cmd == "/model") {
        output = "Active Brain: " + (m_inference_engine ? m_inference_engine->getModelPath() : "None");
        return true;
    }

    if (cmd == "/reset") {
        output = "[SYSTEM] Internal State Reset requested.";
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
    m_planner = std::make_unique<TaskPlanner>(nullptr); 
}

CognitiveIntent IntentEngine::process(const std::string& input, const std::string& history) {
    std::string input_lower = input;
    std::transform(input_lower.begin(), input_lower.end(), input_lower.begin(), ::tolower);
    std::string stripped = strip_punctuation(input_lower);
    std::stringstream ss(stripped);
    std::string t;
    std::set<std::string> token_set;
    while (ss >> t) token_set.insert(t);

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
                           token_set.count("မှတ်မိ") || token_set.count("သိမ်းထား");

    bool is_inquiry = token_set.count("ဘာလဲ") || token_set.count("ဘယ်လို") || 
                      token_set.count("နည်းလမ်း") || token_set.count("ရှင်းပြပါ") ||
                      token_set.count("ရှိလဲ") || token_set.count("သိချင်လို့") ||
                      token_set.count("how") || token_set.count("what") || 
                      token_set.count("why") || token_set.count("explain") ||
                      token_set.count("?") || token_set.count("လား");

    if ((is_complex || is_simple_agent) && !is_inquiry) {
        final_cat = IntentCategory::AGENT_PLAN;
        LOGI(TAG, "v7.0 Agent Request Detected -> Category AGENT_PLAN");
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
