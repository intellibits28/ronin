#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace Ronin::Kernel::Intent {

struct RouteMatch {
    std::string route_name;   // e.g. "FLASHLIGHT", "PITCH_ANALYSIS", "SHM_VIBRATION", etc.
    float confidence = 0.0f;  // 0.0 to 1.0 cosine similarity score
    bool is_confident = false;// true if confidence >= threshold
};

struct RouteDefinition {
    std::string name;
    std::vector<std::string> utterances; // Bilingual sample phrases defining the route
    float threshold = 0.45f;
};

/**
 * Fast, on-device Semantic Router using subword character n-gram + TF-IDF vectorization.
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
     * Precomputes centroid feature vectors for all registered routes.
     */
    void buildIndex();

    /**
     * Classifies a user query to the best-matching semantic route.
     * @param query The raw user query.
     * @return Best matching route and confidence score.
     */
    RouteMatch route(const std::string& query) const;

    /**
     * Initializes standard Ronin routes (SHM, Guitar Tuner, Hardware, etc.).
     */
    void initializeDefaultRoutes();

private:
    struct Feature {
        uint32_t id = 0;
        float weight = 0.0f;
    };

    struct SparseVector {
        std::vector<Feature> features;
        float norm = 0.0f;
    };

    struct RouteIndex {
        std::string name;
        float threshold = 0.40f;
        std::vector<SparseVector> exemplar_vectors;
    };

    std::vector<RouteDefinition> m_routes;
    std::vector<RouteIndex> m_indexed_routes;
    bool m_indexed = false;

    // Feature hashing / n-gram vectorizer
    SparseVector vectorize(const std::string& text) const;
    static float cosineSimilarity(const SparseVector& a, const SparseVector& b);
    static uint32_t hashFeature(const std::string& feat);
};

} // namespace Ronin::Kernel::Intent
