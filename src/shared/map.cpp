#define ENTITIES_WANT_INCLUDES
#include "map.hpp"
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

      // Create entity_placement_t wrapper
      entity_placement_t placement;
      placement.entity = new_entity;

      // Extract position from entity (different entities store position differently)
      placement.position = new_entity->position; // Use base Entity position

      // Extract scale if available (for now default to 1,1,1)
      placement.scale = {1, 1, 1};
      placement.rotation = {0, 0, 0};

      out_map.entities.push_back(placement);
    }
    else
    {
      // TODO: Handle unknown entities or generic spawns?
      // For now, if we can't create it, we skip it or maybe we need a
      // GenericEntity? logic in previous map.cpp handled "entity_spawn" as
      // generic. But we are unifying. If it's not in the list, we can't really
      // use it? Warning?
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
  for (const auto &placement : map.entities)
  {
    if (!placement.entity)
      continue;

    auto &ent = placement.entity;

    // Sync placement position back to entity before saving
    ent->position = placement.position;

    map_entity_def_t def;
    def.classname = get_classname_for_entity(ent.get());

    if (def.classname == "unknown")
      continue;

    // Serialize properties using Schema
    const auto *schema = ent->get_schema();
    if (schema)
    {
      // We need to access the entity data.
      // The schema offsets are relative to the entity pointer.
      const uint8_t *base_ptr = reinterpret_cast<const uint8_t *>(ent.get());

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

    // TODO: What about properties NOT in the schema?
    // The Entity::init_from_map only reads schema fields.
    // If we want to preserve other properties (like target/targetname if they
    // aren't in schema), we would need a generic property bag in Entity. For
    // now, we assume Schema covers everything we need to save.

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
