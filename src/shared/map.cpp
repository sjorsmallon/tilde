#define ENTITIES_WANT_INCLUDES
#include "map.hpp"
#include "asset.hpp"
#include "entities/static_entities.hpp"
#include "entity_system.hpp"
#include <fstream>
#include <sstream>

namespace shared
{

namespace
{

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

  // 3. Check for Wedge entity shape
  if (auto *wedge = dynamic_cast<const network::Wedge_Entity *>(entity))
  {
    wedge_t t;
    t.center = wedge->position;
    t.half_extents = wedge->half_extents;
    t.orientation = wedge->orientation;
    return get_bounds(t);
  }

  // 4. Default: 0.5 unit box at entity position
  return {entity->position - vec3f{0.5f, 0.5f, 0.5f},
          entity->position + vec3f{0.5f, 0.5f, 0.5f}};
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
                                               field.type, value))
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
  return true;
}

} // namespace shared
