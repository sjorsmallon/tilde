#pragma once

#include "network/bitstream.hpp"
#include "network/network_types.hpp"
#include "network/schema.hpp"
#include "components/components.hpp"
#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <string>

namespace network
{

inline constexpr uint64 null_entity_id = 0;

class Entity
{
public:
  SCHEMA_FIELD(uint64, entity_id, Schema_Flags::Networked);
  SCHEMA_FIELD(vec3f, position,
               Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(vec3f, orientation,
               Schema_Flags::Networked | Schema_Flags::Editable);

  virtual ~Entity() = default;

  // Returns true if this entity type contributes to the static collision BVH.
  // Static geometry types (AABB, Wedge, StaticMesh) override this to return true.
  // All other entities go into entity_system at session init.
  virtual bool is_collision_geometry() const { return false; }

  // Register the Entity base class schema (called on-demand by derived schemas)
  static void register_schema();

  // Macro required in every derived class to register schema
  virtual const Class_Schema *get_schema() const = 0;

  // Look up a component by type using the schema system.
  // Returns nullptr if the entity doesn't have a field of type T.
  template <typename T>
  const T *get_component() const
  {
    const Class_Schema *schema = get_schema();
    if (!schema) return nullptr;
    constexpr Field_Type expected = Schema_Type_Info<T>::type;
    for (const auto &field : schema->fields)
    {
      if (field.type == expected)
        return reinterpret_cast<const T *>(
            reinterpret_cast<const uint8_t *>(this) + field.offset);
    }
    return nullptr;
  }

  template <typename T>
  T *get_component()
  {
    return const_cast<T *>(
        const_cast<const Entity *>(this)->get_component<T>());
  }

  virtual void init_from_map(const std::map<std::string, std::string> &props)
  {
    const Class_Schema *schema = get_schema();
    if (!schema)
      return;

    uint8 *current_base = reinterpret_cast<uint8 *>(this);

    for (const auto &[key, value] : props)
    {
      // Backward compat: old maps store "center" for AABB/Wedge entities,
      // now consolidated into the inherited "position" field.
      std::string field_name = key;
      if (key == "center" && !props.count("position"))
        field_name = "position";

      for (const auto &field : schema->fields)
      {
        if (field.name == field_name)
        {
          // Use recursive version to handle nested schemas
          parse_string_to_field_recursive(value, field, current_base + field.offset);
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
