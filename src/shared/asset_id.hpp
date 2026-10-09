#pragma once

// The asset ID SPACE, hand-written: the eight classes, their id types, and the
// name an asset is asked for by. Light on purpose -- the generated entity code
// includes it so a field can be typed `mesh_asset`.
//
// Ids are numbered at STARTUP by the walk (asset_walk.hpp), not by the build,
// so an id enum has exactly one named member. Every other value is minted at
// init and reached through a name: a literal in code hashes at compile time
// (`sound_id("twang")`), a string out of a map or the console hashes at run
// time (`try_from_string<sound_asset>`). asset_pipeline_def.md, "Runtime
// minting".

#include "array.hpp"
#include "span.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace assets
{

enum class asset_class_t : uint8_t
{
  mesh_asset,
  texture_asset,
  sound_asset,
  animation_asset,
  hitbox_rig,
  font_asset,
  pbr_material,
  cubemap_asset,
};

constexpr uint32_t ASSET_CLASS_COUNT = 8;

[[nodiscard]] const char* to_string(asset_class_t asset_class);

enum class mesh_asset : uint16_t { Missing = 0 };
enum class texture_asset : uint16_t { Missing = 0 };
enum class sound_asset : uint16_t { Missing = 0 };
enum class animation_asset : uint16_t { Missing = 0 };
enum class hitbox_rig : uint16_t { Missing = 0 };
enum class font_asset : uint16_t { Missing = 0 };
enum class pbr_material : uint16_t { Missing = 0 };
enum class cubemap_asset : uint16_t { Missing = 0 };

// FNV-1a, 64 bits. The one hash both spellings of a name go through, so a
// literal folded by the compiler and a string hashed at a parser agree.
[[nodiscard]] constexpr uint64_t hash_asset_name(std::string_view text)
{
  uint64_t hash = 14695981039346656037ull;
  for (const char character : text)
  {
    hash ^= (uint8_t)character;
    hash *= 1099511628211ull;
  }
  return hash;
}

// An asset named from CODE. The constructor is consteval, so only a literal
// can build one and the hash is a constant at the call site; `text` is that
// literal, kept so a name that resolves to nothing dies saying which. The
// default-constructed name (hash 0) is "no asset" and resolves to Missing --
// what `sound_asset::Missing` meant in a constexpr table.
struct asset_name_t
{
  uint64_t    hash = 0;
  const char* text = "";

  constexpr asset_name_t() = default;
  consteval asset_name_t(const char* literal) : hash(hash_asset_name(literal)), text(literal) {}

  [[nodiscard]] constexpr bool empty() const { return hash == 0; }
};

inline constexpr asset_name_t NO_ASSET_NAME{};

// One row of a class's table. `path` is empty for Missing, whose bytes are a
// compiled-in constant; for everything else it is the ONE spelling
// read_asset_bytes takes.
struct asset_entry_t
{
  std::string name;
  std::string path;
  uint64_t    name_hash = 0;
};

// Every row of a class, indexed by id. The generated field table carries the
// class as `(int32_t)asset_class_t`, which is what the text conversion, the
// wire range check and the inspector's picker pass in.
[[nodiscard]] Span<const asset_entry_t> asset_class_entries(asset_class_t asset_class);
[[nodiscard]] Span<const asset_entry_t> asset_class_entries(int32_t asset_class_id);
[[nodiscard]] uint32_t                  asset_count(asset_class_t asset_class);

// An FNV over every class's rows in id order, so two processes whose resource
// trees differ refuse to talk (the connect handshake) and a replay recorded
// under another tree refuses to open. The runtime half of SCHEMA_HASH.
[[nodiscard]] uint32_t asset_table_hash();

// --- Names to ids ---
//
// From a literal: a name no file carries is a broken build, so these die naming
// it. From text: input, so the optional is the answer.

[[nodiscard]] mesh_asset      mesh_id(asset_name_t name);
[[nodiscard]] texture_asset   texture_id(asset_name_t name);
[[nodiscard]] sound_asset     sound_id(asset_name_t name);
[[nodiscard]] animation_asset animation_id(asset_name_t name);
[[nodiscard]] hitbox_rig      hitbox_rig_id(asset_name_t name);
[[nodiscard]] font_asset      font_id(asset_name_t name);
[[nodiscard]] pbr_material    pbr_material_id(asset_name_t name);
[[nodiscard]] cubemap_asset   cubemap_id(asset_name_t name);

template <typename T> [[nodiscard]] std::optional<T> try_from_string(std::string_view text);

template <> std::optional<mesh_asset>      try_from_string<mesh_asset>(std::string_view text);
template <> std::optional<texture_asset>   try_from_string<texture_asset>(std::string_view text);
template <> std::optional<sound_asset>     try_from_string<sound_asset>(std::string_view text);
template <> std::optional<animation_asset> try_from_string<animation_asset>(std::string_view text);
template <> std::optional<hitbox_rig>      try_from_string<hitbox_rig>(std::string_view text);
template <> std::optional<font_asset>      try_from_string<font_asset>(std::string_view text);
template <> std::optional<pbr_material>    try_from_string<pbr_material>(std::string_view text);
template <> std::optional<cubemap_asset>   try_from_string<cubemap_asset>(std::string_view text);

// The name behind an id, for logs and the editor. An id outside the class
// reads as "(no such asset)" rather than crashing a log line.
[[nodiscard]] const char* to_string(mesh_asset id);
[[nodiscard]] const char* to_string(texture_asset id);
[[nodiscard]] const char* to_string(sound_asset id);
[[nodiscard]] const char* to_string(animation_asset id);
[[nodiscard]] const char* to_string(hitbox_rig id);
[[nodiscard]] const char* to_string(font_asset id);
[[nodiscard]] const char* to_string(pbr_material id);
[[nodiscard]] const char* to_string(cubemap_asset id);

} // namespace assets

template <> struct enum_traits<assets::asset_class_t>
{
  static constexpr uint32_t count = assets::ASSET_CLASS_COUNT;
};
