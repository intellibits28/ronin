#pragma once

#include <string>
#include <vector>
#include <memory>
#include "base_skill.h"

namespace Ronin::Kernel::Capability {

class NeuralEmbeddingNode : public BaseSkill {
public:
    NeuralEmbeddingNode();
    NeuralEmbeddingNode(const std::string& model_path);
    ~NeuralEmbeddingNode();

    // BaseSkill Implementation
    std::string getName() const override { return "NeuralEmbeddingNode"; }
    std::string execute(const std::string& param) override;

    /**
     * Runs inference on the input text to produce a 384-dim semantic embedding.
     */
    std::vector<float> generateEmbedding(const std::string& input);

    /**
     * Returns true if the ONNX session was successfully initialized.
     */
    bool isLoaded() const;

private:
    // PIMPL or opaque pointer to avoid exposing ORT headers in this public header
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Ronin::Kernel::Capability
