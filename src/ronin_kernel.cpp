#include "ronin_kernel.hpp"
#include "ronin_log.h"
#include <cstdio>

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
  lastIntent_ = state_.currentIntent;

  // Tier 3: Adaptive Reasoning Fallback
  if (state_.currentIntent.confidence < 0.6f) {
      std::string original_input(input.data, input.length);
      LOGI(TAG, "> Tier 3: Adaptive Fallback for: '%s'", original_input.c_str());
      
      // v6.0: Default to AGENT_PLAN for low confidence to explore paths
      state_.currentIntent.id = 1;
      state_.currentIntent.category = IntentCategory::AGENT_PLAN;
      state_.currentIntent.confidence = 0.6f; 
      state_.currentIntent.intent_param = true;
  }

  LOGI(TAG, "Heartbeat start: Intent ID %u, Category %u (Conf: %.2f) [v6.0-ADAPTIVE]",
       state_.currentIntent.id, static_cast<uint32_t>(state_.currentIntent.category), state_.currentIntent.confidence);

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

void RoninKernel::shutdown() {
    LOGI(TAG, "Sovereign Control Mode: Initiating Atomic Shutdown Sequence.");
    if (registry_.shutdownProcessor) {
        registry_.shutdownProcessor();
    }
    contextStore_.clear();
    LOGI(TAG, "Kernel Shutdown Complete. Resources released.");
}

} // namespace Ronin::Kernel
