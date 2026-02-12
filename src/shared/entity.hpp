#pragma once

#include "network/bitstream.hpp"
#include "network/network_types.hpp"
#include "network/schema.hpp"
#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <string>

namespace network
{

struct Entity_Id
{
  uint32 index;
  uint32 generation;

  bool operator==(const Entity_Id &) const = default;
};

inline constexpr Entity_Id null_entity_id = {0, 0};

class Entity
{
public:
  Entity_Id id = null_entity_id;
  SCHEMA_FIELD(vec3f, position,
               Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(vec3f, orientation,
               Schema_Flags::Networked | Schema_Flags::Editable);

  virtual ~Entity() = default;

  // Macro required in every derived class to register schema
  virtual const Class_Schema *get_schema() const = 0;

  virtual void init_from_map(const std::map<std::string, std::string> &props)
  {
    const Class_Schema *schema = get_schema();
    if (!schema)
      return;

    uint8 *current_base = reinterpret_cast<uint8 *>(this);

    for (const auto &[key, value] : props)
    {
      for (const auto &field : schema->fields)
      {
        if (field.name == key)
        {
          parse_string_to_field(value, field.type, current_base + field.offset);
        }
      }
    }
  }

  // Returns all properties as a map of strings (for saving/snapshots)
  virtual std::map<std::string, std::string> get_all_properties() const;

  // Writes the entity state to the stream.
  // If baseline is provided, it only writes changes relative to baseline.
  // If baseline is null, it writes everything (full update).
  void serialize(Bit_Writer &writer, const Entity *baseline) const;
  void deserialize(Bit_Reader &reader);
};

struct Entity_Delta
{
  uint32_t entity_id;   // WHICH object is this? (e.g., Player #42)
  uint16_t entity_type; // WHAT is it? (e.g., ET_Player -> uses Player Schema)
  std::vector<network::Field_Update> updates;
};

struct Entity_Update_Header
{
  uint32_t ent_id;
  uint16_t ent_type;
  uint8_t field_count;
};

} // namespace network

namespace shared
{
// Factory helpers
std::shared_ptr<network::Entity>
create_entity_by_classname(const std::string &classname);

std::string get_classname_for_entity(const network::Entity *entity);
} // namespace shared
