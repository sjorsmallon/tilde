#include "map.hpp"
#include "entity_system.hpp" // For classname_to_type, type_to_classname
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
    }
    else if (ent.classname == "aabb")
    {
      aabb_t aabb;
      // center
      if (ent.properties.count("center"))
      {
        std::stringstream ss(ent.properties.at("center"));
        ss >> aabb.center.x >> aabb.center.y >> aabb.center.z;
      }
      // half_extents
      if (ent.properties.count("half_extents"))
      {
        std::stringstream ss(ent.properties.at("half_extents"));
        ss >> aabb.half_extents.x >> aabb.half_extents.y >> aabb.half_extents.z;
      }
      out_map.aabbs.push_back(aabb);
    }
    else
    {
      // Try entity factory
      // Note: we assume classname_to_type is updated to return
      // shared::entity_type
      entity_type type = shared::classname_to_type(ent.classname);
      bool is_generic_spawn = (ent.classname == "entity_spawn");

      if (type != entity_type::UNKNOWN || is_generic_spawn)
      {
        entity_spawn_t spawn;
        spawn.type = type;
        spawn.properties = ent.properties;

        // Override type if explicit property exists
        if (ent.properties.count("type"))
        {
          try
          {
            int t = std::stoi(ent.properties.at("type"));
            spawn.type = static_cast<entity_type>(t);
          }
          catch (...)
          {
          }
        }

        if (ent.properties.count("origin"))
        {
          std::stringstream ss(ent.properties.at("origin"));
          ss >> spawn.position.x >> spawn.position.y >> spawn.position.z;
        }

        if (ent.properties.count("yaw"))
        {
          try
          {
            spawn.yaw = std::stof(ent.properties.at("yaw"));
          }
          catch (...)
          {
            spawn.yaw = 0;
          }
        }
        out_map.entities.push_back(spawn);
      }
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

  // AABBs
  for (const auto &aabb : map.aabbs)
  {
    map_entity_def_t def;
    def.classname = "aabb";
    std::stringstream ss;
    ss << aabb.center.x << " " << aabb.center.y << " " << aabb.center.z;
    def.properties["center"] = ss.str();

    ss.str("");
    ss << aabb.half_extents.x << " " << aabb.half_extents.y << " "
       << aabb.half_extents.z;
    def.properties["half_extents"] = ss.str();
    entities.push_back(def);
  }

  // Entities
  for (const auto &ent : map.entities)
  {
    map_entity_def_t def;
    def.classname = shared::type_to_classname(ent.type);

    // Copy properties first
    def.properties = ent.properties;

    // If it's a generic connection or unhandled type, save the type ID
    if (def.classname == "entity_spawn" || ent.type == entity_type::UNKNOWN)
    {
      def.properties["type"] = std::to_string(static_cast<int>(ent.type));
    }

    std::stringstream ss;
    ss << ent.position.x << " " << ent.position.y << " " << ent.position.z;
    def.properties["origin"] = ss.str();

    def.properties["yaw"] = std::to_string(ent.yaw);
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
