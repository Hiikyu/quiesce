#pragma once

#include <atomic>
#include <cstdint>
#include <cassert>
#include <type_traits>
#include <utility>
#include <iterator>
#include "../include/utils.hxx"

namespace quiesce::intrusive {

template <typename T, std::atomic<uintptr_t> T::*Next> class HarrisMichaelList {
  static_assert(alignof(T) >= alignof(uintptr_t),
                "T must have pointer alignment for tagged pointers");
  static_assert(
      std::is_same<
          std::atomic<uintptr_t>,
          std::remove_reference_t<decltype(std::declval<T>().*Next)>>::value,
      "Next must be a member of type std::atomic<uintptr_t>");

public:
  class Iterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T *;
    using reference = T &;

    Iterator() noexcept : curr_(nullptr) {}

    T &operator*() const noexcept { return *curr_; }
    T *operator->() const noexcept { return curr_; }

    Iterator &operator++() noexcept {
      curr_ = nextLive(curr_);
      return *this;
    }
    Iterator operator++(int) noexcept {
      Iterator t = *this;
      ++*this;
      return t;
    }

    bool operator==(const Iterator &o) const noexcept {
      return curr_ == o.curr_;
    }
    bool operator!=(const Iterator &o) const noexcept {
      return curr_ != o.curr_;
    }

  private:
    friend class HarrisMichaelList;
    explicit Iterator(T *curr) noexcept : curr_(curr) {}
    T *curr_;
  };

  /// Creates an empty linked list.
  HarrisMichaelList() noexcept : head_(0) {}

  HarrisMichaelList(const HarrisMichaelList &ot) = delete;

  HarrisMichaelList &operator=(const HarrisMichaelList &ot) = delete;

  HarrisMichaelList(HarrisMichaelList &&ot) = delete;

  HarrisMichaelList &operator=(HarrisMichaelList &&ot) = delete;

  /// Inserts `node` at the head of the list.
  void PushFront(T *node) noexcept {
    const uintptr_t rawNode = RawPtr(node);
    uintptr_t expected = head_.load(std::memory_order_relaxed);

    for (;;) {
      (node->*Next).store(expected, std::memory_order_relaxed);
      if (head_.compare_exchange_weak(expected, rawNode,
                                      std::memory_order_release,
                                      std::memory_order_relaxed)) {
        return;
      }
    }
  }

  /// Removes and returns the first unmarked element, or nullptr if empty.
  [[nodiscard]] T *PopFront() noexcept {
    uintptr_t expected = head_.load(std::memory_order_acquire);

    while (expected != 0) {
      T *current = Unmarked<T>(expected);
      uintptr_t next = (current->*Next).load(std::memory_order_acquire);

      if (IsMarked(next)) {
        // Already logically deleted by a concurrent Erase(); help unlink
        // and move on — it is not ours to return.
        head_.compare_exchange_weak(expected, UnmarkedRaw(next),
                                    std::memory_order_acq_rel,
                                    std::memory_order_acquire);
        continue;
      }

      // Claim `current` via the same linearization point Erase() uses,
      // so the two operations can never both deliver the same node.
      if ((current->*Next)
              .compare_exchange_weak(next, Mark(next),
                                     std::memory_order_release,
                                     std::memory_order_relaxed)) {
        head_.compare_exchange_strong(expected, UnmarkedRaw(next),
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire);
        return current;
      }

      expected = head_.load(std::memory_order_acquire);
    }
    return nullptr;
  }

