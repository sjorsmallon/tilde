#pragma once

#include "array.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace shared
{

enum class hit_region_t : uint16_t
{
  Head,
  Torso,
  Legs,
};

constexpr uint32_t HIT_REGION_COUNT = 3;

struct hit_region_name_t
{
  hit_region_t region;
  const char*  name;
};

} // namespace shared

template <> struct enum_traits<shared::hit_region_t>
{
  static constexpr uint32_t count = shared::HIT_REGION_COUNT;
};

namespace shared
{

constexpr Enum_Array<hit_region_t, hit_region_name_t> HIT_REGION_NAMES = {{
    {hit_region_t::Head, "Head"},
    {hit_region_t::Torso, "Torso"},
    {hit_region_t::Legs, "Legs"},
}};
static_assert(rows_in_enum_order<&hit_region_name_t::region>(HIT_REGION_NAMES),
              "HIT_REGION_NAMES must list every hit_region_t in declaration order");

[[nodiscard]] constexpr const char* to_string(hit_region_t region)
{
  return HIT_REGION_NAMES[region].name;
}

[[nodiscard]] constexpr std::optional<hit_region_t> try_parse_hit_region(std::string_view text)
{
  for (const hit_region_name_t& row : HIT_REGION_NAMES)
    if (text == row.name)
      return row.region;
  return std::nullopt;
}

} // namespace shared
