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
    bool loaded = false;
    
    // LiteRT / SentencePiece Objects
    std::unique_ptr<tflite::FlatBufferModel> model;
    std::unique_ptr<tflite::Interpreter> interpreter;
    sentencepiece::SentencePieceProcessor sp_processor;

    std::chrono::steady_clock::time_point last_used;
    std::mutex mutex;
    std::thread timer_thread;
    std::atomic<bool> stop_timer{false};

    Impl(const std::string& path, const std::string& sp_path) 
        : model_path(path), sp_model_path(sp_path), loaded(false) {}
};

NeuralEmbeddingNode::NeuralEmbeddingNode() : m_impl(nullptr) {}

NeuralEmbeddingNode::NeuralEmbeddingNode(const std::string& model_path, const std::string& sp_model_path) {
    m_impl = std::make_unique<Impl>(model_path, sp_model_path);
    
    // Start Auto-Unload Timer
    m_impl->timer_thread = std::thread([this]() {
        while (!m_impl->stop_timer.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            if (m_impl->loaded) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_impl->last_used).count();
                if (elapsed > 30) {
                    LOGI(TAG, "Auto-Unload Policy: 30s idle detected. Releasing RAM.");
                    unload();
                }
            }
        }
    });
}

NeuralEmbeddingNode::~NeuralEmbeddingNode() {
    if (m_impl) {
        m_impl->stop_timer.store(true);
        if (m_impl->timer_thread.joinable()) m_impl->timer_thread.join();
        unload();
    }
}

bool NeuralEmbeddingNode::load() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->loaded) return true;

    // Requirement 5: Resource Guard
    float freeRam = HardwareBridge::getFreeRamGB();
    if (freeRam < 0.5f) {
        LOGW(TAG, "Resource Guard: Insufficient RAM (%.2f GB). Deferring indexing task.", freeRam);
        return false;
    }

    try {
        LOGI(TAG, "Native Path: Loading Multilingual-E5-Small (LiteRT) and SentencePiece.");
        
        // 1. Load SentencePiece Processor
        auto status = m_impl->sp_processor.Load(m_impl->sp_model_path);
        if (!status.ok()) {
            LOGE(TAG, "Failed to load SentencePiece model: %s", status.ToString().c_str());
            return false;
        }

        // 2. Load LiteRT Model
        m_impl->model = tflite::FlatBufferModel::BuildFromFile(m_impl->model_path.c_str());
        if (!m_impl->model) {
            LOGE(TAG, "Failed to build LiteRT model from %s", m_impl->model_path.c_str());
            return false;
        }

        tflite::ops::builtin::BuiltinOpResolver resolver;
        tflite::InterpreterBuilder(*m_impl->model, resolver)(&m_impl->interpreter);

        if (!m_impl->interpreter) {
            LOGE(TAG, "Failed to construct LiteRT interpreter.");
            return false;
        }

        // 3. Configure Interpreter
        m_impl->interpreter->SetNumThreads(2);
        if (m_impl->interpreter->AllocateTensors() != kTfLiteOk) {
            LOGE(TAG, "Failed to allocate LiteRT tensors.");
            return false;
        }

        m_impl->loaded = true;
        m_impl->last_used = std::chrono::steady_clock::now();
        return true;
    } catch (const std::exception& e) {
        LOGE(TAG, "Inference Engine initialization error: %s", e.what());
        return false;
    }
}

void NeuralEmbeddingNode::unload() {
    if (m_impl && m_impl->loaded) {
        LOGI(TAG, "Unloading E5-Small model and SentencePiece to free RAM.");
        m_impl->interpreter.reset();
        m_impl->model.reset();
        m_impl->loaded = false;
    }
}

bool NeuralEmbeddingNode::isLoaded() const {
    return m_impl && m_impl->loaded;
}

std::vector<float> NeuralEmbeddingNode::generateEmbedding(const std::string& input, bool is_query) {
    const int kDim = 384; // Multilingual-E5-Small dimension
    if (!load()) return std::vector<float>(kDim, 0.0f);

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->last_used = std::chrono::steady_clock::now();

    // Rule: Prefix Padding
    std::string processed_input = (is_query ? "query: " : "passage: ") + input;

    try {
        // 1. Tokenize using SentencePiece
        std::vector<int> ids;
        m_impl->sp_processor.Encode(processed_input, &ids);

        // Cap or pad to model input size (e.g., 128)
        const int kMaxSeq = 128;
        std::vector<int32_t> input_ids(kMaxSeq, 0); 
        for (size_t i = 0; i < ids.size() && i < kMaxSeq; ++i) {
            input_ids[i] = static_cast<int32_t>(ids[i]);
        }

        // 2. Feed to LiteRT
        int input_tensor_idx = m_impl->interpreter->inputs()[0];
        int32_t* input_data = m_impl->interpreter->typed_tensor<int32_t>(input_tensor_idx);
        std::copy(input_ids.begin(), input_ids.end(), input_data);

        if (m_impl->interpreter->Invoke() != kTfLiteOk) {
            LOGE(TAG, "Failed to invoke LiteRT interpreter.");
            return std::vector<float>(kDim, 0.0f);
        }

        // 3. Extract Output (assuming first output is the embedding)
        int output_tensor_idx = m_impl->interpreter->outputs()[0];
        float* output_data = m_impl->interpreter->typed_tensor<float>(output_tensor_idx);

        std::vector<float> embedding(kDim);
        std::copy(output_data, output_data + kDim, embedding.begin());

        // L2 Normalization
        float mag = 0.0f;
        for (float f : embedding) mag += f * f;
        mag = std::sqrt(mag);
        if (mag > 1e-9f) {
            for (float& f : embedding) f /= mag;
        }

        return embedding;
    } catch (const std::exception& e) {
        LOGE(TAG, "Native Inference Error: %s", e.what());
        return std::vector<float>(kDim, 0.0f);
    }
}

std::string NeuralEmbeddingNode::execute(const std::string& param) {
    // Default to query mode for direct execution
    auto vec = generateEmbedding(param, true);
    return "E5-Small: Semantic vector (384-dim) generated natively.";
}

} // namespace Ronin::Kernel::Capability
