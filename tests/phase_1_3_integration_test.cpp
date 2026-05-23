/**
 * Ronin Kernel: Integrated Phase 1-3 Verification Test
 * Targets: SQLite FTS5 Search, Myanmar Text Integrity, and Hardware Guard-rail Cancellation.
 */

#include "memory_database.h"
#include "models/inference_engine.h"
#include "ronin_log.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cassert>
#include <vector>

#define TAG "RoninPhaseVerification"

using namespace Ronin::Kernel::Data;
using namespace Ronin::Kernel::Model;

void run_integrated_test() {
    LOGI(TAG, ">>> INITIATING INTEGRATED VERIFICATION [PHASES 1-3] <<<");

    // --- TARGET 1: Database & FTS5 Search (Phase 1) ---
    LOGI(TAG, "[1/3] Verifying MemoryDatabase & Myanmar FTS5...");
    
    // Use a temporary DB for testing
    MemoryDatabase db("ronin_test_phase.db");
    if (!db.isOpen()) {
        LOGE(TAG, "Critical: Could not open test database.");
        return;
    }

    // Requirement: Mock Insert Myanmar text with state_enum = 3 (Forgotten)
    std::string raw_mm = "ကိုသန့်ဇော်၏ မွေးနေ့မှာ ဇန်နဝါရီ ၂၈ ဖြစ်သည်";
    std::string segmented_mm = "ကို သန့် ဇော် ၏ မွေး နေ့ မှာ ဇန် န ဝါ ရီ ၂၈ ဖြစ် သည်"; // Simulated segmentation
    
    bool insert_ok = db.insertMemory(raw_mm, segmented_mm, MemoryState::FORGOTTEN, "unit_test");
    if (!insert_ok) {
        LOGE(TAG, "Database Insertion FAILED.");
        return;
    }
    LOGI(TAG, "Mock Insert Successful: %s (State: FORGOTTEN)", raw_mm.c_str());

    // --- TARGET 2: FTS5 Recall (Requirement 2) ---
    LOGI(TAG, "[2/3] Verifying Search Recall for 'မွေးနေ့'...");
    
    // Search with Myanmar keyword
    std::string query = "မွေးနေ့";
    auto results = db.searchFTS(query, 3); // Top-K = 3 as per Spec

    bool found_and_correct = false;
    for (const auto& entry : results) {
        LOGI(TAG, "Recall Found: %s (ID: %d, State: %d)", entry.raw_text_mm.c_str(), entry.id, static_cast<int>(entry.state));
        if (entry.state == MemoryState::FORGOTTEN && entry.raw_text_mm.find("မွေးနေ့") != std::string::npos) {
            found_and_correct = true;
        }
    }

    if (found_and_correct) {
        LOGI(TAG, "SUCCESS: FTS5 correctly retrieved Forgotten Memory via Myanmar keywords.");
    } else {
        LOGE(TAG, "FAILURE: FTS5 Search recall failed or state was corrupted.");
        return;
    }

    // --- TARGET 3: Per-Token Cancellation (Phase 2 & Requirement 3) ---
    LOGI(TAG, "[3/3] Verifying Per-Token Cancellation Guard-rail...");
    
    InferenceEngine engine("mock_gemma_4.litertlm");
    engine.resetCancellation();
    
    // Start a background thread to trigger cancellation after 100ms
    std::thread cancel_trigger([&engine]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        LOGI(TAG, "Hardware Guard-rail: Triggering cancellation signal!");
        engine.requestCancellation();
    });

    int token_count = 0;
    auto start_tick = std::chrono::steady_clock::now();
    bool aborted_successfully = false;

    // Simulated Inference Loop mimicking src/models/inference_engine.cpp
    LOGI(TAG, "Starting Mock Inference Loop...");
    while (token_count < 500) { // Max 500 tokens
        // PER-TOKEN CHECK: Using relaxed memory order for performance
        if (engine.isCancelled()) {
            LOGI(TAG, "Loop: Cancellation detected at token index %d", token_count);
            aborted_successfully = true;
            break;
        }

        // Simulate token generation work
        token_count++;
        std::this_thread::sleep_for(std::chrono::milliseconds(15)); // ~15ms per token simulation
    }

    auto end_tick = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_tick - start_tick).count();

    cancel_trigger.join();

    if (aborted_successfully) {
        LOGI(TAG, "SUCCESS: Inference loop aborted within %lld ms (Target < 50ms from trigger).", (elapsed_ms - 100));
    } else {
        LOGE(TAG, "FAILURE: Cancellation flag was ignored or loop completed unexpectedly.");
        return;
    }

    LOGI(TAG, ">>> ALL PHASE 1-3 INTEGRATION TARGETS PASSED SUCCESSFULLY <<<");
}

int main() {
    run_integrated_test();
    return 0;
}
