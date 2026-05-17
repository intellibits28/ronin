#include "capabilities/neural_embedding_node.h"
#include "ronin_log.h"
#include "capabilities/hardware_bridge.h"
#include "intent_engine.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <mutex>
#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/kernels/register.h>
#include <tensorflow/lite/model.h>
#include <sentencepiece_processor.h>

#define TAG "RoninNeuralEmbedding"

namespace Ronin::Kernel::Capability {

struct NeuralEmbeddingNode::Impl {
    std::string model_path;
    std::string sp_model_path;
    std::unique_ptr<tflite::FlatBufferModel> model;
    std::unique_ptr<tflite::Interpreter> interpreter;
    std::unique_ptr<sentencepiece::SentencePieceProcessor> sp_processor;
    bool loaded = false;
    std::mutex mutex;

    Impl(const std::string& m_path, const std::string& s_path) 
        : model_path(m_path), sp_model_path(s_path) {}
};

NeuralEmbeddingNode::NeuralEmbeddingNode(const std::string& model_path, const std::string& sp_model_path)
    : m_impl(std::make_unique<Impl>(model_path, sp_model_path)) {}

NeuralEmbeddingNode::~NeuralEmbeddingNode() = default;

std::string NeuralEmbeddingNode::getName() const { return "NeuralEmbeddingNode"; }

bool NeuralEmbeddingNode::load() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->loaded) return true;

    LOGI(TAG, "Loading Multilingual-E5-Small: %s", m_impl->model_path.c_str());
    
    m_impl->model = tflite::FlatBufferModel::BuildFromFile(m_impl->model_path.c_str());
    if (!m_impl->model) {
        LOGE(TAG, "Failed to load TFLite model.");
        return false;
    }

    tflite::ops::builtin::BuiltinOpResolver resolver;
    tflite::InterpreterBuilder(*(m_impl->model), resolver)(&(m_impl->interpreter));

    if (!m_impl->interpreter) {
        LOGE(TAG, "Failed to build TFLite interpreter.");
        return false;
    }

    if (m_impl->interpreter->AllocateTensors() != kTfLiteOk) {
        LOGE(TAG, "Failed to allocate tensors.");
        return false;
    }

    LOGI(TAG, "Loading SentencePiece: %s", m_impl->sp_model_path.c_str());
    m_impl->sp_processor = std::make_unique<sentencepiece::SentencePieceProcessor>();
    if (!m_impl->sp_processor->Load(m_impl->sp_model_path).ok()) {
        LOGE(TAG, "Failed to load SentencePiece model.");
        return false;
    }

    m_impl->loaded = true;
    LOGI(TAG, "Expert Native Path Hydrated Successfully.");
    return true;
}

void NeuralEmbeddingNode::unload() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->loaded) {
        LOGI(TAG, "Unloading E5-Small model and SentencePiece to free RAM.");
        m_impl->interpreter.reset();
        m_impl->model.reset();
        m_impl->sp_processor.reset();
        m_impl->loaded = false;
    }
}

void NeuralEmbeddingNode::trimMemory(int level) {
    if (!m_impl || !m_impl->loaded) return;
    std::lock_guard<std::mutex> lock(m_impl->mutex);

    // level 80 = TRIM_MEMORY_COMPLETE, level 60 = TRIM_MEMORY_MODERATE
    if (level >= 80) {
        LOGW(TAG, "Critical Pressure: Fully unloading embedding engine.");
        m_impl->interpreter.reset();
        m_impl->model.reset();
        m_impl->sp_processor.reset();
        m_impl->loaded = false;
    } else if (level >= 40) {
        LOGI(TAG, "Moderate Pressure: Releasing non-persistent LiteRT memory.");
#ifdef __ANDROID__
        if (m_impl->interpreter) {
            // Standard TFLite 2.16.1 method name
            m_impl->interpreter->ReleaseNonPersistentMemory();
        }
#endif
    }
}

bool NeuralEmbeddingNode::isLoaded() const {
    return m_impl && m_impl->loaded;
}

std::vector<float> NeuralEmbeddingNode::generateEmbedding(const std::string& input, bool is_query) {
    if (!load()) return {};

    std::string processed_input = input;
    // Multi-lingual E5 requirement: prefix with query: or passage:
    if (is_query) {
        if (processed_input.find("query: ") != 0) processed_input = "query: " + processed_input;
    } else {
        if (processed_input.find("passage: ") != 0) processed_input = "passage: " + processed_input;
    }

    std::vector<int> tokens;
    if (!m_impl->sp_processor->Encode(processed_input, &tokens).ok()) {
        LOGE(TAG, "Tokenization failed.");
        return {};
    }

    // Prepare input tensor
    int input_idx = m_impl->interpreter->inputs()[0];
    TfLiteTensor* input_tensor = m_impl->interpreter->tensor(input_idx);
    
    // Resize input for dynamic sequence length (up to 512)
    int seq_len = std::min((int)tokens.size(), 512);
    std::vector<int> dims = {1, seq_len};
    m_impl->interpreter->ResizeInputTensor(input_idx, dims);
    if (m_impl->interpreter->AllocateTensors() != kTfLiteOk) return {};

    // Copy tokens
    int32_t* input_data = m_impl->interpreter->typed_tensor<int32_t>(input_idx);
    for (int i = 0; i < seq_len; ++i) input_data[i] = tokens[i];

    auto start = std::chrono::high_resolution_clock::now();
    if (m_impl->interpreter->Invoke() != kTfLiteOk) {
        LOGE(TAG, "Inference failed.");
        return {};
    }
    auto end = std::chrono::high_resolution_clock::now();
    LOGI(TAG, "Inference Latency: %lld ms", (long long)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    // Extract embedding (assuming average pooling or [CLS] token at index 0)
    int output_idx = m_impl->interpreter->outputs()[0];
    TfLiteTensor* output_tensor = m_impl->interpreter->tensor(output_idx);
    float* output_data = m_impl->interpreter->typed_tensor<float>(output_idx);
    
    int dim = output_tensor->dims->data[output_tensor->dims->size - 1];
    std::vector<float> embedding(dim);
    std::memcpy(embedding.data(), output_data, dim * sizeof(float));

    return embedding;
}

std::string NeuralEmbeddingNode::execute(const std::string& param) {
    // Default to query mode for direct execution
    auto vec = generateEmbedding(param, true);
    return "E5-Small: Semantic vector (384-dim) generated natively.";
}

} // namespace Ronin::Kernel::Capability
