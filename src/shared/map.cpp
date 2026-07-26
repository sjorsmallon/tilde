#define ENTITIES_WANT_INCLUDES
#include "map.hpp"
#include "asset.hpp"
#include "entities/player_entity.hpp"
#include "entities/trigger_volume_entity.hpp"
#include "entity_system.hpp"
#include "log.hpp"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <typeinfo>

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
    uint32_t n = (uint32_t)p.verts.size();
    write(n);
    for (uint32_t k = 0; k < n; ++k) write(p.verts[k]);
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
    p.verts.resize(n);
    p.neighbors.resize(n);
    for (uint32_t k = 0; k < n; ++k) read(p.verts[k]);
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

} // namespace

// ============================================================================
// Per-entity picking bounds
//
// The primary template is intentionally NOT defined. If a new entity type is
// added to SHARED_ENTITIES_LIST and the dev forgets to specialize this here,
// the linker will fail with "undefined reference to
// compute_picking_bounds_for<Foo_Entity>". That's the point -- forces every
// entity type to declare its picking shape rather than silently falling back
// to a tiny default box that makes selection effectively impossible.
//
// Mirrors the Entity_Editor_Traits pattern at
// src/client/editor/entity_editor_traits.hpp.
// ============================================================================

template <typename EntityClass>
aabb_bounds_t compute_picking_bounds_for(const EntityClass *e);

namespace
{
// Mesh-bounds-or-default-box. Shared helper for entities whose picking shape
// is "whatever the render_component's mesh tells us, with a small fallback if
// the mesh hasn't loaded yet".
aabb_bounds_t mesh_or_point_bounds(const network::Entity *entity,
                                   float fallback_half = 0.5f)
{
  if (const auto *rc = entity->get_component<network::render_component_t>())
  {
    const char *mesh_path =
        rc->mesh_path.length > 0 ? rc->mesh_path.c_str() : nullptr;
    if (mesh_path)
    {
      assets::asset_handle_t<assets::mesh_asset_t> mesh_handle;
      if (std::strncmp(mesh_path, "__primitive_", 12) == 0)
        mesh_handle = assets::get_primitive_mesh(mesh_path + 12);
      else
        mesh_handle = assets::load_mesh(mesh_path);

      if (mesh_handle.valid())
      {
        vec3f mesh_min, mesh_max;
        if (assets::compute_mesh_bounds(assets::get(mesh_handle), mesh_min,
                                        mesh_max))
        {
          vec3f mesh_center = (mesh_min + mesh_max) * 0.5f;
          vec3f mesh_half = (mesh_max - mesh_min) * 0.5f;
          vec3f s = rc->scale;
          vec3f world_center =
              entity->position + vec3f{mesh_center.x * s.x, mesh_center.y * s.y,
                                       mesh_center.z * s.z};
          vec3f world_half = vec3f{mesh_half.x * s.x, mesh_half.y * s.y,
                                   mesh_half.z * s.z};
          return {world_center - world_half, world_center + world_half};
        }
      }
    }
  }
  return {entity->position -
              vec3f{fallback_half, fallback_half, fallback_half},
          entity->position +
              vec3f{fallback_half, fallback_half, fallback_half}};
}
} // namespace

template <>
aabb_bounds_t compute_picking_bounds_for(const network::Player_Spawn_Entity *e)
{
  const vec3f hull{network::player_half_width, network::player_half_height,
                   network::player_half_width};
  return {e->position - hull, e->position + hull};
}

template <>
aabb_bounds_t compute_picking_bounds_for(const network::Player_Entity *e)
{
  auto mesh_handle = assets::load_mesh("resources/obj/pyramid.obj");
  if (mesh_handle.valid())
  {
    vec3f mesh_min, mesh_max;
    if (assets::compute_mesh_bounds(assets::get(mesh_handle), mesh_min,
                                    mesh_max))
      return {e->position + mesh_min, e->position + mesh_max};
  }
  const vec3f hull{network::player_half_width, network::player_half_height,
                   network::player_half_width};
  return {e->position - hull, e->position + hull};
}

template <>
aabb_bounds_t compute_picking_bounds_for(const network::Weapon_Entity *e)
{
  return mesh_or_point_bounds(e);
}

