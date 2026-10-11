#pragma once

// What a surface is MADE OF for the purpose of sound, one hop above the
// material: fourteen plastic and glass recolours are two surfaces. A material
// folder names its kind in `surface.txt` (material_sound_plan.md step 1);
// absent is Default. Not on the wire -- the wire carries the material index
// and the client derives the kind from it.
//
//   surface_file := kind_name [ whitespace ]
//   kind_name    := "default" | "stone" | "metal" | "wood" | "glass" | "fabric" | "plastic"

#include "array.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace shared
{

enum class Surface_Kind : uint8_t
{
  Default,
  Stone,
  Metal,
  Wood,
  Glass,
  Fabric,
  Plastic,
};

constexpr uint32_t SURFACE_KIND_COUNT = 7;

struct surface_kind_name_t
{
  Surface_Kind kind;
  const char*  name;
};

} // namespace shared

template <> struct enum_traits<shared::Surface_Kind>
{
  static constexpr uint32_t count = shared::SURFACE_KIND_COUNT;
};

namespace shared
{

constexpr Enum_Array<Surface_Kind, surface_kind_name_t> SURFACE_KIND_NAMES = {{
    {Surface_Kind::Default, "default"},
    {Surface_Kind::Stone, "stone"},
    {Surface_Kind::Metal, "metal"},
    {Surface_Kind::Wood, "wood"},
    {Surface_Kind::Glass, "glass"},
    {Surface_Kind::Fabric, "fabric"},
    {Surface_Kind::Plastic, "plastic"},
}};
static_assert(rows_in_enum_order<&surface_kind_name_t::kind>(SURFACE_KIND_NAMES),
              "SURFACE_KIND_NAMES must list every Surface_Kind in declaration order");

[[nodiscard]] constexpr const char* to_string(Surface_Kind kind)
{
  return SURFACE_KIND_NAMES[kind].name;
}

// The inverse of to_string, for `surface.txt`. Trims surrounding whitespace; the
// word itself is matched exactly.
[[nodiscard]] std::optional<Surface_Kind> try_parse_surface_kind(std::string_view text);

} // namespace shared
