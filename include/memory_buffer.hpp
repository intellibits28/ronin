#ifndef MEMORY_BUFFER_HPP
#define MEMORY_BUFFER_HPP

#include <array>
#include <cstddef>

namespace Ronin::Kernel {

/**
 * A fixed-size, stack-allocated circular buffer for recent interaction context.
 * Implements an "overwrite oldest" eviction policy.
 */
template <typename T, size_t N> class CircularBuffer {
public:
  CircularBuffer() : head_(0), count_(0) {}

  /**
   * Pushes an item into the buffer, overwriting the oldest if full.
   */
  void push(const T &item) {
    data_[head_] = item;
    head_ = (head_ + 1) % N;
    if (count_ < N) {
      count_++;
    }
  }

  /**
   * Access an item by index (0 is oldest).
   */
  const T &operator[](size_t index) const {
    return data_[(head_ - count_ + index + N) % N];
  }

  size_t size() const { return count_; }
  size_t capacity() const { return N; }

  void clear() {
    head_ = 0;
    count_ = 0;
  }

private:
  std::array<T, N> data_;
  size_t head_;
  size_t count_;
};

} // namespace Ronin::Kernel

#endif // MEMORY_BUFFER_HPP
