#pragma once

#include <cstdint>
#include <type_traits>

namespace quiesce {

/// Mask used for logical deletion tagging in pointer values.
static constexpr uintptr_t kMarkMask = 0x1u;

/// Returns true if a tagged pointer is marked.
constexpr bool IsMarked(uintptr_t raw) noexcept { return (raw & kMarkMask) != 0; }

/// Marks a raw pointer value for logical deletion.
constexpr uintptr_t Mark(uintptr_t raw) noexcept { return raw | kMarkMask; }

/// Clears the tag bits from a raw pointer value.
constexpr uintptr_t UnmarkedRaw(uintptr_t raw) noexcept { return raw & ~kMarkMask; }

template <typename T>
constexpr T *Unmarked(uintptr_t raw) noexcept {
  return reinterpret_cast<T *>(UnmarkedRaw(raw));
}

template <typename T>
constexpr uintptr_t RawPtr(T *ptr) noexcept {
  return reinterpret_cast<uintptr_t>(ptr);
}

} // namespace quiesce
