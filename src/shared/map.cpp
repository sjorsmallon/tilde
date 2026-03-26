#define ENTITIES_WANT_INCLUDES
#include "map.hpp"
#include "asset.hpp"
#include "entities/player_entity.hpp"
#include "entities/static_entities.hpp"
#include "entities/displacement_entity.hpp"
#include "entity_system.hpp"
#include <cstdint>
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

aabb_bounds_t compute_entity_bounds(const network::Entity *entity)
{
  // 1. Check for mesh bounds via render component
  if (const auto *rc = entity->get_component<network::render_component_t>())
  {
    if (rc->mesh_id >= 0)
    {
      const char *mesh_path = assets::get_mesh_path(rc->mesh_id);
      if (mesh_path)
      {
        auto mesh_handle = assets::load_mesh(mesh_path);
        if (mesh_handle.valid())
        {
          vec3f mesh_min, mesh_max;
          if (assets::compute_mesh_bounds(assets::get(mesh_handle),
                                          mesh_min, mesh_max))
          {
            vec3f mesh_center = (mesh_min + mesh_max) * 0.5f;
            vec3f mesh_half = (mesh_max - mesh_min) * 0.5f;
            vec3f s = rc->scale;
            vec3f world_center = entity->position +
                vec3f{mesh_center.x * s.x, mesh_center.y * s.y,
                      mesh_center.z * s.z};
            vec3f world_half =
                vec3f{mesh_half.x * s.x, mesh_half.y * s.y,
                      mesh_half.z * s.z};
            return {world_center - world_half, world_center + world_half};
          }
        }
      }
    }
  }

  // 2. Check for AABB entity shape
  if (auto *aabb = dynamic_cast<const network::AABB_Entity *>(entity))
  {
    aabb_t t;
    t.center = aabb->position;
    t.half_extents = aabb->half_extents;
    return get_bounds(t);
  }

  // 2b. Check for Displacement entity shape
  if (auto *disp = dynamic_cast<const network::Displacement_Entity *>(entity))
  {
    aabb_t t;
    t.center = disp->position;
    t.half_extents = disp->half_extents;
    return get_bounds(t);
  }

  // 3. Check for Wedge entity shape
  if (auto *wedge = dynamic_cast<const network::Wedge_Entity *>(entity))
  {
    wedge_t t;
    t.center = wedge->position;
    t.half_extents = wedge->half_extents;
    t.orientation = wedge->orientation;
    return get_bounds(t);
  }

  // 4. Player spawn: use player hull dimensions for picking
  if (dynamic_cast<const network::Player_Spawn_Entity *>(entity))
  {
    const vec3f hull{network::player_half_width, network::player_half_height, network::player_half_width};
    return {entity->position - hull, entity->position + hull};
  }

  // 5. Default: 0.5 unit box at entity position
  return {entity->position - vec3f{0.5f, 0.5f, 0.5f},
          entity->position + vec3f{0.5f, 0.5f, 0.5f}};
}

std::vector<Plane> compute_entity_collision_planes(const network::Entity *entity)
{
  // AABB entity -> 6 axis-aligned planes
  if (auto *aabb = dynamic_cast<const network::AABB_Entity *>(entity))
  {
    aabb_t t;
    t.center = aabb->position;
    t.half_extents = aabb->half_extents;
    return compute_collision_planes(t);
  }

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
  if (auto *aabb = dynamic_cast<const network::AABB_Entity *>(entity))
  {
    aabb_t t;
    t.center = aabb->position;
    t.half_extents = aabb->half_extents;
    return compute_face_polygons(t);
  }

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
