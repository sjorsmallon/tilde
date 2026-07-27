#pragma once

#include <cstdint>

// A non-owning view over a contiguous range: pointer and length carried as ONE
// value, so they cannot be read out of step.
//
// This replaces every "pointer plus count" spelling in the codebase -- the
// out-param form (`const T* f(uint32_t* out_count)`) and the pair-of-members
// form (`struct { T* data; uint32_t count; }`) alike. There is deliberately
// only one such type: a second spelling is how the two forms got here.
//
// WHY NOT std::span: this is the type the generated entity tables hand back,
// and <span> drags in the <ranges> machinery for every translation unit that
// includes them -- which, after P5, is most of the entity-touching code. The
// house type is 20 lines, compiles instantly, and matches how the codebase
// already owns its primitives (pascal_string_t, asset_handle_t, linalg).
//
// It iterates like any range: begin()/end() return raw pointers, which are
// already contiguous iterators, so range-based for, std::for_each and the
// std::ranges algorithms all work on it unchanged.
//
//   for (const field_info_t& field : entity_info(type).fields)
//     ...

template <typename T> struct Span
{
  T*       data  = nullptr;
  uint32_t count = 0;

  constexpr Span() = default;
  constexpr Span(T* data, uint32_t count) : data(data), count(count) {}

  // Deduces the length of a C array, so a table can be handed over without
  // repeating its size -- the mistake this type exists to prevent.
  template <uint32_t N> constexpr Span(T (&array)[N]) : data(array), count(N) {}

  // Any contiguous container (std::vector, std::array, ...) converts
  // implicitly, so call sites read the same as they did under std::span.
  // Span itself is excluded because `data` is a member VARIABLE here, so
  // `c.data()` is ill-formed and the constraint rejects it -- the copy
  // constructor is not hijacked.
  //
  // Binds only to lvalues. That is deliberate: a Span over a temporary
  // container dangles the moment the full-expression ends.
  template <typename Container>
    requires requires(Container &container) {
      container.data();
      container.size();
    }
  constexpr Span(Container &container)
      : data(container.data()), count((uint32_t)container.size())
  {
  }

  constexpr T*       begin() const { return data; }
  constexpr T*       end() const { return data + count; }
  constexpr T&       operator[](uint32_t index) const { return data[index]; }
  constexpr uint32_t size() const { return count; }
  constexpr bool     empty() const { return count == 0; }
  constexpr T&       front() const { return data[0]; }
  constexpr T&       back() const { return data[count - 1]; }

  // No bounds check: same contract as operator[]. Callers that can be wrong
  // about the range should check size() first.
  constexpr Span<T> subspan(uint32_t offset) const
  {
    return Span<T>(data + offset, count - offset);
  }
  constexpr Span<T> subspan(uint32_t offset, uint32_t length) const
  {
    return Span<T>(data + offset, length);
  }
};
