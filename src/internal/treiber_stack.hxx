#pragma once

#include <atomic>

namespace quiesce::intrusive {

// This implementation is currently prone to the ABA problem
template <typename T, T *T::*Next,
          template <typename> typename Atom = std::atomic>
class TreiberStack {
public:
  void Push(T *node) {
    T *oldHead = head.load(std::memory_order_relaxed);
    do {
      node->*Next = oldHead;
    } while (!head.compare_exchange_weak(
        oldHead, node, std::memory_order_release, std::memory_order_relaxed));
  }

  T *Pop() {
    T *oldHead = head.load(std::memory_order_acquire);
    while (oldHead) {
      T *next = oldHead->*Next;
      if (head.compare_exchange_weak(oldHead, next, std::memory_order_release,
                                     std::memory_order_relaxed)) {
        return oldHead;
      }
    }
    return nullptr;
  }

private:
  Atom<T *> head{nullptr};
};

} // namespace quiesce