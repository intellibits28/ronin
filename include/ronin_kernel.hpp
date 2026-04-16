#ifndef RONIN_KERNEL_HPP
#define RONIN_KERNEL_HPP

#include "memory_buffer.hpp"
#include "ronin_registry.hpp"
#include "ronin_types.hpp"

namespace Ronin::Kernel {

/**
 * Interface for pre-dispatch security checks.
 * MUST be validated before triggering any hardware tool or capability.
 */
class CapabilityManager {
public:
  virtual ~CapabilityManager() = default;
  virtual bool canExecute(uint32_t nodeId) const = 0;
};

/**
 * The Ronin Kernel orchestrator.
 * Implements bounded autonomy and static dispatch dispatching.
 */
class RoninKernel {
public:
  RoninKernel(const HandlerRegistry &registry, CapabilityManager &capManager);

  /**
   * Main entry point for the kernel heartbeat.
   * Processes a single user input through the autonomous loop.
   */
  void tick(const Input &input);

  // Contextual Subject Management (v3.9)
  void setSuggestedSubject(const std::string& subject) { m_last_suggested_subject = subject; }
  std::string getSuggestedSubject() const { return m_last_suggested_subject; }
  void clearSuggestedSubject() { m_last_suggested_subject.clear(); }

  // Physical Context Injection
  void injectLocation(double lat, double lon);

private:
  const HandlerRegistry &registry_;
  CapabilityManager &capManager_;
  CognitiveState state_;
  CircularBuffer<uint32_t, 128> contextStore_;

  std::string m_last_suggested_subject;
  const int maxIterations_ = 8;

  /**
   * Internal autonomous loop implementation.
   */
  void runAutonomousLoop(const Input &input);
};

} // namespace Ronin::Kernel

#endif // RONIN_KERNEL_HPP
