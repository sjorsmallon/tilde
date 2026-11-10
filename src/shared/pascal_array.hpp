#pragma once

#include "array.hpp"
#include "span.hpp"

#include <cstdint>

// A COUNT and at most N elements: the generalisation of pascal_string_t to any
// element, and what a `T<N>` field in messages.def or entities.def becomes. The
// bound is what keeps a message a stack local and an entity a fixed-stride pool
// row the codec addresses by offset; there is no unbounded list on the wire. An
// aggregate with no user-declared special members, so it is trivially copyable
// exactly when T is. The count is occupancy from the front, nothing more.
template <typename T, uint32_t N> struct Pascal_Array
{
  using element_t = T;
  static constexpr uint32_t capacity = N;

  uint32_t    count = 0;
  Array<T, N> elements;

  constexpr uint32_t size() const { return count; }
  constexpr bool     empty() const { return count == 0; }
  constexpr bool     full() const { return count == N; }
  constexpr void     clear() { count = 0; }

  constexpr T*       begin() { return elements.data; }
  constexpr T*       end() { return elements.data + count; }
  constexpr const T* begin() const { return elements.data; }
  constexpr const T* end() const { return elements.data + count; }

  // No bounds check, same contract as Span::operator[].
  constexpr T&       operator[](uint32_t index) { return elements.data[index]; }
  constexpr const T& operator[](uint32_t index) const { return elements.data[index]; }

  // False, and nothing stored, when the list is full: the bound is the
  // protocol's, and a caller with more to say sends another message.
  [[nodiscard]] constexpr bool try_push_back(const T& value)
  {
    if (count == N)
      return false;
    elements.data[count++] = value;
    return true;
  }

  constexpr operator Span<T>() { return Span<T>(elements.data, count); }
  constexpr operator Span<const T>() const { return Span<const T>(elements.data, count); }
};
