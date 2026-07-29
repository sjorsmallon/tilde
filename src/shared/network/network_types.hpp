#pragma once

#include "linalg.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace network
{

using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;

using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

using float32 = float;
using float64 = double;

using vec3f = linalg::vec3_t<float32>;

// Fixed-capacity inline string, trivially copyable for schema serialization.
// N is the max number of characters (excluding the length byte). Storage is
// N + 1 bytes so that even a full-capacity string is null-terminated and
// c_str() never reads past the buffer.
//
// CANONICAL ZERO-PADDING INVARIANT: every byte from data[length] to the end of
// the buffer is zero. Two pascal_string_t holding the same text therefore
// always compare equal under memcmp — which is exactly what the schema layer
// uses for baseline diffing, so a violated invariant shows up as a phantom
// delta (a field replicated every tick because its dead tail bytes differ).
// Every mutation path must restore it: see set() here, and the PascalString
// branch of deserialize_field_from_bits() in entity.cpp.
template <uint8 N = 250> struct pascal_string_t
{
  uint8 length = 0;
  char data[N + 1] = {};

  pascal_string_t() = default;

  pascal_string_t(const char *str) { set(str); }

  // Copies str, truncating at capacity. Returns false if it did not fit —
  // truncation is a caller error, not a silently accepted outcome.
  bool set(const char *str)
  {
    length = 0;
    if (str)
    {
      while (length < N && str[length] != '\0')
      {
        data[length] = str[length];
        ++length;
      }
    }

    // Restore the zero-padding invariant: everything past the last character
    // must be zero, not residue from a previous, longer value. Without this,
    // "hello" -> "hi" leaves "hi\0lo" and c_str() reads "hi" but memcmp sees a
    // difference that isn't there.
    std::memset(data + length, 0, sizeof(data) - length);

    const bool fits = (str == nullptr) || (str[length] == '\0');
    assert(fits && "pascal_string_t::set(): string exceeds capacity");
    return fits;
  }

  void clear() { set(nullptr); }

  const char *c_str() const
  {
    // Always null-terminated: the invariant above zeroes data[length], and the
    // buffer has a spare byte so that holds even at full capacity.
    return data;
  }

  uint8 max_length() const { return N; }
};

// Default pascal string type used by the schema system
using pascal_string = pascal_string_t<250>;

// Fixed-capacity inline array, trivially copyable for schema serialization.
// Follows the same pattern as pascal_string_t: count-prefixed, inline storage.
template <typename T, uint16 MaxN> struct schema_array_t
{
  uint16 count = 0;
  T data[MaxN] = {};

  schema_array_t() = default;

  uint16 size() const { return count; }
  uint16 capacity() const { return MaxN; }
  bool empty() const { return count == 0; }

  T &operator[](uint16 i) { return data[i]; }
  const T &operator[](uint16 i) const { return data[i]; }

  void push_back(const T &val)
  {
    if (count < MaxN)
      data[count++] = val;
  }

  void resize(uint16 n)
  {
    if (n > MaxN)
      n = MaxN;
    // Zero-init new elements
    for (uint16 i = count; i < n; ++i)
      data[i] = T{};
    count = n;
  }

  void clear() { count = 0; }

  T *begin() { return data; }
  T *end() { return data + count; }
  const T *begin() const { return data; }
  const T *end() const { return data + count; }
};

// Helpers for type-erased access to schema_array_t<float32, N> from serialization code.
// The memory layout is the same for all N: [uint16 count] [padding] [float32 data...]
using schema_float_array_1 = schema_array_t<float32, 1>;
static constexpr size_t schema_float_array_data_offset =
    offsetof(schema_float_array_1, data);

inline uint16 schema_float_array_count(const void *ptr)
{
  return *static_cast<const uint16 *>(ptr);
}

inline void schema_float_array_set_count(void *ptr, uint16 n)
{
  *static_cast<uint16 *>(ptr) = n;
}

inline const float32 *schema_float_array_data(const void *ptr)
{
  return reinterpret_cast<const float32 *>(
      static_cast<const uint8 *>(ptr) + schema_float_array_data_offset);
}

inline float32 *schema_float_array_data_mut(void *ptr)
{
  return reinterpret_cast<float32 *>(
      static_cast<uint8 *>(ptr) + schema_float_array_data_offset);
}

inline uint16 schema_float_array_max_capacity(size_t field_size)
{
  return static_cast<uint16>(
      (field_size - schema_float_array_data_offset) / sizeof(float32));
}

// render_component_t moved to components.hpp (included after schema.hpp)
// to avoid circular dependencies

constexpr auto sv_max_player_count = 32;
constexpr auto server_port_number = 9999;
// NOTE: there is deliberately no client_port_number. Clients bind an
// ephemeral port (open(0)) — a fixed client port made two clients on one
// machine indistinguishable to the server (both 127.0.0.1:5001) and starved
// the second of replies. The server keys players by the address recvfrom
// reports, so the client port never needs to be known in advance.

} // namespace network
