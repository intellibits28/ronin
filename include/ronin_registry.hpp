#ifndef RONIN_REGISTRY_HPP
#define RONIN_REGISTRY_HPP

#include "ronin_types.hpp"

namespace Ronin::Kernel {

/**
 * Functional interface for intent processing.
 * Replaces the virtual-based IntentEngine.
 */
using IntentHandler = CognitiveIntent (*)(const Input &);

/**
 * Functional interface for node execution.
 * Replaces the virtual-based GraphExecutor.
 */
using ExecHandler = Result (*)(uint32_t nodeId, const CognitiveState &);

/**
 * Functional interface for system shutdown.
 */
using ShutdownHandler = void (*)();

/**
 * Static registry for kernel dispatch.
 * Initialized at compile-time to minimize runtime overhead.
 */
struct HandlerRegistry {
  IntentHandler intentProcessor;
  ExecHandler execProcessor;
  ShutdownHandler shutdownProcessor;
};

} // namespace Ronin::Kernel

#endif // RONIN_REGISTRY_HPP