template <>
aabb_bounds_t compute_picking_bounds_for(const network::Rocket_Entity *e)
{
  return mesh_or_point_bounds(e);
}

template <>
aabb_bounds_t
compute_picking_bounds_for(const network::Particle_Emitter_Entity *e)
{
  return mesh_or_point_bounds(e);
}

template <>
aabb_bounds_t
compute_picking_bounds_for(const network::Trigger_Volume_Entity *e)
{
  return get_bounds(e->volume, e->position);
}

template <>
aabb_bounds_t compute_picking_bounds_for(const network::Light_Entity *e)
{
  return mesh_or_point_bounds(e);
}

template <>
aabb_bounds_t
compute_picking_bounds_for(const network::Physics_Body_Entity *e)
{
  return mesh_or_point_bounds(e);
}

aabb_bounds_t compute_entity_bounds(const network::Entity *entity)
{
  if (!entity)
  {
    log_error("compute_entity_bounds called with null entity");
    return {{0, 0, 0}, {0, 0, 0}};
  }

  // Generic fast-path: any entity that owns a box volume picks through it,
  // regardless of concrete class. Trigger volumes are the only such entity left
  // now that geometry is out; the per-class specializations above cover the rest.
  if (const auto *volume = entity->get_box_volume())
    return get_bounds(*volume, entity->position);

#define X(ENUM, CLASS, NAME, PATH)                                             \
  if (auto *casted = dynamic_cast<const CLASS *>(entity))                      \
    return compute_picking_bounds_for<CLASS>(casted);
  SHARED_ENTITIES_LIST(X)
#undef X

  // Unreachable if every entity type in SHARED_ENTITIES_LIST is handled above
  // (which the linker enforces). Reaching here means something derived from
  // network::Entity exists outside the X-macro -- a real bug.
  log_error("compute_entity_bounds: entity not in SHARED_ENTITIES_LIST "
            "(typeid={}); register it via the X-macro in entity_list.hpp",
            typeid(*entity).name());
  return {entity->position - vec3f{0.5f, 0.5f, 0.5f},
          entity->position + vec3f{0.5f, 0.5f, 0.5f}};
}

std::vector<Plane> compute_entity_collision_planes(const network::Entity *entity)
{
  // Entities are not collision geometry — that's the map's geometry list now
  // (see get_collision_planes in map_geometry.hpp). What's left here serves the
  // callers that want an entity's shape for overlap tests: a box volume if it
  // has one (trigger volumes), otherwise its picking bounds.
  if (const auto *volume = entity->get_box_volume())
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
    if (const box_volume_t *volume = entry->entity->get_box_volume())
    {
      out_center = entry->entity->position;
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
    if (box_volume_t *volume = entry->entity->get_box_volume())
    {
      entry->entity->position = center;
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

std::vector<std::vector<linalg::vec3>> compute_entity_face_polygons(const network::Entity *entity)
{
  // Any entity that owns a box volume -> 6 axis-aligned face quads
  if (const auto *volume = entity->get_box_volume())
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

    std::shared_ptr<network::Entity> new_entity = create_entity_by_classname(classname);
    if (!new_entity)
    {
      log_error("map parse: unknown entity classname \"{}\" — entity skipped",
                classname);
      continue;
    }

    new_entity->init_from_map(block.properties);

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

  // Entities, still schema-driven (and so still alphabetically ordered) until
  // the generator cutover takes over map save/load.
  for (const map_entity_t &entry : map.entities)
  {
    if (!entry.entity)
      continue;

    const std::string classname = get_classname_for_entity(entry.entity.get());
    if (classname == "unknown")
    {
      log_error("map save: entity uid {} has no registered classname — dropped",
                entry.uid);
      continue;
    }

    map_block_out_t block;
    block.keyword = "entity";
    block.properties.emplace_back("classname", classname);
    block.properties.emplace_back("_uid", std::to_string(entry.uid));

    const network::Class_Schema *schema = entry.entity->get_schema();
    if (schema)
    {
      const uint8_t *base_ptr = reinterpret_cast<const uint8_t *>(entry.entity.get());

      for (const auto &field : schema->fields)
      {
        std::string value;
        if (network::serialize_field_to_string(base_ptr + field.offset, field, value))
          block.properties.emplace_back(field.name, value);
      }
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
