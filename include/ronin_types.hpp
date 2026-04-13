#ifndef RONIN_TYPES_HPP
#define RONIN_TYPES_HPP

#include <cstddef>
#include <cstdint>

namespace Ronin::Kernel {

/**
 * Minimalist input container with a fixed-size buffer to prevent heap
 * fragmentation.
 */
struct Input {
  char data[512];
  size_t length;
};

/**
 * Represent a discrete user intent derived from the reasoning spine.
 */
struct CognitiveIntent {
  uint32_t id;
  float confidence;
};

/**
 * Result of a capability node execution.
 */
struct Result {
  bool success;
  int32_t statusCode;
};

/**
 * Encapsulates the internal state of the kernel for a single tick cycle.
 */
struct CognitiveState {
  CognitiveIntent currentIntent;
  uint32_t activeNodeId;
  bool requiresAction;
  int iterations;
};

} // namespace Ronin::Kernel

#endif // RONIN_TYPES_HPP
