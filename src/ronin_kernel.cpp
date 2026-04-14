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

  // Tier 3: Reasoning Fallback
  if (state_.currentIntent.confidence < 0.6f) {
      std::string original_input(input.data, input.length);
      LOGI(TAG, "> Tier 3: Reasoning Fallback triggered for input: '%s'", original_input.c_str());
      LOGI(TAG, "> Prompt: User input: '%s'. Available tools: Flashlight, GPS, Chat. Which one?", original_input.c_str());
      
      // Force ChatNode (ID 1) for the reasoning engine to handle it
      state_.currentIntent.id = 1;
      state_.currentIntent.confidence = 0.6f; 
  }

  LOGI(TAG, "Heartbeat start: CognitiveIntent ID %u (Confidence: %.2f) [v3.8.3-CONTEXT-AWARE]",
       state_.currentIntent.id, state_.currentIntent.confidence);

  runAutonomousLoop(input);

  LOGI(TAG, "Heartbeat complete after %d iterations.", state_.iterations);

  // Strict State Reset (v3.8.1-STABLE-UI)
  state_.currentIntent.id = 0;
  state_.currentIntent.confidence = 0.0f;
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
      // Clear context after successful action (v3.8.3)
      clearSuggestedSubject();
    } else {
      LOGI(TAG, "> Failure: Node %u failed with status %d", state_.activeNodeId,
           result.statusCode);
    }

    // State Clearing (v3.8.3-CONTEXT-AWARE)
    state_.requiresAction = false; 
    state_.currentIntent.id = 0;
    }


  // Bounded Autonomy Check
  if (state_.iterations >= maxIterations_ && state_.requiresAction) {
    LOGI(TAG, "> Warning: Kernel reached max iterations (%d). Force stopping.",
         maxIterations_);
    state_.requiresAction = false;
  }
}

} // namespace Ronin::Kernel
