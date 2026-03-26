#pragma once

#include "linalg.hpp"
#include <cstddef>
#include <cstdint>

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
// N is the max number of characters (excluding the length byte).
template <uint8 N = 250> struct pascal_string_t
{
  uint8 length = 0;
  char data[N] = {};

  pascal_string_t() = default;

  pascal_string_t(const char *str) { set(str); }

  void set(const char *str)
  {
    length = 0;
    if (!str)
      return;
    while (length < N && str[length] != '\0')
    {
      data[length] = str[length];
      ++length;
    }
  }

  const char *c_str() const
  {
    // data is always null-terminated within capacity since we zero-init
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
constexpr auto client_port_number = 5001;

} // namespace network
