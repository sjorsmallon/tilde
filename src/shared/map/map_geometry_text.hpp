#pragma once

#include "entity_uid.hpp"
#include "face_surface.hpp"
#include "linalg.hpp"
#include "map/mesh/primitive_recipe.hpp"
#include "map_blocks.hpp"
#include "span.hpp"
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The text vocabulary of a geometry block (map_format_def.md §4): what the
// kinds' writers and readers in map_geometry.cpp, map_geometry_mesh.cpp and
// map.cpp spell numbers, surfaces, faces and recipes with. Every reader leaves
// its target at the default on a miss and logs anything present but
// unparseable. Implemented in map_geometry.cpp, the recipe pair in map.cpp.

namespace shared
{

std::string format_float(float value);
std::string format_vec3(const linalg::vec3 &value);
std::string format_bool(bool value);
std::string format_float_exact(float value);
std::string format_vec3_exact(const linalg::vec3 &value);
std::string format_quat_exact(const linalg::quatf &value);

const std::string *find_property(const std::map<std::string, std::string> &properties,
                                 const char *key);
void read_float(const std::map<std::string, std::string> &properties, const char *key,
                float &out);
void read_int(const std::map<std::string, std::string> &properties, const char *key,
              int32_t &out);
void read_bool(const std::map<std::string, std::string> &properties, const char *key,
               bool &out);
void read_string(const std::map<std::string, std::string> &properties, const char *key,
                 std::string &out);
void read_vec3(const std::map<std::string, std::string> &properties, const char *key,
               linalg::vec3 &out);
void read_rotation(const std::map<std::string, std::string> &properties, linalg::quatf &out);

void write_surface(const geometry_surface_t &surface,
                   std::vector<std::pair<std::string, std::string>> &out);
void read_surface(const std::map<std::string, std::string> &properties,
                  geometry_surface_t &surface);
void write_uid_property(const char *key, entity_uid_t uid,
                        std::vector<std::pair<std::string, std::string>> &out);
void read_uid_property(const std::map<std::string, std::string> &properties, const char *key,
                       entity_uid_t &uid);

// A `face` block: the plane key then the fields (a brush), or the fields alone
// after a key of the caller's own (a mesh writes `indices`). The reader takes
// whichever keys are there.
void write_face_surface(const face_surface_t &face, Span<const uint16_t> material_remap,
                        map_block_out_t &out);
void write_face_surface_fields(const face_surface_t &face, Span<const uint16_t> material_remap,
                               map_block_out_t &out);
face_surface_t read_face_surface(const map_block_t &block);

// A `primitive` block, the text of a primitive_recipe_t. `owner_uid` only
// names what a refusal is about.
[[nodiscard]] std::optional<primitive_recipe_t>
parse_primitive_recipe_block(const map_block_t &block, entity_uid_t owner_uid);
map_block_out_t write_primitive_recipe_block(const primitive_recipe_t &recipe);

} // namespace shared
