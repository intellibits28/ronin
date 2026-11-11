#include "semantic_router.h"
#include "myanmar_linguistic_normalizer.h"
#include <cmath>
#include <sstream>
#include <algorithm>

namespace Ronin::Kernel::Intent {

SemanticRouter::SemanticRouter() {
    initializeDefaultRoutes();
}

uint32_t SemanticRouter::hashFeature(const std::string& feat) {
    // FNV-1a 32-bit hash
    uint32_t hash = 2166136261u;
    for (unsigned char c : feat) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

SemanticRouter::SparseVector SemanticRouter::vectorize(const std::string& text) {
    SparseVector vec;
    auto norm_res = NLP::MyanmarLinguisticNormalizer::normalize(text);
    std::string clean = norm_res.cleaned_text.empty() ? text : norm_res.cleaned_text;
    if (clean.empty()) return vec;

    std::unordered_map<uint32_t, float> feat_map;

    // 1. Word unigrams (weight = 2.0f)
    std::stringstream ss(clean);
    std::string word;
    while (ss >> word) {
        uint32_t h = hashFeature("w:" + word);
        feat_map[h] += 2.0f;
    }

    // 2. Character 3-grams and 4-grams (captures Myanmar syllables and English morphology)
    if (clean.length() >= 3) {
        for (size_t i = 0; i + 3 <= clean.length(); ++i) {
            uint32_t h3 = hashFeature("3g:" + clean.substr(i, 3));
            feat_map[h3] += 1.0f;
        }
    }
    if (clean.length() >= 4) {
        for (size_t i = 0; i + 4 <= clean.length(); ++i) {
            uint32_t h4 = hashFeature("4g:" + clean.substr(i, 4));
            feat_map[h4] += 1.0f;
        }
    }

    // Convert to sorted vector and calculate L2 norm
    vec.features.reserve(feat_map.size());
    float sum_sq = 0.0f;
    for (const auto& [id, val] : feat_map) {
        vec.features.push_back({id, val});
        sum_sq += val * val;
    }
    std::sort(vec.features.begin(), vec.features.end(), [](const Feature& a, const Feature& b) {
        return a.id < b.id;
    });

    // Refinement 1: Individual exemplar vectors are strictly unit-normalized
    if (sum_sq > 1e-12f) {
        float inv_norm = 1.0f / std::sqrt(sum_sq);
        for (auto& f : vec.features) {
            f.weight *= inv_norm;
        }
        vec.norm = 1.0f;
    } else {
        vec.norm = 0.0f;
    }

    return vec;
}

float SemanticRouter::cosineSimilarity(const SparseVector& a, const SparseVector& b) {
    if (a.norm <= 1e-6f || b.norm <= 1e-6f) return 0.0f;

    float dot = 0.0f;
    size_t ia = 0, ib = 0;
    const size_t na = a.features.size();
    const size_t nb = b.features.size();

    while (ia < na && ib < nb) {
        if (a.features[ia].id < b.features[ib].id) {
            ia++;
        } else if (b.features[ib].id < a.features[ia].id) {
            ib++;
        } else {
            dot += a.features[ia].weight * b.features[ib].weight;
            ia++;
            ib++;
        }
    }

    // Since both a and b are strictly unit L2 normalized, dot product is exact cosine similarity
    return std::clamp(dot, -1.0f, 1.0f);
}

void SemanticRouter::addRoute(const RouteDefinition& route) {
    m_legacy_routes.push_back(route);
    m_indexed = false;
}

void SemanticRouter::addSubCluster(const IntentSubCluster& cluster) {
    m_sub_clusters.push_back(cluster);
    m_indexed = false;
}

void SemanticRouter::setThreshold(const std::string& intent_name, float threshold) {
    m_custom_thresholds[intent_name] = threshold;
    for (auto& g : m_indexed_groups) {
        if (g.name == intent_name) {
            g.threshold = threshold;
        }
    }
}

void SemanticRouter::setDenseCentroid(const std::string& intent_name, LanguageRegister reg, const std::vector<float>& centroid) {
    DenseSubClusterIndex d_idx;
    d_idx.intent_name = intent_name;
    d_idx.reg = reg;
    d_idx.full_cluster_name = intent_name + "_" + languageRegisterToString(reg);
    d_idx.centroid = centroid;
    l2Normalize(d_idx.centroid);
    m_dense_centroids.push_back(std::move(d_idx));
}

void SemanticRouter::buildIndex() {
    m_indexed_groups.clear();

    // Map: intent_name -> vector of SubCentroidIndex
    std::unordered_map<std::string, std::vector<SubCentroidIndex>> group_map;

    // 1. Process 3-register sub-clusters (Refinement 2)
    for (const auto& sub : m_sub_clusters) {
        SubCentroidIndex s_idx;
        s_idx.intent_name = sub.intent_name;
        s_idx.reg = sub.reg;
        s_idx.full_cluster_name = sub.intent_name + sub.register_suffix;
        s_idx.utterances = sub.exemplars;

        // Sum normalized exemplar vectors: sum(v_hat_i)
        std::unordered_map<uint32_t, float> feat_acc;
        for (const auto& utt : sub.exemplars) {
            SparseVector u_vec = vectorize(utt);
            if (u_vec.norm > 1e-6f) {
                for (const auto& f : u_vec.features) {
                    feat_acc[f.id] += f.weight;
                }
            }
        }

        // Refinement 1: Strict L2 Re-Normalization of the centroid
        // C_k = sum(v_hat) / ||sum(v_hat)||_2
        float sum_sq = 0.0f;
        for (const auto& [id, val] : feat_acc) {
            sum_sq += val * val;
        }

        if (sum_sq > 1e-12f) {
            float inv_norm = 1.0f / std::sqrt(sum_sq);
            s_idx.centroid_vec.features.reserve(feat_acc.size());
            for (const auto& [id, val] : feat_acc) {
                s_idx.centroid_vec.features.push_back({id, val * inv_norm});
            }
            std::sort(s_idx.centroid_vec.features.begin(), s_idx.centroid_vec.features.end(),
                      [](const Feature& a, const Feature& b) { return a.id < b.id; });
            s_idx.centroid_vec.norm = 1.0f; // Exactly 1.0
        }

        group_map[sub.intent_name].push_back(std::move(s_idx));
    }

    // 2. Process legacy routes if any
    for (const auto& r : m_legacy_routes) {
        if (group_map.find(r.name) == group_map.end()) {
            SubCentroidIndex s_idx;
            s_idx.intent_name = r.name;
            s_idx.reg = LanguageRegister::MIX;
            s_idx.full_cluster_name = r.name + "_MIX";
            s_idx.utterances = r.utterances;

            std::unordered_map<uint32_t, float> feat_acc;
            for (const auto& utt : r.utterances) {
                SparseVector u_vec = vectorize(utt);
                if (u_vec.norm > 1e-6f) {
                    for (const auto& f : u_vec.features) {
                        feat_acc[f.id] += f.weight;
                    }
                }
            }

            float sum_sq = 0.0f;
            for (const auto& [id, val] : feat_acc) {
                sum_sq += val * val;
            }

            if (sum_sq > 1e-12f) {
                float inv_norm = 1.0f / std::sqrt(sum_sq);
                s_idx.centroid_vec.features.reserve(feat_acc.size());
                for (const auto& [id, val] : feat_acc) {
                    s_idx.centroid_vec.features.push_back({id, val * inv_norm});
                }
                std::sort(s_idx.centroid_vec.features.begin(), s_idx.centroid_vec.features.end(),
                          [](const Feature& a, const Feature& b) { return a.id < b.id; });
                s_idx.centroid_vec.norm = 1.0f;
            }

            group_map[r.name].push_back(std::move(s_idx));
        }
    }

    // Assemble final indexed groups
    for (auto& [name, clusters] : group_map) {
        IntentGroup g;
        g.name = name;
        if (m_custom_thresholds.find(name) != m_custom_thresholds.end()) {
            g.threshold = m_custom_thresholds[name];
        } else if (name == "CHAT_QUERY") {
            g.threshold = 0.28f;
        } else if (name.find("VAULT") != std::string::npos) {
            g.threshold = 0.28f;
        } else {
            g.threshold = 0.32f;
        }
        g.sub_clusters = std::move(clusters);
        m_indexed_groups.push_back(std::move(g));
    }

    m_indexed = true;
}

bool SemanticRouter::performLexicalTieBreak(
    const std::string& query,
    const std::string& top_intent,
    const std::string& runner_up,
    std::string& out_resolved_intent
) {
    auto norm_res = NLP::MyanmarLinguisticNormalizer::normalize(query);
    std::string q_clean = norm_res.cleaned_text.empty() ? query : norm_res.cleaned_text;
    std::transform(q_clean.begin(), q_clean.end(), q_clean.begin(), ::tolower);

    // Mutation markers: update, change, replace, modify, etc.
    const std::vector<std::string> mutation_markers = {
        "update", "change", "replace", "modify", "edit", "reset",
        "ပြောင်း", "ပြင်", "လဲ", "အသစ်"
    };

    // Save/creation markers: save, store, remember, etc.
    const std::vector<std::string> save_markers = {
        "save", "store", "remember", "keep", "record",
        "သိမ်း", "မှတ်", "ထည့်"
    };

    // Lookup/read markers: show, get, lookup, find, what is, etc.
    const std::vector<std::string> lookup_markers = {
        "show", "get", "lookup", "find", "what is", "tell", "view", "check",
        "ပြ", "ရှာ", "တောင်း", "ကြည့်"
    };

    bool has_mutation = false;
    for (const auto& m : mutation_markers) {
        if (q_clean.find(m) != std::string::npos) {
            has_mutation = true;
            break;
        }
    }

    bool has_save = false;
    for (const auto& s : save_markers) {
        if (q_clean.find(s) != std::string::npos) {
            has_save = true;
            break;
        }
    }

    bool has_lookup = false;
    for (const auto& l : lookup_markers) {
        if (q_clean.find(l) != std::string::npos) {
            has_lookup = true;
            break;
        }
    }

    bool candidates_are_vault = (
        top_intent.find("VAULT") != std::string::npos ||
        runner_up.find("VAULT") != std::string::npos
    );

    if (candidates_are_vault) {
        if (has_mutation) {
            out_resolved_intent = "VAULT_UPDATE";
            return true;
        }
        if (has_save && !has_mutation) {
            out_resolved_intent = "VAULT_SAVE";
            return true;
        }
        if (has_lookup && !has_mutation && !has_save) {
            out_resolved_intent = "VAULT_LOOKUP";
            return true;
        }
    }

    // Hardware vs Setting markers
    if ((top_intent == "FLASHLIGHT" && runner_up == "DEVICE_SETTINGS") ||
        (top_intent == "DEVICE_SETTINGS" && runner_up == "FLASHLIGHT")) {
        if (q_clean.find("flashlight") != std::string::npos || q_clean.find("torch") != std::string::npos ||
            q_clean.find("ဓာတ်မီး") != std::string::npos || q_clean.find("မီး") != std::string::npos) {
            out_resolved_intent = "FLASHLIGHT";
            return true;
        }
        if (q_clean.find("wifi") != std::string::npos || q_clean.find("bluetooth") != std::string::npos ||
            q_clean.find("ဝိုင်ဖိုင်") != std::string::npos || q_clean.find("ဘလူးတု") != std::string::npos) {
            out_resolved_intent = "DEVICE_SETTINGS";
            return true;
        }
    }

    return false; // Unable to break tie deterministically
}

RouteMatch SemanticRouter::route(const std::string& query) const {
    RouteMatch result;
    if (!m_indexed || m_indexed_groups.empty()) return result;

    SparseVector q_vec = vectorize(query);
    if (q_vec.norm <= 1e-6f) return result;

    struct ScoredIntent {
        std::string intent_name;
        float score = 0.0f;
        float threshold = 0.30f;
        std::string best_sub_cluster;
    };

    std::vector<ScoredIntent> scored_intents;
    scored_intents.reserve(m_indexed_groups.size());

    // Refinement 2: Evaluate intent score via max-pooling across sub-centroids:
    // score(k) = max_{j in {EN, MM, MIX}} (q_hat . C_{k,j})
    for (const auto& group : m_indexed_groups) {
        float max_sub_sim = -1.0f;
        std::string best_cluster_name = group.name + "_MIX";

        for (const auto& sub : group.sub_clusters) {
            float sim = cosineSimilarity(q_vec, sub.centroid_vec);
            if (sim > max_sub_sim) {
                max_sub_sim = sim;
                best_cluster_name = sub.full_cluster_name;
            }
        }

        scored_intents.push_back({
            group.name,
            std::max(0.0f, max_sub_sim),
            group.threshold,
            best_cluster_name
        });
    }

    if (scored_intents.empty()) return result;

    // Sort descending by score
    std::sort(scored_intents.begin(), scored_intents.end(), [](const ScoredIntent& a, const ScoredIntent& b) {
        return a.score > b.score;
    });

    const auto& top = scored_intents[0];
    float s1 = top.score;
    float s2 = (scored_intents.size() > 1) ? scored_intents[1].score : 0.0f;
    std::string s2_name = (scored_intents.size() > 1) ? scored_intents[1].intent_name : "";

    result.route_name = top.intent_name;
    result.confidence = s1;
    result.matched_sub_cluster = top.best_sub_cluster;
    result.runner_up_confidence = s2;
    result.runner_up_route = s2_name;

    // Confidence threshold check
    if (s1 < top.threshold) {
        result.is_confident = false;
        result.is_ambiguous = false;
        return result;
    }

    // Refinement 3: Top-2 Margin Ambiguity Guard & Lexer Tie-Breaker
    // Require S1 >= tau AND (S1 - S2) >= 0.04
    float margin = s1 - s2;
    if (margin < 0.04f && scored_intents.size() > 1) {
        std::string resolved_intent;
        bool tie_broken = performLexicalTieBreak(query, top.intent_name, s2_name, resolved_intent);
        if (tie_broken) {
            result.route_name = resolved_intent;
            result.is_confident = true;
            result.is_ambiguous = false;
        } else {
            result.is_ambiguous = true;
            result.is_confident = false;
            result.route_name = "CLARIFICATION";
        }
    } else {
        result.is_confident = true;
        result.is_ambiguous = false;
    }

    return result;
}

RouteMatch SemanticRouter::routeDense(const std::vector<float>& query_embedding) const {
    RouteMatch result;
    if (m_dense_centroids.empty() || query_embedding.empty()) return result;

    std::vector<float> q_norm = query_embedding;
    l2Normalize(q_norm);

    // Group by intent and max-pool across registers
    std::unordered_map<std::string, std::pair<float, std::string>> intent_scores;

    for (const auto& d : m_dense_centroids) {
        float sim = dotProductCosine(q_norm, d.centroid);
        auto it = intent_scores.find(d.intent_name);
        if (it == intent_scores.end() || sim > it->second.first) {
            intent_scores[d.intent_name] = {sim, d.full_cluster_name};
        }
    }

    struct DenseScored {
        std::string name;
        float score = 0.0f;
        std::string cluster;
    };
    std::vector<DenseScored> scored;
    for (const auto& [name, p] : intent_scores) {
        scored.push_back({name, p.first, p.second});
    }
    std::sort(scored.begin(), scored.end(), [](const DenseScored& a, const DenseScored& b) {
        return a.score > b.score;
    });

    if (scored.empty()) return result;

    float s1 = scored[0].score;
    float s2 = (scored.size() > 1) ? scored[1].score : 0.0f;
    std::string s2_name = (scored.size() > 1) ? scored[1].name : "";

    result.route_name = scored[0].name;
    result.confidence = s1;
    result.matched_sub_cluster = scored[0].cluster;
    result.runner_up_confidence = s2;
    result.runner_up_route = s2_name;

    // Threshold for dense 768-dim embedding is tau = 0.72
    constexpr float TAU_DENSE = 0.72f;
    if (s1 < TAU_DENSE) {
        result.is_confident = false;
        result.is_ambiguous = false;
        return result;
    }

    if ((s1 - s2) < 0.04f && scored.size() > 1) {
        std::string resolved;
        bool tie_broken = performLexicalTieBreak("", scored[0].name, s2_name, resolved);
        if (tie_broken) {
            result.route_name = resolved;
            result.is_confident = true;
            result.is_ambiguous = false;
        } else {
            result.is_ambiguous = true;
            result.is_confident = false;
            result.route_name = "CLARIFICATION";
        }
    } else {
        result.is_confident = true;
        result.is_ambiguous = false;
    }

    return result;
}

void SemanticRouter::initializeDefaultRoutes() {
    m_sub_clusters = getCuratedIntentClusters();
    m_legacy_routes.clear();
    buildIndex();
}

} // namespace Ronin::Kernel::Intent
