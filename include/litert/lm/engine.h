#ifndef LITE_RT_LM_ENGINE_H_
#define LITE_RT_LM_ENGINE_H_

#include <string>
#include <vector>
#include <memory>
#include <functional>

/**
 * PHASE 5.0: LiteRT-LM (Production C++ API)
 * Optimized for Gemma 4 (PLE & Shared KV Cache).
 */

namespace absl {
    // Status definitions are shared with MediaPipe GenAI
    class Status {
    public:
        Status() : is_ok(true) {}
        bool ok() const { return is_ok; }
        std::string message() const { return msg; }
        static Status OkStatus() { return Status(); }
    private:
        bool is_ok = true;
        std::string msg = "OK";
    };

    template <typename T>
    class StatusOr {
    public:
        StatusOr() : is_ok(false) {}
        StatusOr(T&& val) : value(std::move(val)), is_ok(true) {}
        bool ok() const { return is_ok; }
        T& operator*() { return value; }
        T* operator->() { return &value; }
        Status status() const { return status_; }
        T release() { return std::move(value); }
    private:
        T value;
        bool is_ok;
        Status status_;
    };
}

namespace litert::lm {

enum class KVCacheType {
    kShared,
    kPrivate
};

struct KVCacheConfig {
    KVCacheType type = KVCacheType::kShared;
    int max_num_sequences = 1;
};

struct EngineConfig {
    std::string model_path;
    bool enable_ple = true; // Per-Layer Embeddings (Gemma 4 Requirement)
    KVCacheConfig kv_cache_config;
    int max_tokens = 2048;
    float temperature = 0.7f;
    int top_k = 40;
};

class LlmEngine {
public:
    virtual ~LlmEngine() = default;

    // Production Symbol (MUST be exported by libllm_inference_engine_jni.so)
    static absl::StatusOr<std::unique_ptr<LlmEngine>> Create(const EngineConfig& config);

    typedef std::function<void(const std::vector<std::string>&, bool)> ProgressCallback;

    virtual absl::Status GenerateResponse(const std::string& prompt, ProgressCallback callback) = 0;
};

} // namespace litert::lm

#endif // LITE_RT_LM_ENGINE_H_
