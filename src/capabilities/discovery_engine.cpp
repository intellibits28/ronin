#include "capabilities/discovery_engine.h"
#include <algorithm>
#include <sstream>
#include <set>
#include <unordered_set>
#include "ronin_log.h"

#define TAG "RoninDiscoveryEngine"

namespace Ronin::Kernel::Reasoning {

CapabilityDiscoveryEngine::CapabilityDiscoveryEngine() {}

static std::set<std::string> tokenizeText(const std::string& text) {
    std::set<std::string> tokens;
    std::string text_lower = text;
    std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);
    
    for (char& c : text_lower) {
        if (std::ispunct(c)) {
            c = ' ';
        }
    }
    
    std::stringstream ss(text_lower);
    std::string token;
    while (ss >> token) {
        if (token.length() > 1) {
            tokens.insert(token);
        }
    }
    return tokens;
}

static float computeJaccard(const std::set<std::string>& s1, const std::set<std::string>& s2) {
    if (s1.empty() || s2.empty()) return 0.0f;
    
    std::vector<std::string> intersection;
    std::set_intersection(s1.begin(), s1.end(), s2.begin(), s2.end(), std::back_inserter(intersection));
    
    std::vector<std::string> union_set;
    std::set_union(s1.begin(), s1.end(), s2.begin(), s2.end(), std::back_inserter(union_set));
    
    return (float)intersection.size() / union_set.size();
}

std::vector<Capability::ToolMetadata> CapabilityDiscoveryEngine::resolveCapabilities(const std::vector<std::string>& requirements) {
    auto& registry = Capability::ToolRegistry::getInstance();
    std::vector<Capability::ToolMetadata> matches;
    
    // Compute total tokens across all requirements
    std::set<std::string> req_tokens;
    for (const auto& req : requirements) {
        auto tokens = tokenizeText(req);
        req_tokens.insert(tokens.begin(), tokens.end());
    }
    
    if (req_tokens.empty()) {
        return matches;
    }
    
    // Fetch all tools from registry (using a broad search or fetching all)
    auto all_tools = registry.searchTools("");
    
    struct ScoredTool {
        Capability::ToolMetadata meta;
        float score;
    };
    std::vector<ScoredTool> scored_tools;
    
    for (const auto& tool : all_tools) {
        std::set<std::string> tool_tokens = tokenizeText(tool.name + " " + tool.description);
        float jaccard = computeJaccard(req_tokens, tool_tokens);
        
        // Boost score slightly if tool outputs match one of the requirement query words
        float type_boost = 0.0f;
        for (const auto& out_type : tool.outputs) {
            if (req_tokens.count(out_type)) {
                type_boost += 0.2f;
            }
        }
        
        float final_score = jaccard + type_boost;
        if (final_score > 0.05f) {
            scored_tools.push_back({tool, final_score});
        }
    }
    
    // Sort in descending order of score, with secondary sorting on success_rate and latency
    std::sort(scored_tools.begin(), scored_tools.end(), [](const ScoredTool& a, const ScoredTool& b) {
        if (std::abs(a.score - b.score) > 0.001f) {
            return a.score > b.score;
        }
        if (std::abs(a.meta.success_rate - b.meta.success_rate) > 0.001f) {
            return a.meta.success_rate > b.meta.success_rate;
        }
        return a.meta.average_latency_ms < b.meta.average_latency_ms;
    });
    
    for (const auto& st : scored_tools) {
        matches.push_back(st.meta);
        LOGI(TAG, "Resolved tool: %s (score: %.3f)", st.meta.name.c_str(), st.score);
    }
    
    return matches;
}

std::vector<std::string> CapabilityDiscoveryEngine::buildExecutionGraph(
    const std::vector<Capability::ToolMetadata>& resolved_tools,
    const std::vector<std::string>& initial_inputs
) {
    std::vector<std::string> graph_sequence;
    std::unordered_set<std::string> available_types(initial_inputs.begin(), initial_inputs.end());
    std::vector<Capability::ToolMetadata> remaining_tools = resolved_tools;
    
    bool progressed = true;
    while (!remaining_tools.empty() && progressed) {
        progressed = false;
        
        for (auto it = remaining_tools.begin(); it != remaining_tools.end(); ) {
            bool inputs_satisfied = true;
            for (const auto& req_input : it->inputs) {
                if (available_types.find(req_input) == available_types.end()) {
                    inputs_satisfied = false;
                    break;
                }
            }
            
            if (inputs_satisfied) {
                // Schedule this tool
                graph_sequence.push_back(it->name);
                
                // Outputs are now available types
                for (const auto& out_type : it->outputs) {
                    available_types.insert(out_type);
                }
                
                it = remaining_tools.erase(it);
                progressed = true;
            } else {
                ++it;
            }
        }
    }
    
    // Log if there are unresolved dependencies
    if (!remaining_tools.empty()) {
        for (const auto& tool : remaining_tools) {
            LOGW(TAG, "Tool %s has unresolved input dependencies", tool.name.c_str());
        }
    }
    
    return graph_sequence;
}

} // namespace Ronin::Kernel::Reasoning