  /// Inserts `node` after `after`. If `after` is null, insert is no-op.
  [[nodiscard]] bool InsertAfter(const T *after, T *node) noexcept {
    assert(after != nullptr && "parameter `after` must not be nullptr");

    const uintptr_t rawNode = RawPtr(node);

  retry:
    std::atomic<uintptr_t> *prevNext = &head_;
    uintptr_t currRaw = prevNext->load(std::memory_order_acquire);

    while (currRaw != 0) {
      T *curr = Unmarked<T>(currRaw);
      uintptr_t nextRaw = (curr->*Next).load(std::memory_order_acquire);

      if (IsMarked(nextRaw)) {
        if (tryUnlink(*prevNext, currRaw, UnmarkedRaw(nextRaw))) {
          currRaw = prevNext->load(std::memory_order_acquire);
          continue;
        }
        goto retry;
      }

      if (curr == after) {
        (node->*Next).store(nextRaw, std::memory_order_relaxed);
        if ((curr->*Next)
                .compare_exchange_strong(nextRaw, rawNode,
                                         std::memory_order_release,
                                         std::memory_order_relaxed)) {
          return true;
        }
        goto retry;
      }

      prevNext = &(curr->*Next);
      currRaw = nextRaw;
    }

    // Not Found
    return false;
  }

  [[nodiscard]] bool Erase(T *target) noexcept {
  retry:
    std::atomic<uintptr_t> *prevNext = &head_;
    uintptr_t currRaw = prevNext->load(std::memory_order_acquire);

    while (currRaw != 0) {
      T *curr = Unmarked<T>(currRaw);
      uintptr_t nextRaw = (curr->*Next).load(std::memory_order_acquire);

      if (IsMarked(nextRaw)) {
        if (tryUnlink(*prevNext, currRaw, UnmarkedRaw(nextRaw))) {
          currRaw = prevNext->load(std::memory_order_acquire);
          continue;
        }
        goto retry;
      }

      if (curr == target) {
        uintptr_t expected = nextRaw;
        if ((curr->*Next)
                .compare_exchange_strong(expected, Mark(nextRaw),
                                         std::memory_order_release,
                                         std::memory_order_relaxed)) {

          tryUnlink(*prevNext, currRaw, UnmarkedRaw(nextRaw));

          return true;
        }

        if (IsMarked(expected))
          return false;

        goto retry;
      }

      prevNext = &(curr->*Next);
      currRaw = nextRaw;
    }

    return false;
  }

  /// Erases and returns the node immediately following `prev`, or nullptr if
  /// there is none. O(1) — no traversal from head_. `prev` must currently be
  /// linked into this list; the caller must not free it concurrently (same
  /// reclamation precondition as PopFront/Erase).
  T *EraseAfter(T *prev) noexcept {
    std::atomic<uintptr_t> *prevNext = &(prev->*Next);

    for (;;) {
      uintptr_t currRaw = prevNext->load(std::memory_order_acquire);
      if (IsMarked(currRaw) || currRaw == 0) {
        // `prev` itself was concurrently erased, or nothing follows it.
        return nullptr;
      }

      T *curr = Unmarked<T>(currRaw);
      uintptr_t nextRaw = (curr->*Next).load(std::memory_order_acquire);

      if (IsMarked(nextRaw)) {
        tryUnlink(*prevNext, currRaw, UnmarkedRaw(nextRaw));
        continue; // help unlink, reread what now follows prev
      }

      if ((curr->*Next)
              .compare_exchange_strong(nextRaw, Mark(nextRaw),
                                       std::memory_order_release,
                                       std::memory_order_relaxed)) {
        tryUnlink(*prevNext, currRaw, UnmarkedRaw(nextRaw));
        return curr;
      }
      // lost the mark race, or curr's Next changed; retry from this position
    }
  }

