#include "map.hpp"
#include "asset.hpp"
#include "log.hpp"
#include "player_constants.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace shared
{

namespace
{

// Binary sidecar format for .navmesh files (polygon mesh, v3).
// Layout: magic(4) + version(4) + num_vertices(4) + num_polygons(4)
//       + vertices[num_vertices * (float x, float y, float z)]
//       + polygons[num_polygons * (uint32 num_verts, int32 verts[N], int32 neighbors[N], int32 island)]
static constexpr uint32_t NAVMESH_MAGIC   = 0x504F4C59; // "POLY"
static constexpr uint32_t NAVMESH_VERSION = 3;

static std::string navmesh_path_for(const std::string &map_path)
{
  auto dot = map_path.rfind('.');
  if (dot != std::string::npos)
    return map_path.substr(0, dot) + ".navmesh";
  return map_path + ".navmesh";
}

static void save_navmesh(const std::string &map_path, const navmesh_t &nav)
{
  std::ofstream out(navmesh_path_for(map_path), std::ios::binary);
  if (!out.is_open())
    return;

  auto write = [&](const auto &v) {
    out.write(reinterpret_cast<const char *>(&v), sizeof(v));
  };

  write(NAVMESH_MAGIC);
  write(NAVMESH_VERSION);
  write((uint32_t)nav.vertices.size());
  write((uint32_t)nav.polygons.size());

  for (const auto &v : nav.vertices)
  {
    write(v.pos.x);
    write(v.pos.y);
    write(v.pos.z);
  }

  for (const auto &p : nav.polygons)
  {
    uint32_t n = (uint32_t)p.vertices.size();
    write(n);
    for (uint32_t k = 0; k < n; ++k) write(p.vertices[k]);
    for (uint32_t k = 0; k < n; ++k) write(p.neighbors[k]);
    write(p.island);
  }
}

static bool load_navmesh(const std::string &map_path, navmesh_t &nav)
{
  std::ifstream in(navmesh_path_for(map_path), std::ios::binary);
  if (!in.is_open())
    return false;

  auto read = [&](auto &v) {
    in.read(reinterpret_cast<char *>(&v), sizeof(v));
  };

  uint32_t magic, version;
  read(magic);
  read(version);
  if (magic != NAVMESH_MAGIC || version != NAVMESH_VERSION)
    return false;

  uint32_t num_verts, num_polys;
  read(num_verts);
  read(num_polys);

  nav.vertices.resize(num_verts);
  for (auto &v : nav.vertices)
  {
    read(v.pos.x);
    read(v.pos.y);
    read(v.pos.z);
  }

  nav.polygons.resize(num_polys);
  for (auto &p : nav.polygons)
  {
    uint32_t n;
    read(n);
    p.vertices.resize(n);
    p.neighbors.resize(n);
    for (uint32_t k = 0; k < n; ++k) read(p.vertices[k]);
    for (uint32_t k = 0; k < n; ++k) read(p.neighbors[k]);
    read(p.island);
  }

  return in.good();
}



// ============================================================================
// The .source file format
//
//   map_file := block*
//   block    := keyword '{' property* '}'
//   keyword  := 'entity' | 'box' | 'static_mesh' | 'displacement'
//   property := string string                  -- key, then value
//   string   := '"' char* '"'                  -- no escapes; may contain spaces
//
// Tokens are whitespace-separated, so '{' and '}' must stand alone. This reader
// doesn't know the keyword set: it reads EVERY block to its closing brace and
// hands them all up, and parse_map_from_string reports-and-skips the ones it
// doesn't recognize. That's what makes a map written by a newer build degrade to
// "missing some objects" on an older one instead of derailing the whole parse.
//
// 'entity' blocks carry a "classname" property naming the entity class (with
// "worldspawn" as the pseudo-class holding the map's own properties); every
// other keyword names a geometry kind and is read by parse_geometry(). Geometry
// used to live in 'entity' blocks too — see convert_legacy_geometry_entity().
// ============================================================================

struct map_block_t
{
  std::string keyword;
  // For 'entity' blocks this holds "classname" alongside the rest, exactly as
  // written; the entity path pulls it out itself.
  std::map<std::string, std::string> properties;
};

// Strip the surrounding quotes from a token, if it has them.
std::string unquote(std::string token)
{
  if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
    return token.substr(1, token.size() - 2);
  return token;
}

std::vector<map_block_t> parse_map_content(const std::string &content)
{
  std::vector<map_block_t> blocks;
  std::stringstream stream(content);
  std::string token;

  while (stream >> token)
  {
    map_block_t block;
    block.keyword = token;

    std::string brace;
    if (!(stream >> brace) || brace != "{")
    {
      log_error("map parse: block \"{}\" is not followed by '{{' (got \"{}\") — "
                "stopping",
                block.keyword, brace);
      break;
    }

    while (stream >> token)
    {
      if (token == "}")
        break;

      std::string key = unquote(token);

      std::string value;
      if (!(stream >> value))
      {
        log_error("map parse: key \"{}\" in block \"{}\" has no value", key,
                  block.keyword);
        break;
      }

      // A quoted value may contain spaces ("0 0 0"), so keep consuming tokens
      // until the closing quote.
      if (!value.empty() && value.front() == '"')
      {
        while (value.back() != '"' && !stream.eof())
        {
          std::string part;
          stream >> part;
          value += " " + part;
        }
        value = unquote(value);
      }

      block.properties[key] = value;
    }

    blocks.push_back(std::move(block));
  }

  return blocks;
}

// One block on the way out. Properties are an ordered vector, not a map, so a
// writer can emit them in declaration order and the file stays git-diffable.
// (Entity blocks still come out schema-ordered — i.e. alphabetical — until the
// generator cutover rewrites entity save/load too.)
struct map_block_out_t
{
  std::string keyword;
  std::vector<std::pair<std::string, std::string>> properties;
};

std::string serialize_map_blocks(const std::vector<map_block_out_t> &blocks)
{
  std::stringstream stream;
  for (const map_block_out_t &block : blocks)
  {
    stream << block.keyword << "\n{\n";
    for (const auto &[key, value] : block.properties)
      stream << "  \"" << key << "\" \"" << value << "\"\n";
    stream << "}\n";
  }
  return stream.str();
}

// ============================================================================
// One-time conversion of pre-exit geometry entities
//
// Geometry used to be entities, so it was written as 'entity' blocks with
// schema-serialized properties — including component fields flattened into a
// single "key:value|key:value" blob ("render", "volume"). Nothing writes that
// form any more; this reads it, and the next save emits proper geometry blocks.
// One conversion, no versioned format, no permanent shim.
// ============================================================================

// Split a legacy component blob into its key/value pairs. Splits on '|', then at
// the FIRST ':' of each part. Nested components were flattened into the same
// list, so "material:shader_type:lit|color:1 1 1" yields
// {"material": "shader_type:lit", "color": "1 1 1"} — the nested value keeps its
// own "key:value" text, which convert_legacy_render_component() below unpicks.
std::map<std::string, std::string> parse_legacy_component_blob(const std::string &blob)
{
  std::map<std::string, std::string> fields;
  size_t start = 0;
  while (start <= blob.size())
  {
    const size_t bar = blob.find('|', start);
    const std::string part =
        blob.substr(start, (bar == std::string::npos) ? std::string::npos : bar - start);

    const size_t colon = part.find(':');
    if (colon != std::string::npos)
      fields[part.substr(0, colon)] = part.substr(colon + 1);

    if (bar == std::string::npos)
      break;
    start = bar + 1;
  }
  return fields;
}

linalg::vec3 parse_legacy_vec3(const std::string &text, linalg::vec3 fallback)
{
  std::istringstream stream(text);
  linalg::vec3 parsed;
  if (stream >> parsed.x >> parsed.y >> parsed.z)
    return parsed;
  return fallback;
}

// Read a legacy "render" blob into the geometry surface it becomes.
void convert_legacy_render_component(const std::string &render_blob,
                                     geometry_surface_t &surface)
{
  const std::map<std::string, std::string> fields =
      parse_legacy_component_blob(render_blob);

  auto field = [&](const char *key) -> const std::string *
  {
    auto it = fields.find(key);
    return (it == fields.end()) ? nullptr : &it->second;
  };

  if (const std::string *mesh_path = field("mesh_path"))
    surface.mesh_path = *mesh_path;
  if (const std::string *visible = field("visible"))
    surface.visible = (*visible == "true" || *visible == "1");
  if (const std::string *is_wireframe = field("is_wireframe"))
    surface.is_wireframe = (*is_wireframe == "true" || *is_wireframe == "1");
  if (const std::string *color = field("color"))
    surface.color = parse_legacy_vec3(*color, surface.color);
  if (const std::string *roughness = field("roughness"))
  {
    try
    {
      surface.roughness = std::stof(*roughness);
    }
    catch (const std::exception &)
    {
      log_error("map conversion: legacy render roughness \"{}\" is not a float",
                *roughness);
    }
  }

  // material's own fields were flattened in, so its first one arrives as the
  // value of the "material" key: "shader_type:lit".
  if (const std::string *material = field("material"))
  {
    const size_t colon = material->find(':');
    if (colon != std::string::npos && material->substr(0, colon) == "shader_type")
      surface.shader_type = material->substr(colon + 1);
  }
}

// Convert one legacy geometry entity block. Returns false if this classname was
// never geometry (so the caller keeps treating it as an entity).
bool convert_legacy_geometry_entity(const std::string &classname,
                                    const std::map<std::string, std::string> &properties,
                                    geometry_value_t &out_geometry)
{
  auto property = [&](const char *key) -> const std::string *
  {
    auto it = properties.find(key);
    return (it == properties.end()) ? nullptr : &it->second;
  };

  // Legacy geometry's shape came from a box_volume_t component field named
  // "volume", holding just "half_extents:X Y Z" — and before THAT refactor, from
  // a flat "half_extents" property on the entity itself. Both forms are read
  // here, which is what retires the old "center"/"half_extents" compatibility
  // shims: they don't move to the geometry writer, they end at this conversion.
  auto legacy_half_extents = [&](linalg::vec3 fallback) -> linalg::vec3
  {
    if (const std::string *volume = property("volume"))
    {
      const std::map<std::string, std::string> fields =
          parse_legacy_component_blob(*volume);
      auto it = fields.find("half_extents");
      if (it != fields.end())
        return parse_legacy_vec3(it->second, fallback);
    }

    // Pre-box_volume_t form.
    if (const std::string *half_extents = property("half_extents"))
      return parse_legacy_vec3(*half_extents, fallback);

    return fallback;
  };

  // "center" is the pre-position name, from when the shape carried its own
  // center instead of the entity carrying a position.
  auto legacy_position = [&]() -> linalg::vec3
  {
    if (const std::string *position = property("position"))
      return parse_legacy_vec3(*position, {0, 0, 0});
    if (const std::string *center = property("center"))
      return parse_legacy_vec3(*center, {0, 0, 0});
    return {0, 0, 0};
  };

  auto legacy_surface = [&](geometry_surface_t &surface)
  {
    if (const std::string *render = property("render"))
      convert_legacy_render_component(*render, surface);
  };

  if (classname == "aabb_entity")
  {
    box_geometry_t box;
    box.position = legacy_position();
    box.half_extents = legacy_half_extents(box.half_extents);
    legacy_surface(box.surface);
    out_geometry = std::move(box);
    return true;
  }

  if (classname == "static_mesh_entity")
  {
    static_mesh_geometry_t static_mesh;
    static_mesh.position = legacy_position();
    if (const std::string *orientation = property("orientation"))
      static_mesh.orientation = parse_legacy_vec3(*orientation, {0, 0, 0});
    legacy_surface(static_mesh.surface);

    // Scale lived on the render component, not on the entity.
    if (const std::string *render = property("render"))
    {
      const std::map<std::string, std::string> fields = parse_legacy_component_blob(*render);
      auto it = fields.find("scale");
      if (it != fields.end())
        static_mesh.scale = parse_legacy_vec3(it->second, static_mesh.scale);
    }

    out_geometry = std::move(static_mesh);
    return true;
  }

  if (classname == "displacement_entity")
  {
    displacement_geometry_t displacement;
    displacement.position = legacy_position();
    displacement.half_extents = legacy_half_extents(displacement.half_extents);

    if (const std::string *active_face = property("active_face"))
    {
      try
      {
        displacement.active_face = (box_face_t)std::stol(*active_face);
      }
      catch (const std::exception &)
      {
        log_error("map conversion: legacy active_face \"{}\" is not an int",
                  *active_face);
      }
    }

    if (const std::string *subdivision = property("subdivision_level"))
    {
      try
      {
        displacement.subdivision_level = (int32_t)std::stol(*subdivision);
      }
      catch (const std::exception &)
      {
        log_error("map conversion: legacy subdivision_level \"{}\" is not an int",
                  *subdivision);
      }
    }

    // The legacy array was a flat float list ("<float_count> f f f ..."), three
    // floats per vertex; the value type stores one vec3 per vertex.
    displacement.displacements.assign((size_t)displacement.vertex_count(),
                                      linalg::vec3{0, 0, 0});
    if (const std::string *raw = property("displacements"))
    {
      std::istringstream stream(*raw);
      size_t announced_float_count = 0;
      if (stream >> announced_float_count)
      {
        std::vector<linalg::vec3> parsed;
        linalg::vec3 value;
        while (stream >> value.x >> value.y >> value.z)
          parsed.push_back(value);

        if (parsed.size() * 3 != announced_float_count)
          log_error("map conversion: legacy displacements announced {} floats, "
                    "read {} vertices ({} floats)",
                    announced_float_count, parsed.size(), parsed.size() * 3);

        parsed.resize((size_t)displacement.vertex_count(), linalg::vec3{0, 0, 0});
        displacement.displacements = std::move(parsed);
      }
      else
      {
        log_error("map conversion: legacy displacements has no leading float "
                  "count — grid left flat");
      }
    }

    legacy_surface(displacement.surface);
    out_geometry = std::move(displacement);
    return true;
  }

  return false;
}

// ============================================================================
// Entity blocks: classnames and fields
// ============================================================================

// The classnames the macro system wrote, for the types whose generated
// classname differs. The generator derives a classname from the declared type
// name (Light_Entity -> "light_entity"); the X-macro let each entry pick its
// own string, and five of them picked something else.
//
// This is the "renames via one-time map conversion" rule from the phase plan,
// running on load rather than as a separate pass: a map written before the
// cutover still loads, and the first save rewrites it with the generated name,
// after which the alias is never consulted for that file again. No version
// number is involved, and none is needed -- the old name either appears or it
// does not.
struct legacy_classname_t
{
  const char *legacy;
  const char *current;
};

constexpr legacy_classname_t LEGACY_CLASSNAMES[] = {
    {"player_start", "player_spawn_entity"},
    {"weapon_basic", "weapon_entity"},
    {"particle_emitter", "particle_emitter_entity"},
    {"trigger_volume", "trigger_volume_entity"},
    {"physics_body", "physics_body_entity"},
};

} // namespace

std::shared_ptr<entities::Entity> create_map_entity(const std::string &classname)
{
  entities::entity_type type = entities::entity_type_from_classname(classname.c_str());

  if (type == entities::entity_type::Invalid)
  {
    for (const legacy_classname_t &alias : LEGACY_CLASSNAMES)
    {
      if (classname != alias.legacy)
        continue;
      type = entities::entity_type_from_classname(alias.current);
      break;
    }
  }

  return make_entity(type);
}

std::shared_ptr<entities::Entity> make_entity(entities::entity_type type)
{
  if (type == entities::entity_type::Invalid)
    return nullptr;

  // create_entity() gives back the CONCRETE type's raw pointer but erases it
  // to Entity*, so the deleter has to be attached explicitly here -- entities
  // have no virtual destructor and never will, and destroy_entity is what
  // recovers the concrete type from the runtime tag before delete.
  entities::Entity *entity = entities::create_entity(type);
  return std::shared_ptr<entities::Entity>(entity, &entities::destroy_entity);
}

std::pair<entity_uid_t, std::shared_ptr<entities::Entity>>
spawn_entity(map_t &map, entities::entity_type type)
{
  std::shared_ptr<entities::Entity> entity = make_entity(type);
  if (!entity)
    return {0, nullptr};

  entity_uid_t uid = map.add_entity(entity);
  return {uid, entity};
}

namespace
{

// Rewrites a pre-cutover entity block into the shape read_entity_fields expects.
// The third and last piece of the one-time conversion, after the classname
// aliases and the numeric-enum fallback. Three transformations, all of them
// consequences of decisions recorded in entities.def:
//
//   1. COMPONENT BLOBS -> DOTTED KEYS. A component used to serialize into one
//      value ("volume" = "half_extents:1 2 3"); it is now one key per leaf
//      ("volume.half_extents"). Same split rule as the geometry conversion.
//   2. RENAMED KEYS. Only where a field's NAME changed, which is the one
//      versioning case the no-version-numbers rule cannot absorb.
// A third legacy form -- enum values as lowercase strings -- is handled in
// read_entity_fields instead; see the note at the bottom of this function.
//
// Values already in the new form pass through untouched, so this is idempotent
// and costs a converted map nothing.
void rewrite_legacy_entity_properties(std::map<std::string, std::string> &properties)
{
  // 1. Component blobs.
  static constexpr const char *BLOB_KEYS[] = {"volume", "render", "hitbox", "material"};
  for (const char *blob_key : BLOB_KEYS)
  {
    auto it = properties.find(blob_key);
    if (it == properties.end() || it->second.find(':') == std::string::npos)
      continue;

    for (const auto &[inner_key, inner_value] : parse_legacy_component_blob(it->second))
      properties.emplace(std::string(blob_key) + "." + inner_key, inner_value);

    properties.erase(it);
  }

  // 2. Renamed keys. `action_name` held the string the server looked up in the
  //    trigger action registry; it is the Trigger_Action enum `action` now.
  static constexpr legacy_classname_t RENAMED_KEYS[] = {
      {"action_name", "action"},
  };
  for (const legacy_classname_t &rename : RENAMED_KEYS)
  {
    auto it = properties.find(rename.legacy);
    if (it == properties.end())
      continue;
    properties.emplace(rename.current, it->second);
    properties.erase(it);
  }

  // The third legacy form -- enum values spelled as lowercase strings -- is
  // deliberately NOT handled here. This function sees keys and text but no
  // field types, so it cannot tell an enum from a string, and blanket
  // title-casing turned the string field param_target_name "spawn_a" into
  // "Spawn_A". It lives in read_entity_fields, which has the field record.
}

// Reads a block's properties into an already-constructed entity.
//
// VERSIONING, which has no version numbers by design:
//   * a @Saveable key the file does not carry keeps the DSL default, so adding
//     a field is free and old maps load;
//   * a key the entity has no field for is ignored with a warning, so removing
//     a field is free and the warning says what was dropped;
//   * a key that does not parse is left at its default with an error, never
//     half-written and never silently zeroed.
// Renames are the one case that needs a real conversion, which is what the
// legacy table above is for.
void read_entity_fields(entities::Entity &entity, const std::string &classname,
                        std::map<std::string, std::string> properties)
{
  // Taken by value: the legacy rewrite below edits it, and the caller's block
  // is what the map file literally said. One conversion, in one place.
  rewrite_legacy_entity_properties(properties);

  const std::vector<entities::leaf_field_t> leaves =
      entities::collect_leaf_fields(entity.type, entities::FIELD_FLAG_SAVEABLE);

  uint8_t *base = reinterpret_cast<uint8_t *>(&entity);

  // Which keys were consumed, so the leftovers can be reported rather than
  // vanishing. Sized off the block, not the schema: an unknown key is exactly
  // what this is looking for.
  std::map<std::string, bool> consumed;
  for (const auto &[key, value] : properties)
    consumed[key] = false;

  for (const entities::leaf_field_t &leaf : leaves)
  {
    auto it = properties.find(leaf.name);
    if (it == properties.end())
      continue; // additive change: keep the DSL default

    consumed[leaf.name] = true;

    if (entities::field_from_text(it->second, *leaf.info, base + leaf.offset))
      continue;

    // Legacy value form, the second half of the one-time conversion the
    // classname aliases above are the first half of. Fields that are enums now
    // were plain ints in the macro system (spawn_type "0", light kind "2"), so
    // a pre-cutover map spells them as a NUMBER where field_from_text wants the
    // value's NAME. Accept the number, once, and the next save writes the name.
    //
    // Deliberately not in field_from_text: that is the canonical reader, and an
    // enum whose text is "2" is exactly the sort of thing it should reject.
    // This is map.cpp's business because only map.cpp is reading a file that
    // might predate the change.
    if (leaf.info->type == entities::FIELD_TYPE_ENUM)
    {
      const entities::enum_type_info_t &enum_info =
          entities::enum_info((entities::enum_type)leaf.info->enum_id);

      char *parse_end = nullptr;
      const long numeric = std::strtol(it->second.c_str(), &parse_end, 10);

      const bool parsed_whole = parse_end != nullptr && *parse_end == '\0' &&
                                parse_end != it->second.c_str();

      if (parsed_whole && numeric >= 0 && (uint32_t)numeric < enum_info.value_names.size())
      {
        base[leaf.offset] = (uint8_t)numeric;
        log_warning("map parse: {}.{} was the legacy numeric \"{}\"; read as {}. "
                    "The next save writes the name.",
                    classname, leaf.name, it->second,
                    enum_info.value_names[(uint32_t)numeric]);
        continue;
      }

      // Legacy form B: the value was a lowercase string ("on_enter",
      // "warp_to_spawn", "lit", "sphere") where the generated name is
      // Title_Case. Capitalise the first letter and each one after an
      // underscore, then look that up.
      std::string title_case = it->second;
      bool at_word_start = true;
      for (char &character : title_case)
      {
        if (at_word_start && character >= 'a' && character <= 'z')
          character = (char)(character - 'a' + 'A');
        at_word_start = (character == '_');
      }

      if (entities::field_from_text(title_case, *leaf.info, base + leaf.offset))
      {
        log_warning("map parse: {}.{} was the legacy spelling \"{}\"; read as {}. "
                    "The next save writes the name.",
                    classname, leaf.name, it->second, title_case);
        continue;
      }
    }

    log_error("map parse: {}.{} could not be read from \"{}\" — left at its default",
              classname, leaf.name, it->second);
  }

  for (const auto &[key, was_consumed] : consumed)
  {
    if (was_consumed || key == "classname" || key == "_uid")
      continue;

    // A warning, not an error: this is the documented behavior for a REMOVED
    // field, and removals are meant to be free. Pre-cutover maps all carry at
    // least one (entity_id, which the macro system saved because it saved every
    // field regardless of flags and is @Networked-only now), so treating it as
    // an error would bury the real errors in expected noise.
    log_warning("map parse: {} has no saveable field named \"{}\" — key ignored", classname,
                key);
  }
}

} // namespace

// ============================================================================
// Per-entity picking bounds
//
// One exhaustive switch over the closed enum, where this used to be a template
// whose primary was deliberately left undefined so a missing specialization
// became a link error. The switch is the better version of the same trick: an
// unhandled entity_type is a -Wswitch warning at COMPILE time, in this file,
// naming the type -- rather than an undefined symbol at link time.
// ============================================================================

namespace
{

// Mesh-bounds-or-default-box. Shared by every entity whose picking shape is
// "whatever the Render component's mesh says, with a small fallback if the mesh
// has not loaded yet".
aabb_bounds_t mesh_or_point_bounds(const entities::Entity *entity,
                                   float fallback_half = 0.5f)
{
  if (const entities::Render *render = entities::get_render(entity))
  {
    assets::asset_handle_t<assets::mesh_asset_t> mesh_handle =
        assets::get_mesh(render->mesh);

    if (mesh_handle.valid())
    {
      vec3f mesh_min, mesh_max;
      if (assets::compute_mesh_bounds(assets::get(mesh_handle), mesh_min, mesh_max))
      {
        vec3f mesh_center = (mesh_min + mesh_max) * 0.5f;
        vec3f mesh_half = (mesh_max - mesh_min) * 0.5f;
        vec3f s = render->scale;
        vec3f world_center =
            entity->position + vec3f{mesh_center.x * s.x, mesh_center.y * s.y,
                                     mesh_center.z * s.z};
        vec3f world_half = vec3f{mesh_half.x * s.x, mesh_half.y * s.y,
                                 mesh_half.z * s.z};
        return {world_center - world_half, world_center + world_half};
      }
    }
  }
  return {entity->position -
              vec3f{fallback_half, fallback_half, fallback_half},
          entity->position +
              vec3f{fallback_half, fallback_half, fallback_half}};
}

// The player hull, used by both player-shaped types.
aabb_bounds_t player_hull_bounds(const entities::Entity *entity)
{
  const vec3f hull{player_half_width, player_half_height, player_half_width};
  return {entity->position - hull, entity->position + hull};
}

} // namespace

aabb_bounds_t compute_entity_bounds(const entities::Entity *entity)
{
  if (!entity)
  {
    log_error("compute_entity_bounds called with null entity");
    return {{0, 0, 0}, {0, 0, 0}};
  }

  switch (entity->type)
  {
    case entities::entity_type::Player_Spawn_Entity:
      return player_hull_bounds(entity);

    case entities::entity_type::Player_Entity:
    {
      // Drawn as the pyramid marker, so it picks as one -- the Render component
      // is not what a player is drawn from.
      assets::asset_handle_t<assets::mesh_asset_t> mesh_handle =
          assets::get_mesh(entities::mesh_asset::Pyramid);
      if (mesh_handle.valid())
      {
        vec3f mesh_min, mesh_max;
        if (assets::compute_mesh_bounds(assets::get(mesh_handle), mesh_min, mesh_max))
          return {entity->position + mesh_min, entity->position + mesh_max};
      }
      return player_hull_bounds(entity);
    }

    case entities::entity_type::Trigger_Volume_Entity:
    {
      const entities::Box_Volume *volume = entities::get_box_volume(entity);
      assert(volume != nullptr && "Trigger_Volume_Entity lost its Box_Volume component");
      return get_bounds(*volume, entity->position);
    }

    case entities::entity_type::Weapon_Entity:
    case entities::entity_type::Rocket_Entity:
    case entities::entity_type::Particle_Emitter_Entity:
    case entities::entity_type::Light_Entity:
    case entities::entity_type::Physics_Body_Entity:
      return mesh_or_point_bounds(entity);

    case entities::entity_type::Invalid:
      break;
  }

  log_error("compute_entity_bounds: entity carries an invalid type tag ({})",
            (int)entity->type);
  return {entity->position - vec3f{0.5f, 0.5f, 0.5f},
          entity->position + vec3f{0.5f, 0.5f, 0.5f}};
}

std::vector<Plane> compute_entity_collision_planes(const entities::Entity *entity)
{
  // Entities are not collision geometry — that's the map's geometry list now
  // (see get_collision_planes in map_geometry.hpp). What's left here serves the
  // callers that want an entity's shape for overlap tests: a box volume if it
  // has one (trigger volumes), otherwise its picking bounds.
  if (const entities::Box_Volume *volume = entities::get_box_volume(entity))
    return compute_collision_planes(to_aabb(*volume, entity->position));

  auto bounds = compute_entity_bounds(entity);
  aabb_t t;
  t.center = (bounds.min + bounds.max) * 0.5f;
  t.half_extents = (bounds.max - bounds.min) * 0.5f;
  return compute_collision_planes(t);
}

// ============================================================================
// Uniform per-uid accessors over both regimes
// ============================================================================

aabb_bounds_t compute_object_bounds(const map_t &map, entity_uid_t uid)
{
  if (const map_geometry_t *entry = map.find_geometry_by_uid(uid))
    return get_bounds(entry->value);

  if (const map_entity_t *entry = map.find_by_uid(uid))
    return compute_entity_bounds(entry->entity.get());

  log_error("compute_object_bounds: no map object has uid {}", uid);
  return {{0, 0, 0}, {0, 0, 0}};
}

bool get_object_position(const map_t &map, entity_uid_t uid, linalg::vec3 &out_position)
{
  if (const map_geometry_t *entry = map.find_geometry_by_uid(uid))
  {
    out_position = get_position(entry->value);
    return true;
  }

  if (const map_entity_t *entry = map.find_by_uid(uid); entry && entry->entity)
  {
    out_position = entry->entity->position;
    return true;
  }

  return false;
}

bool get_object_box(const map_t &map, entity_uid_t uid, linalg::vec3 &out_center,
                    linalg::vec3 &out_half_extents)
{
  if (const map_geometry_t *entry = map.find_geometry_by_uid(uid))
  {
    switch (get_kind(entry->value))
    {
    case geometry_kind_t::Box:
    {
      const box_geometry_t &box = std::get<box_geometry_t>(entry->value);
      out_center = box.position;
      out_half_extents = box.half_extents;
      return true;
    }

    case geometry_kind_t::Displacement:
    {
      const displacement_geometry_t &displacement =
          std::get<displacement_geometry_t>(entry->value);
      out_center = displacement.position;
      out_half_extents = displacement.half_extents;
      return true;
    }

    case geometry_kind_t::Static_Mesh:
      return false; // sized by its mesh asset
    }

    return false;
  }

  if (const map_entity_t *entry = map.find_by_uid(uid); entry && entry->entity)
  {
    if (const entities::Box_Volume *volume = entities::get_box_volume(entry->entity.get()))
    {
      out_center = entry->entity->position + volume->position;
      out_half_extents = volume->half_extents;
      return true;
    }
  }

  return false;
}

bool set_object_box(map_t &map, entity_uid_t uid, const linalg::vec3 &center,
                    const linalg::vec3 &half_extents)
{
  if (map_geometry_t *entry = map.find_geometry_by_uid(uid))
  {
    switch (get_kind(entry->value))
    {
    case geometry_kind_t::Box:
    {
      box_geometry_t &box = std::get<box_geometry_t>(entry->value);
      box.position = center;
      box.half_extents = half_extents;
      return true;
    }

    case geometry_kind_t::Displacement:
    {
      displacement_geometry_t &displacement =
          std::get<displacement_geometry_t>(entry->value);
      displacement.position = center;
      displacement.half_extents = half_extents;
      return true;
    }

    case geometry_kind_t::Static_Mesh:
      return false;
    }

    return false;
  }

  if (map_entity_t *entry = map.find_by_uid(uid); entry && entry->entity)
  {
    if (entities::Box_Volume *volume = entities::get_box_volume(entry->entity.get()))
    {
      // The entity moves; the volume's local offset is left alone, so a box
      // authored off-center stays off-center when it is dragged.
      entry->entity->position = center - volume->position;
      volume->half_extents = half_extents;
      return true;
    }
  }

  return false;
}

std::vector<std::pair<entity_uid_t, aabb_bounds_t>>
collect_object_bounds(const map_t &map)
{
  std::vector<std::pair<entity_uid_t, aabb_bounds_t>> result;
  result.reserve(map.object_count());

  for (const map_geometry_t &entry : map.geometry)
    result.emplace_back(entry.uid, get_bounds(entry.value));

  for (const map_entity_t &entry : map.entities)
  {
    if (!entry.entity)
      continue;
    result.emplace_back(entry.uid, compute_entity_bounds(entry.entity.get()));
  }

  return result;
}

bool set_object_position(map_t &map, entity_uid_t uid, const linalg::vec3 &position)
{
  if (map_geometry_t *entry = map.find_geometry_by_uid(uid))
  {
    set_position(entry->value, position);
    return true;
  }

  if (map_entity_t *entry = map.find_by_uid(uid); entry && entry->entity)
  {
    entry->entity->position = position;
    return true;
  }

  return false;
}

std::vector<std::vector<linalg::vec3>> compute_entity_face_polygons(const entities::Entity *entity)
{
  // Any entity that owns a box volume -> 6 axis-aligned face quads
  if (const entities::Box_Volume *volume = entities::get_box_volume(entity))
    return compute_face_polygons(to_aabb(*volume, entity->position));

  // Fallback: use entity bounds as an AABB
  auto bounds = compute_entity_bounds(entity);
  aabb_t t;
  t.center = (bounds.min + bounds.max) * 0.5f;
  t.half_extents = (bounds.max - bounds.min) * 0.5f;
  return compute_face_polygons(t);
}

bool parse_map_from_string(const std::string &content, map_t &out_map)
{
  const std::vector<map_block_t> blocks = parse_map_content(content);
  out_map = {}; // Clear

  // "_uid" is written by both regimes; absent (a hand-authored map) means
  // auto-assign. Returns 0 for "not present".
  auto read_uid = [](const map_block_t &block) -> entity_uid_t
  {
    auto it = block.properties.find("_uid");
    if (it == block.properties.end())
      return 0;

    try
    {
      return (entity_uid_t)std::stoul(it->second);
    }
    catch (const std::exception &)
    {
      log_error("map parse: block \"{}\" has a malformed _uid \"{}\" — "
                "auto-assigning",
                block.keyword, it->second);
      return 0;
    }
  };

  for (const map_block_t &block : blocks)
  {
    const entity_uid_t uid = read_uid(block);

    // --- geometry blocks ---
    if (block.keyword != "entity")
    {
      geometry_value_t geometry;
      if (!parse_geometry(block.keyword, block.properties, geometry))
      {
        log_error("map parse: unknown block keyword \"{}\" — block skipped",
                  block.keyword);
        continue;
      }

      if (uid != 0)
        out_map.add_geometry_with_uid(uid, std::move(geometry));
      else
        out_map.add_geometry(std::move(geometry));
      continue;
    }

    // --- entity blocks ---
    auto classname_it = block.properties.find("classname");
    if (classname_it == block.properties.end())
    {
      log_error("map parse: entity block has no \"classname\" — block skipped");
      continue;
    }
    const std::string &classname = classname_it->second;

    if (classname == "worldspawn")
    {
      auto name_it = block.properties.find("name");
      if (name_it != block.properties.end())
        out_map.name = name_it->second;
      continue;
    }

    // Geometry written by a pre-exit build arrives as an entity block. Convert
    // it in place; the next save writes it as a geometry block and the legacy
    // form is gone from that file forever.
    {
      geometry_value_t geometry;
      if (convert_legacy_geometry_entity(classname, block.properties, geometry))
      {
        if (uid != 0)
          out_map.add_geometry_with_uid(uid, std::move(geometry));
        else
          out_map.add_geometry(std::move(geometry));
        continue;
      }
    }

    // Wedges were retired before the geometry exit and never came back — the
    // geometry kinds are box / static mesh / displacement. Drop them loudly;
    // they are real data being discarded.
    if (classname == "wedge_entity")
    {
      auto position_it = block.properties.find("position");
      log_error("map parse: dropped a retired wedge_entity at position \"{}\" — "
                "wedges are not a geometry kind; rebuild it from boxes",
                position_it == block.properties.end() ? "?" : position_it->second);
      continue;
    }

    std::shared_ptr<entities::Entity> new_entity = create_map_entity(classname);
    if (!new_entity)
    {
      log_error("map parse: unknown entity classname \"{}\" — entity skipped",
                classname);
      continue;
    }

    read_entity_fields(*new_entity, classname, block.properties);

    if (uid != 0)
      out_map.add_entity_with_uid(uid, new_entity);
    else
      out_map.add_entity(new_entity);
  }

  return true;
}

bool load_map(const std::string &filename, map_t &out_map)
{
  std::ifstream in(filename);
  if (!in.is_open())
  {
    return false;
  }

  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string content = buffer.str();
  in.close();

  if (!parse_map_from_string(content, out_map))
    return false;

  load_navmesh(filename, out_map.navmesh);

  return true;
}

std::string serialize_map_to_string(const map_t &map)
{
  std::vector<map_block_out_t> blocks;
  blocks.reserve(map.object_count() + 1);

  // Worldspawn — the map's own properties, not an entity in either list.
  {
    map_block_out_t worldspawn;
    worldspawn.keyword = "entity";
    worldspawn.properties.emplace_back("classname", "worldspawn");
    worldspawn.properties.emplace_back("name", map.name);
    blocks.push_back(std::move(worldspawn));
  }

  // Geometry first, so a level's structure reads before its props.
  for (const map_geometry_t &entry : map.geometry)
  {
    map_block_out_t block;
    block.properties.emplace_back("_uid", std::to_string(entry.uid));
    serialize_geometry(entry.value, block.keyword, block.properties);
    blocks.push_back(std::move(block));
  }

  // Entities, from the generated tables: every @Saveable leaf, in DECLARATION
  // order. That order is the point — the std::map this replaced sorted keys
  // alphabetically, so a field's position in the file had nothing to do with
  // where it sits in entities.def, and reordering the .def produced no diff
  // while renaming a field reshuffled the whole block.
  for (const map_entity_t &entry : map.entities)
  {
    if (!entry.entity)
      continue;

    const entities::Entity *entity = entry.entity.get();
    if (entity->type == entities::entity_type::Invalid)
    {
      log_error("map save: entity uid {} carries an invalid type tag — dropped", entry.uid);
      continue;
    }

    map_block_out_t block;
    block.keyword = "entity";
    block.properties.emplace_back("classname", entities::classname_of(entity));
    block.properties.emplace_back("_uid", std::to_string(entry.uid));

    const uint8_t *base = reinterpret_cast<const uint8_t *>(entity);

    for (const entities::leaf_field_t &leaf :
         entities::collect_leaf_fields(entity->type, entities::FIELD_FLAG_SAVEABLE))
    {
      std::string value;
      if (!entities::field_to_text(base + leaf.offset, *leaf.info, value))
      {
        log_error("map save: entity uid {} field {}.{} could not be written as text — "
                  "the key is omitted and the value will be lost on the next load",
                  entry.uid, entities::classname_of(entity), leaf.name);
        continue;
      }
      block.properties.emplace_back(leaf.name, value);
    }

    blocks.push_back(std::move(block));
  }

  return serialize_map_blocks(blocks);
}

bool save_map(const std::string &filename, const map_t &map)
{
  std::string content = serialize_map_to_string(map);
  std::ofstream out(filename);
  if (!out.is_open())
    return false;
  out << content;
  out.close();

  if (map.navmesh.valid())
    save_navmesh(filename, map.navmesh);

  return true;
}

std::string resolve_map_path(const std::string &maps_dir,
                             const std::string &identifier)
{
  // Only the basename of the identifier matters — the directory part (if any) is
  // replaced by maps_dir. So a server's maps-relative wire id ("new_map.source")
  // and a last_map.txt entry (an absolute path) both resolve the same way, into
  // whatever maps_dir this process was told to use.
  std::filesystem::path leaf = std::filesystem::path(identifier).filename();
  return (std::filesystem::path(maps_dir) / leaf).generic_string();
}

bool save_navmesh_sidecar(const std::string &map_path, const navmesh_t &nav)
{
  if (!nav.valid())
    return false;
  save_navmesh(map_path, nav);
  return true;
}

uint32_t compute_map_content_hash(const map_t &map)
{
  const std::string content = serialize_map_to_string(map);

  uint32_t hash = 2166136261u; // FNV-1a offset basis
  for (unsigned char byte : content)
  {
    hash ^= byte;
    hash *= 16777619u; // FNV prime
  }
  return hash;
}

} // namespace shared
