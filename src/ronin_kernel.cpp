#include "ronin_kernel.hpp"
#include "ronin_log.h"
#include <cstdio>
#include <thread>
#include <chrono>

#define TAG "RoninKernel"

namespace Ronin::Kernel {

RoninKernel::RoninKernel(const HandlerRegistry &registry,
                         CapabilityManager &capManager)
    : registry_(registry), capManager_(capManager) {
  state_ = {};
  contextStore_.clear();
}

void RoninKernel::tick(const Input &input) {
  // Reset state for new heartbeat
  state_.iterations = 0;
  state_.requiresAction = true;

  // Initial Intent Processing via Static Dispatch
  state_.currentIntent = registry_.intentProcessor(input);

  // Tier 3: Reasoning Fallback
  if (state_.currentIntent.confidence < 0.6f) {
      std::string original_input(input.data, input.length);
      LOGI(TAG, "> Tier 3: Reasoning Fallback triggered for input: '%s'", original_input.c_str());
      LOGI(TAG, "> Prompt: User input: '%s'. Available tools: Flashlight, GPS, Chat. Which one?", original_input.c_str());
      
      // Force ChatNode (ID 1) for the reasoning engine to handle it
      state_.currentIntent.id = 1;
      state_.currentIntent.confidence = 0.6f; 
      state_.currentIntent.intent_param = true;
  }

  LOGI(TAG, "Heartbeat start: CognitiveIntent ID %u (Confidence: %.2f) [v3.9.5-STABLE]",
       state_.currentIntent.id, state_.currentIntent.confidence);

  runAutonomousLoop(input);

  LOGI(TAG, "Heartbeat complete after %d iterations.", state_.iterations);

  // Strict State Reset (v3.9.5-STABLE)
  state_.currentIntent.id = 0;
  state_.currentIntent.confidence = 0.0f;
  state_.currentIntent.intent_param = true;
  state_.requiresAction = false;
}

void RoninKernel::runAutonomousLoop(const Input &input) {
  while (state_.requiresAction && state_.iterations < maxIterations_) {
    state_.iterations++;

    // 1. Resolve Plan (Selection logic encapsulated in dispatch or state update)
    // For the prototype, we map intent ID directly to node ID if action required
    state_.activeNodeId = state_.currentIntent.id;

    LOGI(TAG, "> Thinking: Step [%d] - Analyzing Node %u", state_.iterations,
         state_.activeNodeId);

    // 2. Security Gate: Pre-dispatch authorization
    if (!capManager_.canExecute(state_.activeNodeId)) {
      LOGI(TAG, "> SECURITY WARNING: Unauthorized access attempt to Node %u. "
                "Skipping.",
           state_.activeNodeId);
      state_.requiresAction = false;
      break;
    }

    // 3. Execution via Static Dispatch Registry (Sandboxed-style wrapper)
    Result result = {false, -1};
    try {
      if (registry_.execProcessor) {
        result = registry_.execProcessor(state_.activeNodeId, state_);
        
        // --- GPS Synchronization Loop (v3.9.7-RECOVERY) ---
        if (state_.activeNodeId == 5 && result.success) {
            LOGI(TAG, "> GPS: Entering wait-loop for asynchronous location injection...");
            m_is_waiting_for_location = true;
            
            // Wait for injectLocation callback to clear the flag
            int timeout_counter = 0;
            while (m_is_waiting_for_location) {
                // Yield CPU to prevent high memory pressure and LMK panics
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                // Safety break after 15s if injectLocation fails to notify us
                if (++timeout_counter > 150) {
                    LOGW(TAG, "> GPS Error: Kernel-side timeout waiting for JNI injection.");
                    m_is_waiting_for_location = false;
                }
            }
            LOGI(TAG, "> GPS: Wait-loop terminated. Processing injected context.");
        }
      }
    } catch (...) {
      LOGE(TAG, "> FATAL: Exception caught during Node %u execution! "
                "Emergency halt.",
           state_.activeNodeId);
      state_.requiresAction = false;
      break;
    }

    // 4. Context Update (Short-term memory)
    contextStore_.push(state_.activeNodeId);

    // 5. Termination Condition / Planning Update
    // In a full implementation, the result or a new intent check would determine
    // if more actions are needed.
    if (result.success) {
      LOGI(TAG, "> Success: Node %u returned status %d", state_.activeNodeId,
           result.statusCode);
      // Clear context after successful action (v3.9)
      clearSuggestedSubject();
    } else {
      LOGI(TAG, "> Failure: Node %u failed with status %d", state_.activeNodeId,
           result.statusCode);
    }

    // State Clearing (v3.9-SYSTEM-CONTROL-MASTER)
    state_.requiresAction = false; 
    state_.currentIntent.id = 0;
    state_.currentIntent.intent_param = true;
  }


  // Bounded Autonomy Check
  if (state_.iterations >= maxIterations_ && state_.requiresAction) {
    LOGI(TAG, "> Warning: Kernel reached max iterations (%d). Force stopping.",
         maxIterations_);
    state_.requiresAction = false;
  }
}

void RoninKernel::injectLocation(double lat, double lon) {
    LOGI(TAG, ">>> Physical Context Injected: GPS Coordinates [%.6f, %.6f]", lat, lon);
    
    // Accept lat == 0.0 && lon == 0.0 as terminal state (Timeout/Error)
    // Always break the waiting loop when a terminal state is reached
    m_is_waiting_for_location = false;

    // In v3.9.7, we inject this into the suggested subject so subsequent queries 
    // (e.g., "where am I") can use this real data.
    char buffer[128];
    if (lat == 0.0 && lon == 0.0) {
        snprintf(buffer, sizeof(buffer), "GPS_ERROR: Location Unavailable");
    } else {
        snprintf(buffer, sizeof(buffer), "Current Location: %.6f, %.6f", lat, lon);
    }
    setSuggestedSubject(buffer);
}

} // namespace Ronin::Kernel