  /// Erases the element at `pos` and returns an iterator to the next live
  /// element (or end()). Built for the erase-during-iteration idiom:
  ///
  ///   for (auto it = list.begin(); it != list.end(); ) {
  ///     if (ShouldRemove(*it)) it = list.EraseAt(it);
  ///     else                   ++it;
  ///   }
  ///
  /// Unlike Erase(T*), this does NOT traverse the list to find `target`'s
  /// predecessor — it CASes `target->Next` directly. Marking is the actual
  /// linearization point of removal; the predecessor pointer is only needed
  /// for the *opportunistic* physical unlink, which this deliberately skips.
  /// Physical cleanup is left to whichever traversal next passes over the
  /// marked node — in the iterate-and-erase idiom above, that's the very
  /// next ++/EraseAt call in the same loop, so cleanup happens almost
  /// immediately anyway. This keeps a full sweep O(n) total; reusing
  /// Erase's head-to-tail search here would make it O(n) *per erasure*,
  /// i.e. O(n²) for a loop that removes many elements — exactly the kind of
  /// regression this method exists to avoid.
  ///
  /// If you're erasing one specific object outside of a traversal and want
  /// immediate physical cleanup, use Erase(T*) instead.
  Iterator EraseAt(Iterator pos) noexcept {
    T *target = pos.operator->();
    assert(target != nullptr && "EraseAt requires a valid (non-end) iterator");

    uintptr_t next = (target->*Next).load(std::memory_order_acquire);

    while (!IsMarked(next)) {
      if ((target->*Next)
              .compare_exchange_weak(next, Mark(next),
                                     std::memory_order_release,
                                     std::memory_order_relaxed)) {
        break; // we marked it; `next` still holds the unmarked value
      }
      // CAS failed: compare_exchange_weak refreshed `next` to the actual
      // current value. Loop re-checks IsMarked — either someone else
      // just marked it (loop exits naturally) or Next changed for some
      // other reason, e.g. a concurrent InsertAfter(target, ...), and we
      // retry the mark with the fresh value.
    }

    // Whether we marked `target` ourselves, lost the race to a concurrent
    // Erase/PopFront/EraseAt, or it was already marked before this call
    // even started, `next` (mark stripped) is always "what came after
    // target" — its memory stays valid under the same precondition that
    // already protects every other raw-pointer operation in this API.
    return Iterator(firstLive(UnmarkedRaw(next)));
  }

  /// Atomically detaches the entire list and returns the head of the
  /// detached chain, or nullptr if empty. Leaves this list empty.
  T *Clear() noexcept {
    uintptr_t raw = head_.exchange(0, std::memory_order_acq_rel);
    return raw == 0 ? nullptr : Unmarked<T>(raw);
  }

  Iterator begin() noexcept {
    return Iterator(firstLive(head_.load(std::memory_order_acquire)));
  }
  Iterator end() noexcept { return Iterator(); }

  Iterator Begin() noexcept { return begin(); }
  Iterator End() noexcept { return end(); }

  /// Returns the first element satisfying `pred`, or nullptr.
  /// O(n), weakly consistent — same caveats as iteration: a concurrent
  /// insert/erase elsewhere may or may not be observed.
  template <typename Predicate>
  [[nodiscard]] T *Find(Predicate pred) const noexcept {
    for (T *node = firstLive(head_.load(std::memory_order_acquire));
         node != nullptr; node = nextLive(node)) {
      if (pred(*node))
        return node;
    }
    return nullptr;
  }

  /// Returns true if `target` is currently reachable from this list.
  [[nodiscard]] bool Contains(const T *target) const noexcept {
    return Find([target](const T &x) { return &x == target; }) != nullptr;
  }

private:
  // Shared by begin() and operator++: starting from a raw (possibly 0)
  // position, walk forward past any already-marked nodes and land on the
  // first live one, or nullptr. Read-only — no helping CAS (see below).
  static T *firstLive(uintptr_t raw) noexcept {
    while (raw != 0) {
      T *node = Unmarked<T>(raw);
      uintptr_t next = (node->*Next).load(std::memory_order_acquire);
      if (!IsMarked(next))
        return node;
      raw = UnmarkedRaw(next); // `node` is itself deleted; skip it too
    }
    return nullptr;
  }

  static T *nextLive(T *curr) noexcept {
    // Strip the mark unconditionally — whether curr_ is still live or was
    // just erased out from under the iterator, curr_->Next (mark stripped)
    // is always "what comes after curr_" at the moment of this read.
    uintptr_t raw = UnmarkedRaw((curr->*Next).load(std::memory_order_acquire));
    return firstLive(raw);
  }

  static bool tryUnlink(std::atomic<uintptr_t> &prevNext, uintptr_t expectedCurr,
                 uintptr_t next) noexcept {
    return prevNext.compare_exchange_strong(expectedCurr, next,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire);
  }

  std::atomic<uintptr_t> head_;
};

} // namespace quiesce
