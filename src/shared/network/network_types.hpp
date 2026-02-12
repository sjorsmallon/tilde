#pragma once

#include "linalg.hpp"
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

// Render component — embeddable in any entity via SCHEMA_FIELD.
// Bundles mesh reference, visibility, and a local transform.
struct render_component_t
{
  int32 mesh_id = -1;            // integer mesh asset handle (-1 = none)
  pascal_string mesh_path;       // human-readable asset path
  bool visible = true;
  vec3f offset = {0, 0, 0};     // local position offset from entity origin
  vec3f scale = {1, 1, 1};      // local scale
  vec3f rotation = {0, 0, 0};   // local rotation (Euler, degrees)
};

constexpr auto sv_max_player_count = 32;
constexpr auto server_port_number = 2020;
constexpr auto client_port_number = 2024;

} // namespace network
