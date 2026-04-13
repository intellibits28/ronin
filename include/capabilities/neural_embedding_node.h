#pragma once

#include <string>
#include <vector>
#include <memory>

namespace Ronin::Kernel::Capability {

class NeuralEmbeddingNode {
public:
    NeuralEmbeddingNode(const std::string& model_path);
    ~NeuralEmbeddingNode();

    /**
     * Runs inference on the input text to produce a 384-dim semantic embedding.
     */
    std::vector<float> execute(const std::string& input);

private:
    // PIMPL or opaque pointer to avoid exposing ORT headers in this public header
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Ronin::Kernel::Capability
