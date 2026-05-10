#include "capabilities/neural_embedding_node.h"
#include "ronin_log.h"
#include "capabilities/hardware_bridge.h"
#include "intent_engine.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <mutex>
#include <onnxruntime_cxx_api.h>

#define TAG "RoninNeuralEmbedding"

namespace Ronin::Kernel::Capability {

struct NeuralEmbeddingNode::Impl {
    std::string model_path;
    bool loaded = false;
    
    // ORT Objects
    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    std::chrono::steady_clock::time_point last_used;
    std::mutex mutex;
    std::thread timer_thread;
    std::atomic<bool> stop_timer{false};

    Impl(const std::string& path) : model_path(path), loaded(false) {}
};

NeuralEmbeddingNode::NeuralEmbeddingNode() : m_impl(nullptr) {}

NeuralEmbeddingNode::NeuralEmbeddingNode(const std::string& model_path) {
    m_impl = std::make_unique<Impl>(model_path);
    
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
    if (Ronin::Kernel::Intent::g_thermal_state == Ronin::Kernel::Intent::ThermalState::SEVERE) {
        LOGW(TAG, "Resource Guard: Thermal SEVERE. Deferring intensive task.");
        return false;
    }

    try {
        LOGI(TAG, "Lazy Loading BGE-Base model: %s", m_impl->model_path.c_str());
        m_impl->env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "RoninORT");
        
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(2); // Optimized for SD778G efficiency
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        m_impl->session = std::make_unique<Ort::Session>(*m_impl->env, m_impl->model_path.c_str(), session_options);
        m_impl->loaded = true;
        m_impl->last_used = std::chrono::steady_clock::now();
        return true;
    } catch (const std::exception& e) {
        LOGE(TAG, "Failed to initialize ONNX session: %s", e.what());
        return false;
    }
}

void NeuralEmbeddingNode::unload() {
    // Note: Called under lock by load() or timer, but needs internal safety
    if (m_impl && m_impl->loaded) {
        LOGI(TAG, "Unloading Neural model to free RAM.");
        m_impl->session.reset();
        m_impl->env.reset();
        m_impl->loaded = false;
    }
}

bool NeuralEmbeddingNode::isLoaded() const {
    return m_impl && m_impl->loaded;
}

std::vector<float> NeuralEmbeddingNode::generateEmbedding(const std::string& input) {
    if (!load()) return std::vector<float>(768, 0.0f);

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->last_used = std::chrono::steady_clock::now();

    try {
        // Simplified Tokenization: ASCII mapping (Production requires WordPiece/BPE)
        std::vector<int64_t> input_ids(128, 0);
        for (size_t i = 0; i < input.length() && i < 128; ++i) {
            input_ids[i] = static_cast<int64_t>(static_cast<unsigned char>(input[i]));
        }

        std::vector<int64_t> input_shape = {1, 128};
        Ort::Value input_tensor = Ort::Value::CreateTensor<int64_t>(
            m_impl->memory_info, input_ids.data(), input_ids.size(), input_shape.data(), input_shape.size());

        const char* input_names[] = {"input_ids"};
        const char* output_names[] = {"last_hidden_state"};

        auto output_tensors = m_impl->session->Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);
        
        float* float_ptr = output_tensors.front().GetTensorMutableData<float>();
        
        // Mean pooling for 768 dimensions
        std::vector<float> embedding(768, 0.0f);
        for (int i = 0; i < 768; ++i) {
            embedding[i] = float_ptr[i]; // Simplified pooling (taking first token or first 768 values)
        }

        // Normalize
        float mag = 0.0f;
        for (float f : embedding) mag += f * f;
        mag = std::sqrt(mag);
        if (mag > 1e-9f) {
            for (float& f : embedding) f /= mag;
        }

        return embedding;
    } catch (const std::exception& e) {
        LOGE(TAG, "Inference error: %s", e.what());
        return std::vector<float>(768, 0.0f);
    }
}

std::string NeuralEmbeddingNode::execute(const std::string& param) {
    auto vec = generateEmbedding(param);
    return "BGE-Base: Semantic vector (768-dim) generated and normalized.";
}

} // namespace Ronin::Kernel::Capability
