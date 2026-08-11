#pragma once

#include "span.hpp"

#include <cstdint>
#include <type_traits>

// Fixed-size OWNING arrays, the counterpart to Span's non-owning view.
//
// Two types, because the codebase has two different fixed-size shapes and only
// one of them is a plain array:
//
//   Array<T, N>              length is a number
//   Enum_Array<Enum_T, T>    length is an enum's value count, indexed by the enum
//
// WHY NOT std::array: the same three reasons Span is not std::span, plus one.
// (1) <array> drags MSVC's <xutility> into every translation unit that includes
// a generated table. (2) size() and operator[] are size_t, so every mix with
// Span's uint32_t is a narrowing cast or a signedness warning. (3) the codebase
// already owns its primitives -- Span, pascal_string_t, linalg. The fourth is
// Enum_Array, which has no standard equivalent at all and needs somewhere to
// live: `WEAPON_DEFINITIONS[weapon]` instead of
// `WEAPON_DEFINITIONS[(size_t)weapon]`, with the size tied to the enum rather
// than restated.
//
// Both are AGGREGATES with no user-declared special members, so they are
// trivially copyable exactly when their element type is. That is load-bearing:
// it is what lets one sit inside an entity struct without breaking the
// blittable / memcmp-diffable contract the schema system relies on.

// The compile-time count behind Enum_Array. The primary template is left
// undefined on purpose -- a missing specialization should say "incomplete type
// enum_traits<Foo>" at the point of use, not silently pick a default length.
//
// def_gen emits one specialization per enum in every .def, and generated ones
// carry an extra `type` member (the enum's `enum_type` id) so the compile-time
// C++ type and the runtime reflection id can be checked against each other.
// Hand-written enums specialize this next to their own declaration and provide
// `count` alone.
template <typename Enum_T> struct enum_traits;

template <typename Enum_T>
concept Indexing_Enum = std::is_enum_v<Enum_T> && requires {
  (uint32_t)enum_traits<Enum_T>::count;
};

template <typename T, uint32_t N> struct Array
{
  static_assert(N > 0, "Array<T, 0> has no storage. whatever you think you're doing, it cannot be correct.");

  // Value-initialized by default, so `Array<T, N> scratch;` is zeroed rather
  // than indeterminate. A raw `T buffer[N]` would not be, and that difference
  // is the entire reason to prefer this over one. An explicit initializer
  // replaces this, so a constexpr table pays nothing for it.
  T data[N] = {};

  constexpr T*       begin() { return data; }
  constexpr T*       end() { return data + N; }
  constexpr const T* begin() const { return data; }
  constexpr const T* end() const { return data + N; }

  // No bounds check, same contract as Span::operator[].
  constexpr T&       operator[](uint32_t index) { return data[index]; }
  constexpr const T& operator[](uint32_t index) const { return data[index]; }

  constexpr T&       front() { return data[0]; }
  constexpr const T& front() const { return data[0]; }
  constexpr T&       back() { return data[N - 1]; }
  constexpr const T& back() const { return data[N - 1]; }

  constexpr uint32_t size() const { return N; }

  // Implicit, so an Array passes straight into anything taking a Span. Span's
  // own container constructor cannot do this -- it requires `data()` as a CALL
  // and here `data` is a member variable -- which is why these exist.
  constexpr operator Span<T>() { return Span<T>(data, N); }
  constexpr operator Span<const T>() const { return Span<const T>(data, N); }
};

template <typename T, typename... Rest>
Array(T, Rest...) -> Array<T, 1 + (uint32_t)sizeof...(Rest)>;

// An Array whose length IS the enum's value count and whose index IS the enum.
//
// The storage is a bare `Value_T values[count]` rather than an Array member on
// purpose: nesting one aggregate inside another would cost a third level of
// braces at every table definition. This is the same one-extra-brace shape
// std::array has, and no worse.
//
// Enum values are dense and start at 0 -- def_gen guarantees that for generated
// enums, and a hand-written enum specializing enum_traits owes the same. A
// sparse enum would make `count` wrong as a length.
//
// WHAT THIS DOES NOT DO: it fixes the LENGTH of the storage, not that you
// filled it. A short initializer value-initializes the tail, like any
// aggregate -- add a value to the enum and the table quietly grows a zeroed
// row rather than failing. rows_in_enum_order (below) is what closes that, and
// is why a hand-written data table is not finished without it.
template <Indexing_Enum Enum_T, typename Value_T> struct Enum_Array
{
  static constexpr uint32_t count = enum_traits<Enum_T>::count;
  static_assert(count > 0, "Enum_Array over an enum with no values");

  // Zeroed by default, same as Array -- see the note there. It matters more
  // here: the runtime-storage use of this type is a table of handles or
  // pointers keyed by an enum, where the empty state IS zero.
  Value_T values[count] = {};

  // For a key your own code produced -- a literal, a switch subject, a loop
  // over the enum. Unchecked, because it cannot be out of range.
  constexpr Value_T&       operator[](Enum_T key) { return values[(uint32_t)key]; }
  constexpr const Value_T& operator[](Enum_T key) const { return values[(uint32_t)key]; }

  // For a key that came from OUTSIDE -- a snapshot field, a map file, a console
  // token. Enum fields are deserialized with no range validation, so indexing
  // on one unchecked is an out-of-bounds read driven by a packet. nullptr when
  // the key names no value.
  constexpr Value_T* try_get(Enum_T key)
  {
    return (uint32_t)key < count ? &values[(uint32_t)key] : nullptr;
  }
  constexpr const Value_T* try_get(Enum_T key) const
  {
    return (uint32_t)key < count ? &values[(uint32_t)key] : nullptr;
  }

  constexpr Value_T*       begin() { return values; }
  constexpr Value_T*       end() { return values + count; }
  constexpr const Value_T* begin() const { return values; }
  constexpr const Value_T* end() const { return values + count; }

  constexpr uint32_t size() const { return count; }

  constexpr operator Span<Value_T>() { return Span<Value_T>(values, count); }
  constexpr operator Span<const Value_T>() const { return Span<const Value_T>(values, count); }
};

// Checks that row N is the row for enum value N, given a member on the row that
// names its own enum value: `rows_in_enum_order<&weapon_definition_t::weapon>(WEAPON_DEFINITIONS)`.
//
// This is the ONLY check on the contents, and it catches both failures a table
// indexed by an enum has:
//
//   REORDER    -- swap two rows, or insert a value in the middle of the .def
//                 enum, and every lookup returns its neighbour's data.
//   SHORT LIST -- add a value to the enum and the missing tail row is
//                 value-initialized, so it is zeroed rather than absent.
//
// Both look like working code at the definition; the second is caught here
// only because a zeroed tail row does not name its own index. So every
// enum-indexed table of hand-written DATA wants a static_assert on this, and
// its rows want a member naming their own enum value in order to have one.
//
// Tables of runtime STORAGE (caches, handle arrays) are a different thing:
// they have no key member, zero-fill is what they want, and there is nothing
// here for them.
template <auto Key_Member, Indexing_Enum Enum_T, typename Value_T>
constexpr bool rows_in_enum_order(const Enum_Array<Enum_T, Value_T>& rows)
{
  for (uint32_t index = 0; index < Enum_Array<Enum_T, Value_T>::count; ++index)
    if ((uint32_t)(rows.values[index].*Key_Member) != index)
      return false;
  return true;
}
