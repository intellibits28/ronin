#ifndef RONIN_REGISTRY_HPP
#define RONIN_REGISTRY_HPP

#include "ronin_types.hpp"

namespace Ronin::Kernel {

/**
 * Functional interface for intent processing.
 * Replaces the virtual-based IntentEngine.
 */
using IntentHandler = Intent (*)(const Input &);

/**
 * Functional interface for node execution.
 * Replaces the virtual-based GraphExecutor.
 */
using ExecHandler = Result (*)(uint32_t nodeId, const CognitiveState &);

/**
 * Static registry for kernel dispatch.
 * Initialized at compile-time to minimize runtime overhead.
 */
struct HandlerRegistry {
  IntentHandler intentProcessor;
  ExecHandler execProcessor;
};

} // namespace Ronin::Kernel

#endif // RONIN_REGISTRY_HPP
