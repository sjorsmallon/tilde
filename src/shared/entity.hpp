#pragma once

#include "network/bitstream.hpp"
#include "network/network_types.hpp"
#include "network/schema.hpp"
#include "components/components.hpp"
#include "entities/entity_type.hpp"
#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <string>

namespace shared
{
// Forward decl so Entity can expose get_box_volume() without pulling in shapes.hpp.
struct box_volume_t;
} // namespace shared

namespace network
{

class Entity
{
public:
  SCHEMA_FIELD(uint32, entity_id, Schema_Flags::Networked);
  SCHEMA_FIELD(vec3f, position,
               Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(vec3f, orientation,
               Schema_Flags::Networked | Schema_Flags::Editable);

  virtual ~Entity() = default;

  // Returns true if this entity type contributes to the static collision BVH.
  // Static geometry types (AABB, Wedge, StaticMesh) override this to return true.
  // All other entities go into entity_system at session init.
  virtual bool is_collision_geometry() const { return false; }

  // Box-shaped entities (AABB, Trigger_Volume, Displacement, ...) override this
  // to return a pointer to their owned box_volume_t. Editor tools dispatch on
  // this instead of dynamic_cast<concrete_entity>, so any entity that grows a
  // box volume becomes sculptable/pickable through the same code path.
  virtual shared::box_volume_t *get_box_volume() { return nullptr; }
  virtual const shared::box_volume_t *get_box_volume() const { return nullptr; }

  // Register the Entity base class schema (called on-demand by derived schemas)
  static void register_schema();

  // Macro required in every derived class to register schema
  virtual const Class_Schema *get_schema() const = 0;

  // Returns the closed-enum tag for this entity's concrete type.
  // Pure virtual: every concrete entity must declare its type. The standard
  // way is to inherit from Entity_Of<entity_type::FOO> instead of Entity
  // directly — the CRTP base supplies both this override and a compile-time
  // T::static_type constant. Replaces dynamic_cast for type queries.
  virtual ::entity_type get_type() const = 0;

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
          parse_string_to_field(value, field, current_base + field.offset);
        }
      }
    }

    // Backward compat: pre-box-volume-component maps stored "half_extents" as a
    // flat property on AABB_Entity / Trigger_Volume_Entity / Displacement_Entity.
    // If we see that key and the entity has a "volume" nested-schema field,
    // route the value into volume.half_extents. Skipped if "volume" is already
    // present (newer save format wins).
    auto half_extents_it = props.find("half_extents");
    if (half_extents_it != props.end() && !props.count("volume"))
    {
      for (const auto &field : schema->fields)
      {
        if (field.name != "volume" || field.type != Field_Type::NestedSchema)
          continue;
        const Class_Schema *nested =
            Schema_Registry::get().get_nested_schema(field);
        if (!nested)
          break;
        uint8 *volume_base = current_base + field.offset;
        for (const auto &nfield : nested->fields)
        {
          if (nfield.name == "half_extents")
          {
            parse_string_to_field(half_extents_it->second, nfield,
                                  volume_base + nfield.offset);
            break;
          }
        }
        break;
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

// CRTP base that supplies get_type() and a compile-time static_type constant.
// Every concrete entity should inherit from Entity_Of<entity_type::FOO> rather
// than Entity directly. The intermediate base has no data members, so the
// object layout is identical to inheriting Entity directly.
template <::entity_type Type>
class Entity_Of : public Entity
{
public:
  static constexpr ::entity_type static_type = Type;
  ::entity_type get_type() const override { return Type; }
};

} // namespace network

namespace shared
{
// Factory helpers
std::shared_ptr<network::Entity>
create_entity_by_classname(const std::string &classname);

// Preferred internal factory: takes the enum tag, not a string. Use this
// when you know the type at compile time or already have it as an enum.
// The classname factory is for callers that genuinely have a string
// (map loader, transaction snapshot restore).
std::shared_ptr<network::Entity>
create_entity_by_type(::entity_type type);

std::string get_classname_for_entity(const network::Entity *entity);

// Closed-enum replacement for dynamic_cast<T*>(entity).
// Returns the pointer typed as T* if the entity's concrete type is exactly T,
// else nullptr. Uses Entity_Of's T::static_type integer compare instead of
// walking RTTI — one branch + one load, no type-info traversal.
//
// Semantics differ from dynamic_cast in one way: this is *exact match only*.
// The entity hierarchy is closed and one-level (all leaves inherit Entity_Of
// directly), so this matches what every existing dynamic_cast call site in
// this codebase actually wants.
template <typename T>
T *entity_as(network::Entity *entity)
{
  if (!entity || entity->get_type() != T::static_type)
    return nullptr;
  return static_cast<T *>(entity);
}

template <typename T>
const T *entity_as(const network::Entity *entity)
{
  if (!entity || entity->get_type() != T::static_type)
    return nullptr;
  return static_cast<const T *>(entity);
}
} // namespace shared
