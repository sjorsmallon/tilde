#pragma once

// A material folder's declaration, `material.txt` (material_def.md ss3): the
// shader an author names and the lines that shader reads. The file is the
// folder's marker to the walk, so every material folder carries one.
//
//   material_file := line*
//   line          := key SP value NL
//   key           := "shader" | "surface"
//   value         := word
//   word          := [a-z_]+
//
// `shader` absent is Lit; `surface` absent is Default. A key or value this
// grammar does not know dies naming the folder.

#include "array.hpp"
#include "map/surface_kind.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace assets
{

// What an AUTHOR names. The renderer's shader_t is the program that draws it;
// a Lit material is drawn by whichever variant its maps call for.
enum class material_shader_t : uint8_t
{
  Lit,
  Unlit,
  Ghost,
  Procedural_Blending,
  Locked_Wall,
};

constexpr uint32_t MATERIAL_SHADER_COUNT = 5;

struct material_shader_name_t
{
  material_shader_t shader;
  const char*       name;
};

} // namespace assets

template <> struct enum_traits<assets::material_shader_t>
{
  static constexpr uint32_t count = assets::MATERIAL_SHADER_COUNT;
};

namespace assets
{

constexpr Enum_Array<material_shader_t, material_shader_name_t> MATERIAL_SHADER_NAMES = {{
    {material_shader_t::Lit, "lit"},
    {material_shader_t::Unlit, "unlit"},
    {material_shader_t::Ghost, "ghost"},
    {material_shader_t::Procedural_Blending, "procedural_blending"},
    {material_shader_t::Locked_Wall, "locked_wall"},
}};
static_assert(rows_in_enum_order<&material_shader_name_t::shader>(MATERIAL_SHADER_NAMES),
              "MATERIAL_SHADER_NAMES must list every material_shader_t in declaration order");

[[nodiscard]] constexpr const char* to_string(material_shader_t shader)
{
  return MATERIAL_SHADER_NAMES[shader].name;
}

[[nodiscard]] std::optional<material_shader_t> try_parse_material_shader(std::string_view word);

// Whether the program samples the folder's maps at all. A map a shader never
// reads is not missing when absent, so the renderer's checkerboard stays away.
[[nodiscard]] constexpr bool material_shader_reads_maps(material_shader_t shader)
{
  switch (shader)
  {
  case material_shader_t::Lit:
  case material_shader_t::Unlit:
  case material_shader_t::Locked_Wall:
    return true;
  case material_shader_t::Ghost:
  case material_shader_t::Procedural_Blending:
    return false;
  }
  return true;
}

struct material_file_t
{
  material_shader_t    shader  = material_shader_t::Lit;
  shared::Surface_Kind surface = shared::Surface_Kind::Default;
};

// `folder` names the material in the message of a line that will not parse.
[[nodiscard]] material_file_t parse_material_file(std::string_view text, std::string_view folder);

} // namespace assets
