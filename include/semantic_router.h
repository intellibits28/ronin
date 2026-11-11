#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "intent_centroids.h"

namespace Ronin::Kernel::Intent {

struct RouteMatch {
    std::string route_name;          // e.g. "FLASHLIGHT", "VAULT_LOOKUP", "VAULT_UPDATE", etc.
    float confidence = 0.0f;         // 0.0 to 1.0 cosine similarity score
    bool is_confident = false;       // true if confidence >= threshold and not ambiguous
    std::string matched_sub_cluster; // e.g. "VAULT_LOOKUP_MM"
    float runner_up_confidence = 0.0f;// runner-up score S2
    std::string runner_up_route;      // runner-up route name
    bool is_ambiguous = false;        // true if top-2 margin < 0.04 and lexer tie-break failed
};

struct RouteDefinition {
    std::string name;
    std::vector<std::string> utterances; // Bilingual sample phrases defining the route
    float threshold = 0.32f;
};

/**
 * Fast, on-device Semantic Router using multi-centroid sub-clustering (EN, MM, MIX)
 * with strict L2 re-normalization, max-pooling, and top-2 margin ambiguity guard.
 * Operates in microseconds without needing heavyweight LLM inference.
 */
class SemanticRouter {
public:
    SemanticRouter();

    /**
     * Registers a new semantic route with representative exemplar utterances.
     */
    void addRoute(const RouteDefinition& route);

    /**
     * Registers a specific language-register sub-cluster.
     */
    void addSubCluster(const IntentSubCluster& cluster);

    /**
     * Sets a custom confidence threshold for a specific primary intent.
     */
    void setThreshold(const std::string& intent_name, float threshold);

    /**
     * Precomputes unit L2-normalized centroid vectors for all registered routes and sub-clusters.
     */
    void buildIndex();

    /**
     * Classifies a user query to the best-matching semantic route.
     * Evaluates max-pooling across sub-centroids and applies top-2 margin guard with lexical tie-breaker.
     * @param query The raw user query.
     * @return Best matching route and confidence score.
     */
    RouteMatch route(const std::string& query) const;

    /**
     * Dense embedding vector classification (e.g. from LiteRT EmbeddingGemma 2).
     */
    RouteMatch routeDense(const std::vector<float>& query_embedding) const;

    /**
     * Registers dense centroids for an intent.
     */
    void setDenseCentroid(const std::string& intent_name, LanguageRegister reg, const std::vector<float>& centroid);

    /**
     * Initializes default routes using the curated 3-register sub-cluster exemplars.
     */
    void initializeDefaultRoutes();

    /**
     * Deterministic C++ lexical tie-breaker checking for mutation markers vs creation/read markers.
     * Returns true if tie was broken, setting out_resolved_intent.
     */
    static bool performLexicalTieBreak(
        const std::string& query,
        const std::string& top_intent,
        const std::string& runner_up,
        std::string& out_resolved_intent
    );

    struct Feature {
        uint32_t id = 0;
        float weight = 0.0f;
    };

    struct SparseVector {
        std::vector<Feature> features;
        float norm = 0.0f;
    };

    static SparseVector vectorize(const std::string& text);
    static float cosineSimilarity(const SparseVector& a, const SparseVector& b);
    static uint32_t hashFeature(const std::string& feat);

private:
    struct SubCentroidIndex {
        std::string intent_name;
        LanguageRegister reg = LanguageRegister::MIX;
        std::string full_cluster_name; // e.g. "VAULT_UPDATE_MM"
        std::vector<std::string> utterances;
        SparseVector centroid_vec; // Strictly unit L2-normalized
    };

    struct IntentGroup {
        std::string name;
        float threshold = 0.30f;
        std::vector<SubCentroidIndex> sub_clusters;
    };

    struct DenseSubClusterIndex {
        std::string intent_name;
        LanguageRegister reg = LanguageRegister::MIX;
        std::string full_cluster_name;
        std::vector<float> centroid; // Strictly unit L2-normalized (768-dim)
    };

    std::vector<RouteDefinition> m_legacy_routes;
    std::vector<IntentSubCluster> m_sub_clusters;
    std::vector<IntentGroup> m_indexed_groups;
    std::unordered_map<std::string, float> m_custom_thresholds;
    std::vector<DenseSubClusterIndex> m_dense_centroids;
    bool m_indexed = false;
};

} // namespace Ronin::Kernel::Intent
