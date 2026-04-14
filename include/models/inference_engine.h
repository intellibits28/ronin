#pragma once

#include <string>
#include <vector>
#include <memory>
#include "ronin_types.hpp"

namespace Ronin::Kernel::Model {

class InferenceEngine {
public:
    InferenceEngine(const std::string& model_path);
    ~InferenceEngine();

    /**
     * Runs inference on the input text to determine the intent.
     * Returns a CognitiveIntent with the predicted ID and confidence.
     */
    CognitiveIntent predict(const std::string& input);

    /**
     * Returns true if the ONNX session was successfully initialized.
     */
    bool isLoaded() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Ronin::Kernel::Model
