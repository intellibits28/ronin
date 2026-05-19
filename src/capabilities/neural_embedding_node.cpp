#include "capabilities/neural_embedding_node.h"
#include "ronin_log.h"
#include "capabilities/hardware_bridge.h"
#include "intent_engine.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <mutex>
#include <cstring>
#include <tensorflow/lite/c/c_api.h>
#include <sentencepiece_processor.h>

#define TAG "RoninNeuralEmbedding"

namespace Ronin::Kernel::Capability {

struct NeuralEmbeddingNode::Impl {
    std::string model_path;
    std::string sp_model_path;
    TfLiteModel* model = nullptr;
    TfLiteInterpreter* interpreter = nullptr;
    std::unique_ptr<sentencepiece::SentencePieceProcessor> sp_processor;
    bool loaded = false;
    std::mutex mutex;

    Impl(const std::string& m_path, const std::string& s_path) 
        : model_path(m_path), sp_model_path(s_path) {}

    ~Impl() {
        if (interpreter) TfLiteInterpreterDelete(interpreter);
        if (model) TfLiteModelDelete(model);
    }
};

NeuralEmbeddingNode::NeuralEmbeddingNode()
    : m_impl(std::make_unique<Impl>("", "")) {}

NeuralEmbeddingNode::NeuralEmbeddingNode(const std::string& model_path, const std::string& sp_model_path)
    : m_impl(std::make_unique<Impl>(model_path, sp_model_path)) {}

NeuralEmbeddingNode::~NeuralEmbeddingNode() = default;

std::string NeuralEmbeddingNode::getName() const { return "NeuralEmbeddingNode"; }

bool NeuralEmbeddingNode::load() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->loaded) return true;

#ifdef ANDROID
    LOGI(TAG, "Loading Multilingual-E5-Small (C API): %s", m_impl->model_path.c_str());
    
    m_impl->model = TfLiteModelCreateFromFile(m_impl->model_path.c_str());
    if (!m_impl->model) {
        LOGE(TAG, "Failed to load TFLite model via C API.");
        return false;
    }

    TfLiteInterpreterOptions* options = TfLiteInterpreterOptionsCreate();
    // Optimized for Snapdragon 778G (4 cores for background task)
    TfLiteInterpreterOptionsSetNumThreads(options, 4);

    m_impl->interpreter = TfLiteInterpreterCreate(m_impl->model, options);
    TfLiteInterpreterOptionsDelete(options);

    if (!m_impl->interpreter) {
        LOGE(TAG, "Failed to create TFLite interpreter via C API.");
        return false;
    }

    if (TfLiteInterpreterAllocateTensors(m_impl->interpreter) != kTfLiteOk) {
        LOGE(TAG, "Failed to allocate tensors via C API.");
        return false;
    }

    LOGI(TAG, "Loading SentencePiece: %s", m_impl->sp_model_path.c_str());
    m_impl->sp_processor = std::make_unique<sentencepiece::SentencePieceProcessor>();
    if (!m_impl->sp_processor->Load(m_impl->sp_model_path).ok()) {
        LOGE(TAG, "Failed to load SentencePiece model.");
        return false;
    }
#else
    LOGI(TAG, "Host Build: Using Mock Embedding Engine.");
#endif

    m_impl->loaded = true;
    LOGI(TAG, "Expert Native Path (C API) Hydrated Successfully.");
    return true;
}

void NeuralEmbeddingNode::unload() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->loaded) {
        LOGI(TAG, "Unloading E5-Small model (C API) and SentencePiece.");
#ifdef ANDROID
        if (m_impl->interpreter) {
            TfLiteInterpreterDelete(m_impl->interpreter);
            m_impl->interpreter = nullptr;
        }
        if (m_impl->model) {
            TfLiteModelDelete(m_impl->model);
            m_impl->model = nullptr;
        }
        m_impl->sp_processor.reset();
#endif
        m_impl->loaded = false;
    }
}

