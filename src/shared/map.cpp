#define ENTITIES_WANT_INCLUDES
#include "map.hpp"
#include "asset.hpp"
#include "entities/player_entity.hpp"
#include "entities/static_entities.hpp"
#include "entities/displacement_entity.hpp"
#include "entities/trigger_volume_entity.hpp"
#include "entity_system.hpp"
#include "log.hpp"
#include <cstdint>
#include <cstring>
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



struct map_entity_def_t
{
  std::string classname;
  std::map<std::string, std::string> properties;
};

std::vector<map_entity_def_t> parse_map_content(const std::string &content)
{
  std::vector<map_entity_def_t> entities;
  std::stringstream ss(content);
  std::string token;

  while (ss >> token)
  {
    if (token == "entity")
    {
      std::string brace;
      ss >> brace;
      if (brace == "{")
      {
        map_entity_def_t file_ent;
        while (ss >> token)
        {
          if (token == "}")
          {
            break;
          }
          // Expecting "key" "value"
          std::string key = token;
          if (key.size() >= 2 && key.front() == '"' && key.back() == '"')
          {
            key = key.substr(1, key.size() - 2);
          }

          std::string value;
          ss >> value;
          // Values can have spaces if quoted "0 0 0"
          if (value.front() == '"')
          {
            // If it doesn't end with quote, read until it does
            while (value.back() != '"' && !ss.eof())
            {
              std::string part;
              ss >> part;
              value += " " + part;
            }
            if (value.size() >= 2)
              value = value.substr(1, value.size() - 2);
          }

          if (key == "classname")
          {
            file_ent.classname = value;
          }
          else
          {
            file_ent.properties[key] = value;
          }
        }
        entities.push_back(file_ent);
      }
    }
  }
  return entities;
}

