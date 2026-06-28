#include "dsp/dsp_tools.h"
#include "capabilities/tool_registry.h"
#include "third_party/pffft/pffft.h"
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <nlohmann/json.hpp>
#include "ronin_log.h"

#define TAG "RoninDSPTools"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Ronin::Kernel::DSP {

static bool isPowerOfTwo(int n) {
    return (n > 0) && ((n & (n - 1)) == 0);
}

static int nextPowerOfTwo(int n) {
    if (n <= 1) return 2;
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

// 1. FFT Tool Implementation
static std::string runFFT(const std::string& payload, ToolContext* context) {
    try {
        nlohmann::json jIn = nlohmann::json::parse(payload);
        std::vector<float> input_array;
        float sample_rate = 100.0f; // default

        if (jIn.is_array()) {
            input_array = jIn.get<std::vector<float>>();
        } else {
            if (jIn.contains("array")) {
                input_array = jIn["array"].get<std::vector<float>>();
            }
            if (jIn.contains("sample_rate")) {
                sample_rate = jIn["sample_rate"].get<float>();
            }
        }

        if (input_array.empty()) {
            return "Error: Input array is empty.";
        }

        int orig_size = input_array.size();
        int n_fft = isPowerOfTwo(orig_size) ? orig_size : nextPowerOfTwo(orig_size);
        if (n_fft < 32) {
            n_fft = 32;
        }
        if (n_fft > 16384) {
            return "Error: FFT size too large (max 16384).";
        }

        // Pad input with zeros if not power of 2
        if (orig_size < n_fft) {
            input_array.resize(n_fft, 0.0f);
        }

        PFFFT_Setup* setup = pffft_new_setup(n_fft, PFFFT_REAL);
        if (!setup) {
            return "Error: Failed to initialize PFFFT.";
        }

        float* work = (float*)pffft_aligned_malloc(n_fft * sizeof(float));
        float* output = (float*)pffft_aligned_malloc(n_fft * sizeof(float));

        pffft_transform_ordered(setup, input_array.data(), output, work, PFFFT_FORWARD);

        std::vector<float> magnitudes;
        std::vector<float> frequencies;
        magnitudes.reserve(n_fft / 2);
        frequencies.reserve(n_fft / 2);

        for (int i = 0; i < n_fft / 2; ++i) {
            float r = output[2 * i];
            float im = (i == 0) ? 0.0f : output[2 * i + 1];
            float mag = std::sqrt(r * r + im * im);
            magnitudes.push_back(mag);
            frequencies.push_back((float)i * sample_rate / n_fft);
        }

        pffft_aligned_free(work);
        pffft_aligned_free(output);
        pffft_destroy_setup(setup);

        nlohmann::json jOut;
        jOut["frequencies"] = frequencies;
        jOut["magnitudes"] = magnitudes;
        return jOut.dump();
    } catch (const std::exception& e) {
        return std::string("Error: ") + e.what();
    }
}

// 2. Filter Tool Implementation (Lowpass)
static std::string runLowpass(const std::string& payload, ToolContext* context) {
    try {
        nlohmann::json jIn = nlohmann::json::parse(payload);
        std::vector<float> input_array = jIn["array"].get<std::vector<float>>();
        float fc = jIn.value("cutoff_hz", 10.0f);
        float fs = jIn.value("sample_rate", 100.0f);

        if (input_array.empty()) return "Error: Input array is empty.";

        // 2nd order Butterworth filter design
        float omega = std::tan(M_PI * fc / fs);
        float root2 = std::sqrt(2.0f);
        float norm = 1.0f / (1.0f + root2 * omega + omega * omega);

        float b0 = omega * omega * norm;
        float b1 = 2.0f * b0;
        float b2 = b0;
        float a1 = 2.0f * (omega * omega - 1.0f) * norm;
        float a2 = (1.0f - root2 * omega + omega * omega) * norm;

        std::vector<float> filtered(input_array.size());
        float z1 = 0, z2 = 0, v1 = 0, v2 = 0;

        for (size_t i = 0; i < input_array.size(); ++i) {
            float x = input_array[i];
            float y = b0 * x + b1 * z1 + b2 * z2 - a1 * v1 - a2 * v2;
            z2 = z1; z1 = x;
            v2 = v1; v1 = y;
            filtered[i] = y;
        }

        nlohmann::json jOut;
        jOut["filtered"] = filtered;
        return jOut.dump();
    } catch (const std::exception& e) {
        return std::string("Error: ") + e.what();
    }
}

// 3. Peak Detection Tool
static std::string runDetectPeaks(const std::string& payload, ToolContext* context) {
    try {
        nlohmann::json jIn = nlohmann::json::parse(payload);
        std::vector<float> input_array;
        float threshold = 0.5f;

        if (jIn.is_array()) {
            input_array = jIn.get<std::vector<float>>();
        } else {
            input_array = jIn["array"].get<std::vector<float>>();
            threshold = jIn.value("threshold", 0.5f);
        }

        if (input_array.size() < 3) return "Error: Array too small.";

        std::vector<int> peak_indices;
        for (size_t i = 1; i < input_array.size() - 1; ++i) {
            if (input_array[i] > input_array[i-1] && input_array[i] > input_array[i+1] && input_array[i] >= threshold) {
                peak_indices.push_back(i);
            }
        }

        nlohmann::json jOut;
        jOut["peaks"] = peak_indices;
        jOut["count"] = peak_indices.size();
        return jOut.dump();
    } catch (const std::exception& e) {
        return std::string("Error: ") + e.what();
    }
}

// 4. Zero Crossing Rate Tool
static std::string runZeroCrossing(const std::string& payload, ToolContext* context) {
    try {
        nlohmann::json jIn = nlohmann::json::parse(payload);
        std::vector<float> input_array;
        if (jIn.is_array()) {
            input_array = jIn.get<std::vector<float>>();
        } else {
            input_array = jIn["array"].get<std::vector<float>>();
        }

        if (input_array.empty()) return "Error: Array empty.";

        int crossings = 0;
        for (size_t i = 1; i < input_array.size(); ++i) {
            if ((input_array[i] >= 0.0f && input_array[i-1] < 0.0f) || 
                (input_array[i] < 0.0f && input_array[i-1] >= 0.0f)) {
                crossings++;
            }
        }

        float zcr = (float)crossings / input_array.size();

        nlohmann::json jOut;
        jOut["zero_crossing_rate"] = zcr;
        jOut["crossings"] = crossings;
        return jOut.dump();
    } catch (const std::exception& e) {
        return std::string("Error: ") + e.what();
    }
}

// 5. RMS Tool
static std::string runRMS(const std::string& payload, ToolContext* context) {
    try {
        nlohmann::json jIn = nlohmann::json::parse(payload);
        std::vector<float> input_array;
        if (jIn.is_array()) {
            input_array = jIn.get<std::vector<float>>();
        } else {
            input_array = jIn["array"].get<std::vector<float>>();
        }

        if (input_array.empty()) return "Error: Array empty.";

        float sum_sq = 0.0f;
        for (float v : input_array) sum_sq += v * v;
        float rms_val = std::sqrt(sum_sq / input_array.size());

        nlohmann::json jOut;
        jOut["rms"] = rms_val;
        return jOut.dump();
    } catch (const std::exception& e) {
        return std::string("Error: ") + e.what();
    }
}

void registerDspTools() {
    auto& registry = Capability::ToolRegistry::getInstance();

    Capability::ToolMetadata fftMeta;
    fftMeta.name = "fft";
    fftMeta.description = "Computes Fast Fourier Transform on float arrays to find frequency spectrum";
    fftMeta.inputs = {"float_array"};
    fftMeta.outputs = {"float_array_frequencies", "float_array_magnitudes"};
    registry.registerTool(fftMeta, runFFT);

    Capability::ToolMetadata lowpassMeta;
    lowpassMeta.name = "lowpass";
    lowpassMeta.description = "Applies 2nd order Butterworth lowpass filter to clean signal noise";
    lowpassMeta.inputs = {"float_array"};
    lowpassMeta.outputs = {"float_array_filtered"};
    registry.registerTool(lowpassMeta, runLowpass);

    Capability::ToolMetadata peaksMeta;
    peaksMeta.name = "detect_peaks";
    peaksMeta.description = "Detects peak indices in a signal array above a given threshold";
    peaksMeta.inputs = {"float_array"};
    peaksMeta.outputs = {"int_array_peak_indices"};
    registry.registerTool(peaksMeta, runDetectPeaks);

    Capability::ToolMetadata zcMeta;
    zcMeta.name = "zero_crossing";
    zcMeta.description = "Calculates zero crossing rate of a float signal array";
    zcMeta.inputs = {"float_array"};
    zcMeta.outputs = {"float_zero_crossing_rate"};
    registry.registerTool(zcMeta, runZeroCrossing);

    Capability::ToolMetadata rmsMeta;
    rmsMeta.name = "rms";
    rmsMeta.description = "Calculates root-mean-square value (signal energy) of a float array";
    rmsMeta.inputs = {"float_array"};
    rmsMeta.outputs = {"float_rms"};
    registry.registerTool(rmsMeta, runRMS);

    LOGI(TAG, "All DSP Tools registered successfully.");
}

} // namespace Ronin::Kernel::DSP
