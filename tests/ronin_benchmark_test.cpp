#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include <string>
#include <iostream>
#include <numeric>
#include <thread>
#include <atomic>
#include "kernel/include/event_bus.h"
#include "kernel/include/execution_context.h"
#include "long_term_memory.h"
#include "capabilities/tool_registry.h"
#include "dsp/dsp_tools.h"
#include <nlohmann/json.hpp>

using namespace Ronin::Kernel::Event;
using namespace Ronin::Kernel::Execution;
using namespace Ronin::Kernel::Memory;
using namespace Ronin::Kernel::Capability;
using namespace Ronin::Kernel::DSP;

class PerformanceRegressionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure clean state before each benchmark
    }
};

TEST_F(PerformanceRegressionTest, EventDispatchLatencyWithinThreshold) {
    auto& bus = EventBus::getInstance();
    std::atomic<bool> received{false};
    std::chrono::high_resolution_clock::time_point send_time;
    std::chrono::high_resolution_clock::time_point receive_time;

    // Subscribe to "default" because string payloads route to "default" topic in EventBus::dispatchLoop
    bus.subscribe("default", [&](const Message&) {
        receive_time = std::chrono::high_resolution_clock::now();
        received.store(true);
    });

    bus.start();

    const int iterations = 100;
    std::vector<double> latencies_us;
    latencies_us.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        received.store(false);
        Message msg;
        msg.sender_id = "bench_actor";
        msg.priority = EventPriority::CRITICAL;
        msg.payload = std::string("ping");

        send_time = std::chrono::high_resolution_clock::now();
        bus.publish(msg, "default");

        int timeout_ms = 100;
        while (!received.load() && timeout_ms-- > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ASSERT_TRUE(received.load()) << "Event dispatch timed out";

        auto dur_us = std::chrono::duration_cast<std::chrono::microseconds>(receive_time - send_time).count();
        latencies_us.push_back(static_cast<double>(dur_us));
    }

    bus.stop();

    double sum = std::accumulate(latencies_us.begin(), latencies_us.end(), 0.0);
    double avg_latency_us = sum / iterations;

    std::cout << "[Benchmark] Average Event Dispatch Latency: " << avg_latency_us << " us (Target: < 1000 us)" << std::endl;
    EXPECT_LT(avg_latency_us, 1000.0) << "Performance Regression: Event dispatch latency exceeded 1ms limit!";
}

TEST_F(PerformanceRegressionTest, BlackboardReadLatencyWithinThreshold) {
    Blackboard board;
    const std::string actor_id = "bench_writer";
    
    for (int i = 0; i < 20; ++i) {
        board.write("key_" + std::to_string(i), BlackboardValue(static_cast<float>(i * 1.5f)), actor_id);
    }

    const int iterations = 10000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        volatile auto val = board.read("key_5");
        (void)val;
    }

    auto end = std::chrono::high_resolution_clock::now();
    double total_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    double avg_read_us = total_us / iterations;

    std::cout << "[Benchmark] Average Blackboard Read Latency: " << avg_read_us << " us (Target: < 50 us)" << std::endl;
    EXPECT_LT(avg_read_us, 50.0) << "Performance Regression: Blackboard read latency exceeded 50us limit!";
}

TEST_F(PerformanceRegressionTest, FFT4096SamplesLatencyWithinThreshold) {
    auto& registry = ToolRegistry::getInstance();
    registerDspTools();

    // Create 4096 float sample array
    std::vector<float> samples(4096);
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] = std::sin(2.0f * 3.14159265f * 440.0f * (i / 44100.0f));
    }
    std::string json_input = nlohmann::json(samples).dump();

    const int iterations = 25;
    std::vector<double> latencies_ms;
    latencies_ms.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        std::string res = registry.execute("fft", json_input);
        auto end = std::chrono::high_resolution_clock::now();
        
        ASSERT_FALSE(res.empty());
        double dur_ms = std::chrono::duration<double, std::milli>(end - start).count();
        latencies_ms.push_back(dur_ms);
    }

    double sum = std::accumulate(latencies_ms.begin(), latencies_ms.end(), 0.0);
    double avg_fft_ms = sum / iterations;

    std::cout << "[Benchmark] Average FFT (4096 samples) Latency: " << avg_fft_ms << " ms (Target: < 50 ms in Debug mode)" << std::endl;
    // In unoptimized debug CI build, JSON formatting 6000+ floats takes up to 40ms. In Release mode it is < 10ms.
    EXPECT_LT(avg_fft_ms, 50.0) << "Performance Regression: FFT latency exceeded debug threshold!";
}

TEST_F(PerformanceRegressionTest, MemoryQueryLatencyWithinThreshold) {
    LongTermMemory ltm(":memory:");

    // Seed 150 notes into FTS database
    for (int i = 0; i < 150; ++i) {
        ltm.storeNote("Note " + std::to_string(i), "Cognitive runtime benchmark memory record containing resonance keyword " + std::to_string(i));
    }

    const int iterations = 50;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        auto results = ltm.searchNotes("resonance");
        ASSERT_FALSE(results.empty());
    }

    auto end = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
    double avg_query_ms = total_ms / iterations;

    std::cout << "[Benchmark] Average Memory Query Latency: " << avg_query_ms << " ms (Target: < 20 ms)" << std::endl;
    EXPECT_LT(avg_query_ms, 20.0) << "Performance Regression: Memory query latency exceeded 20ms limit!";
}