std::string
serialize_map_entities(const std::vector<map_entity_def_t> &entities)
{
  std::stringstream ss;
  for (const auto &ent : entities)
  {
    ss << "entity\n{\n";
    ss << "  \"classname\" \"" << ent.classname << "\"\n";
    for (const auto &[key, value] : ent.properties)
    {
      ss << "  \"" << key << "\" \"" << value << "\"\n";
    }
    ss << "}\n";
  }
  return ss.str();
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
aabb_bounds_t compute_picking_bounds_for(const network::AABB_Entity *e)
{
  return get_bounds(e->volume, e->position);
}

template <>
aabb_bounds_t compute_picking_bounds_for(const network::Wedge_Entity *e)
{
  wedge_t t;
  t.center = e->position;
  t.half_extents = e->half_extents;
  t.orientation = e->orientation;
  return get_bounds(t);
}

template <>
aabb_bounds_t compute_picking_bounds_for(const network::Static_Mesh_Entity *e)
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
compute_picking_bounds_for(const network::Displacement_Entity *e)
{
  return get_bounds(e->volume, e->position);
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
  // regardless of concrete class. Per-class specializations below remain only
  // for non-box entities (mesh, wedge, player, ...).
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
  // Any entity that owns a box volume -> 6 axis-aligned planes.
  //
  // TODO(displacement-collision): Displacement_Entity also flows through this
  // branch because it owns a box_volume_t, so its in-game collision is a flat
  // axis-aligned box rather than its actual heightmap surface. Players will
  // walk on an invisible lid above the terrain. Fix is to either give
  // Displacement its own compute_entity_collision_planes path (slice the
  // heightmap into per-quad planes) or wire it through a future
  // mesh_volume_t / triangle collision path.
  if (const auto *volume = entity->get_box_volume())
    return compute_collision_planes(to_aabb(*volume, entity->position));

  // Wedge entity -> 5 planes (including slope)
  if (auto *wedge = dynamic_cast<const network::Wedge_Entity *>(entity))
  {
    wedge_t t;
    t.center = wedge->position;
    t.half_extents = wedge->half_extents;
    t.orientation = wedge->orientation;
    return compute_collision_planes(t);
  }

  // Fallback: use entity bounds as an AABB
  auto bounds = compute_entity_bounds(entity);
  aabb_t t;
  t.center = (bounds.min + bounds.max) * 0.5f;
  t.half_extents = (bounds.max - bounds.min) * 0.5f;
  return compute_collision_planes(t);
}

std::vector<std::vector<linalg::vec3>> compute_entity_face_polygons(const network::Entity *entity)
{
  // Any entity that owns a box volume -> 6 axis-aligned face quads
  if (const auto *volume = entity->get_box_volume())
    return compute_face_polygons(to_aabb(*volume, entity->position));

  if (auto *wedge = dynamic_cast<const network::Wedge_Entity *>(entity))
  {
    wedge_t t;
    t.center = wedge->position;
    t.half_extents = wedge->half_extents;
    t.orientation = wedge->orientation;
    return compute_face_polygons(t);
  }

  // Fallback: use entity bounds as an AABB
  auto bounds = compute_entity_bounds(entity);
  aabb_t t;
  t.center = (bounds.min + bounds.max) * 0.5f;
  t.half_extents = (bounds.max - bounds.min) * 0.5f;
  return compute_face_polygons(t);
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

  auto entities = parse_map_content(content);
  out_map = {}; // Clear

  for (const auto &ent : entities)
  {
    if (ent.classname == "worldspawn")
    {
      if (ent.properties.count("name"))
      {
        out_map.name = ent.properties.at("name");
      }
      continue;
    }

    // Wedge entities are being retired pending a follow-up that gives them a
    // proper wedge_volume_t component (parallel to the box_volume_t refactor).
    // Strip them on load so existing maps clean themselves on the next save.
    // Non-silent by design: wedges are real data being discarded.
    if (ent.classname == "wedge_entity")
    {
      const std::string &pos = ent.properties.count("position")
                                   ? ent.properties.at("position")
                                   : (ent.properties.count("center")
                                          ? ent.properties.at("center")
                                          : std::string("?"));
      printf("Info: stripped wedge entity from %s at position %s "
             "(deferred refactor)\n",
             filename.c_str(), pos.c_str());
      continue;
    }

    auto new_entity = create_entity_by_classname(ent.classname);
    if (new_entity)
    {
      new_entity->init_from_map(ent.properties);

      // Restore uid from file if present, otherwise auto-assign
      if (ent.properties.count("_uid"))
      {
        entity_uid_t uid =
            (entity_uid_t)std::stoul(ent.properties.at("_uid"));
        out_map.add_entity_with_uid(uid, new_entity);
      }
      else
      {
        out_map.add_entity(new_entity);
      }
    }
    else
    {
      printf("Warning: Unknown entity classname: %s\n", ent.classname.c_str());
    }
  }

  load_navmesh(filename, out_map.navmesh);

  return true;
}

bool save_map(const std::string &filename, const map_t &map)
{
  std::vector<map_entity_def_t> entities;

  // Worldspawn
  {
    map_entity_def_t world;
    world.classname = "worldspawn";
    world.properties["name"] = map.name;
    entities.push_back(world);
  }

  // Entities
  for (const auto &entry : map.entities)
  {
    if (!entry.entity)
      continue;

    map_entity_def_t def;
    def.classname = get_classname_for_entity(entry.entity.get());

    if (def.classname == "unknown")
      continue;

    // Save uid
    def.properties["_uid"] = std::to_string(entry.uid);

    // Serialize properties using Schema
    const auto *schema = entry.entity->get_schema();
    if (schema)
    {
      const uint8_t *base_ptr =
          reinterpret_cast<const uint8_t *>(entry.entity.get());

      for (const auto &field : schema->fields)
      {
        std::string value;
        if (network::serialize_field_to_string(base_ptr + field.offset,
                                               field, value))
        {
          def.properties[field.name] = value;
        }
      }
    }

    entities.push_back(def);
  }

  std::string content = serialize_map_entities(entities);
  std::ofstream out(filename);
  if (!out.is_open())
    return false;
  out << content;
  out.close();

  if (map.navmesh.valid())
    save_navmesh(filename, map.navmesh);

  return true;
}

bool save_navmesh_sidecar(const std::string &map_path, const navmesh_t &nav)
{
  if (!nav.valid())
    return false;
  save_navmesh(map_path, nav);
  return true;
}

} // namespace shared
