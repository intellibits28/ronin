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
  LOGI(TAG, ">>> KERNEL HEARTBEAT START <<<");
  
  observe();
  orient();
  decide(input);
  act();

  LOGI(TAG, ">>> KERNEL HEARTBEAT COMPLETE <<<");
}

void RoninKernel::observe() {
  LOGI(TAG, "[LOOP] 1. OBSERVE: Synchronizing World State...");
  // v13.0: Physical constraints (Battery: %.1f%%, RAM: %.1f MB)", m_world_state.battery_percent, m_world_state.ram_available_mb);
  // This state is pushed from Kotlin via JNI injectWorldState
}

void RoninKernel::orient() {
  LOGI(TAG, "[LOOP] 2. ORIENT: Updating Beliefs & Contextual Buffer...");
  // v10.2.13: Isolation logic
  contextStore_.clear();
  
  // v13.0: Inject world state into belief system if significant
  if (m_world_state.battery_percent < 15.0f) {
      setSuggestedSubject("System Constraint: Low Battery");
  }
}

void RoninKernel::decide(const Input& input) {
  LOGI(TAG, "[LOOP] 3. DECIDE: Intent Decomposition & Goal Selection...");
  state_.currentIntent = registry_.intentProcessor(input);
  lastIntent_ = state_.currentIntent;
}

void RoninKernel::act() {
  LOGI(TAG, "[LOOP] 4. ACT: Speculative Execution & Replay...");
  // In v1.5, execution is handled by AgentScheduler which is triggered by JNI.
  // This method acts as a synchronous fallback or internal maintenance trigger.
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

void RoninKernel::enterSafeMode() {
    LOGE(TAG, "Entering Strict SafeMode.");
    // In a fully decoupled architecture, we would broadcast an event.
    // For now, we rely on the JNI layer to handle global side-effects,
    // but we can clear local state here.
    contextStore_.clear();
    state_.requiresAction = false;
}

} // namespace Ronin::Kernel