void NeuralEmbeddingNode::trimMemory(int level) {
    if (!m_impl || !m_impl->loaded) return;
    std::lock_guard<std::mutex> lock(m_impl->mutex);

    if (level >= 80) { // TRIM_MEMORY_COMPLETE
        LOGW(TAG, "Critical Pressure: Fully unloading embedding engine.");
        unload();
    }
    // Note: C API doesn't expose ReleaseNonPersistentMemory directly in standard c_api.h
    // It's usually handled by the runtime or requires specific extensions.
}

bool NeuralEmbeddingNode::isLoaded() const {
    return m_impl && m_impl->loaded;
}

std::vector<float> NeuralEmbeddingNode::generateEmbedding(const std::string& input, bool is_query) {
    if (!load()) return {};

#ifdef ANDROID
    std::string processed_input = input;
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

    // Phase 9.1: Fixed Metadata Optimization
    // The specific E5 model has a [1, 1] metadata shape and requires explicit resize to [1, 128].
    const int target_seq_len = 128;
    int input_dims[] = {1, target_seq_len};
    int input_count = TfLiteInterpreterGetInputTensorCount(m_impl->interpreter);

    for (int i = 0; i < input_count; ++i) {
        if (TfLiteInterpreterResizeInputTensor(m_impl->interpreter, i, input_dims, 2) != kTfLiteOk) {
            LOGE(TAG, "Failed to resize input tensor %d to [1, 128].", i);
            return {};
        }
    }

    if (TfLiteInterpreterAllocateTensors(m_impl->interpreter) != kTfLiteOk) {
        LOGE(TAG, "Failed to re-allocate tensors after resize.");
        return {};
    }

    // Populate Input Tensors
    for (int i = 0; i < input_count; ++i) {
        TfLiteTensor* tensor = TfLiteInterpreterGetInputTensor(m_impl->interpreter, i);
        int32_t* data = (int32_t*)TfLiteTensorData(tensor);
        
        // Use tensor name or index to determine content
        // 0: input_ids, 1: attention_mask, 2: token_type_ids (standard BERT order)
        for (int j = 0; j < target_seq_len; ++j) {
            if (i == 0) { // input_ids
                data[j] = (j < tokens.size()) ? tokens[j] : 0;
            } else if (i == 1) { // attention_mask
                data[j] = (j < tokens.size()) ? 1 : 0;
            } else { // token_type_ids or others
                data[j] = 0;
            }
        }
    }

    auto start = std::chrono::high_resolution_clock::now();
    if (TfLiteInterpreterInvoke(m_impl->interpreter) != kTfLiteOk) {
        LOGE(TAG, "Inference failed.");
        return {};
    }
    auto end = std::chrono::high_resolution_clock::now();

    // Extract embedding
    const TfLiteTensor* output_tensor = TfLiteInterpreterGetOutputTensor(m_impl->interpreter, 0);
    const float* output_data = (const float*)TfLiteTensorData(output_tensor);
    
    int dim = 1;
    for (int i = 0; i < TfLiteTensorNumDims(output_tensor); ++i) {
        dim *= TfLiteTensorDim(output_tensor, i);
    }
    
    std::vector<float> embedding(dim);
    std::memcpy(embedding.data(), output_data, dim * sizeof(float));

    return embedding;
#else
    // Host build: Return a deterministic mock vector
    return std::vector<float>(384, 0.0f);
#endif
}

std::string NeuralEmbeddingNode::execute(const std::string& param) {
    if (!isLoaded() && !load()) {
        return "Expert Path: Multilingual-E5 model not found. Please use the Setup Wizard to import '.tflite' from storage.";
    }
    auto vec = generateEmbedding(param, true);
    if (vec.empty()) return "Expert Path Error: Inference failed or model corrupted.";
    return "E5-Small: Semantic vector generated natively via LiteRT C API.";
}

} // namespace Ronin::Kernel::Capability
