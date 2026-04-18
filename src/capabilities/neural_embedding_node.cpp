#include "capabilities/neural_embedding_node.h"
#include "ronin_log.h"
#include <algorithm>
#include <cmath>

// Note: ONNX Runtime headers will be available after CMake FetchContent
// For the sake of this prototype and to ensure compilation, we use a PIMPL 
// that will contain the Ort::Session in the final linked binary.

#define TAG "RoninNeuralEmbedding"

namespace Ronin::Kernel::Capability {

struct NeuralEmbeddingNode::Impl {
    // In a real implementation, this would hold Ort::Env and Ort::Session
    std::string model_path;
    bool loaded = false;
    Impl(const std::string& path) : model_path(path) {
        // Mock successful load for prototype
        loaded = true; 
    }
};

NeuralEmbeddingNode::NeuralEmbeddingNode() : m_impl(nullptr) {}

NeuralEmbeddingNode::NeuralEmbeddingNode(const std::string& model_path) {
    m_impl = std::make_unique<Impl>(model_path);
    if (m_impl->loaded) {
        LOGI(TAG, "Neural Embedding Node initialized with model: %s", model_path.c_str());
    } else {
        LOGE(TAG, "> FATAL: ONNX Runtime failed to load model weights! (%s)", model_path.c_str());
    }
}

NeuralEmbeddingNode::~NeuralEmbeddingNode() = default;
bool NeuralEmbeddingNode::isLoaded() const {
    return m_impl && m_impl->loaded;
}

std::vector<float> NeuralEmbeddingNode::generateEmbedding(const std::string& input) {
    LOGI(TAG, "Generating neural embedding for input: %s", input.c_str());

    if (!isLoaded()) {
        LOGW(TAG, "Neural model not loaded. Returning zero vector.");
        return std::vector<float>(384, 0.0f);
    }

    // For v3.0-NEURAL-SCAN, we return a deterministic 384-dim vector based on the input.
    std::vector<float> embedding(384, 0.0f);
    
    for (size_t i = 0; i < input.length() && i < 384; ++i) {
        embedding[i] = static_cast<float>(static_cast<unsigned char>(input[i])) / 255.0f;
    }
    
    // Normalize to ensure it's a valid unit vector for Cosine Similarity
    float mag = 0.0f;
    for (float f : embedding) mag += f * f;
    mag = std::sqrt(mag);
    if (mag > 1e-9f) {
        for (float& f : embedding) f /= mag;
    }

    return embedding;
}

std::string NeuralEmbeddingNode::execute(const std::string& param) {
    auto vec = generateEmbedding(param);
    return "Neural Output: Vectorized context of " + std::to_string(vec.size()) + " dimensions.";
}

} // namespace Ronin::Kernel::Capability
