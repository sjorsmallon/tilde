#pragma once

#include "map.hpp" // For map_t and entity_type
#include "network/network_types.hpp"
#include "network/schema.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace shared
{

struct Spawn_Info
{
  linalg::vec3 position = {{0, 0, 0}};
  float yaw = 0.0f;
  std::map<std::string, std::string> properties;
};

struct Entity_Pool_Base
{
  virtual ~Entity_Pool_Base() = default;
  virtual void reset() = 0;
  virtual void instantiate(const Spawn_Info &spawn) = 0;
  virtual void add_existing(const network::Entity *entity) = 0;
};

template <typename T> struct EntityPool : Entity_Pool_Base
{
  std::vector<T> entities;

  void reset() override { entities.clear(); }

  // Instantiates an entity from spawn data.
  // 1. Calls ent.init_from_map(spawn.properties) to parse generic properties.
  // 2. "Magically" injects position and yaw if the Entity's schema has matching
  // fields:
  //    - Field "position" (Vec3f) <- spawn.position
  //    - Field "yaw" or "view_angle_yaw" (Float32) <- spawn.yaw
  //    - Field "yaw" or "view_angle_yaw" (Float32) <- spawn.yaw
  void instantiate(const Spawn_Info &spawn) override
  {
    auto &ent = entities.emplace_back();

    // 1. Init properties from map first
    ent.init_from_map(spawn.properties);

    // 2. "Magic" injection:
    // If the entity has a "position" field of type Vec3f, set it from
    // spawn.position If the entity has a "yaw" field of type Float32, set it
    // from spawn.yaw

    const auto *schema = ent.get_schema();
    if (schema)
    {
      network::uint8 *base = reinterpret_cast<network::uint8 *>(&ent);

      for (const auto &field : schema->fields)
      {
        if (field.name == "position" &&
            field.type == network::Field_Type::Vec3f)
        {
          // We have a position field!
          network::Network_Var<linalg::vec3> *ptr =
              reinterpret_cast<network::Network_Var<linalg::vec3> *>(
                  base + field.offset);
          *ptr = spawn.position;
        }
        else if (field.name == "yaw" &&
                 field.type == network::Field_Type::Float32)
        {
          // We have a yaw field!
          // (Note: player_entity uses view_angle_yaw, so this might need to be
          // specific or we just rely on standard naming conventions if we want
          // this to be generic.) BUT, the goal is to map from spawn.yaw if
          // possible. However, player_entity has "view_angle_yaw". Let's check
          // for "view_angle_yaw" too? Or maybe we should just set "yaw" if it
          // exists. For now let's stick to strict "yaw" or maybe check for both
          // common names? No, let's just do "yaw" and "view_angle_yaw" for
          // convenience? Wait, "yaw" is what we parse from map property "yaw".

          network::Network_Var<float> *ptr =
              reinterpret_cast<network::Network_Var<float> *>(base +
                                                              field.offset);
          *ptr = spawn.yaw;
        }
        else if (field.name == "view_angle_yaw" &&
                 field.type == network::Field_Type::Float32)
        {
          network::Network_Var<float> *ptr =
              reinterpret_cast<network::Network_Var<float> *>(base +
                                                              field.offset);
          *ptr = spawn.yaw;
        }
      }
    }
  }

  void add_existing(const network::Entity *entity) override
  {
    if (const T *cast_ent = dynamic_cast<const T *>(entity))
    {
      entities.push_back(*cast_ent);
    }
  }

  void remove(T *ptr)
  {
    for (size_t i = 0; i < entities.size(); ++i)
    {
      if (&entities[i] == ptr)
      {
        if (i != entities.size() - 1)
        {
          entities[i] = std::move(entities.back());
        }
        entities.pop_back();
        return;
      }
    }
  }
};

//@NOTE(SJM): while I am mostly opposed to constructors and not default
// parameters,
// we need to be sure to register all entity types before we can use them, and
// it does not make sense to live in a world where you can forget that.
struct Entity_System
{

  Entity_System() { register_all_known_entity_types(); }
  std::map<entity_type, std::unique_ptr<struct Entity_Pool_Base>> pools;

  template <typename T> void register_entity_type(entity_type type)
  {
#ifdef ENTITIES_WANT_INCLUDES
    // Check if T is defined?
    // Wait, T is a template param.
#endif
    pools[type] = std::make_unique<EntityPool<T>>();
    T::register_schema();
  }

  template <typename T> std::vector<T> *get_entities(entity_type type)
  {
    auto it = pools.find(type);
    if (it != pools.end())
    {
      // Safe downcast because we trust the type registration matching
      return &static_cast<EntityPool<T> *>(it->second.get())->entities;
    }
    return nullptr;
  }

  //@FIXME(SJM): I don't think this should actually return T* ? and that delete
  // should not actually delete but just free up a slot.
  template <typename T> T *spawn(entity_type type, const Spawn_Info &info = {})
  {
    auto it = pools.find(type);
    if (it != pools.end())
    {
      auto *pool = static_cast<EntityPool<T> *>(it->second.get());
      pool->instantiate(info);
      return &pool->entities.back();
    }
    return nullptr;
  }

  template <typename T> void destroy(entity_type type, T *ptr)
  {
    auto it = pools.find(type);
    if (it != pools.end())
    {
      auto *pool = static_cast<EntityPool<T> *>(it->second.get());
      pool->remove(ptr);
    }
  }

  void reset();
  void populate_from_map(const map_t &map);
  void add_entity(const std::shared_ptr<network::Entity> &entity);

  // this is called in the constructor, no need for you to call it.
  void register_all_known_entity_types();
};

// Helpers migrated from EntityFactory
entity_type classname_to_type(const std::string &classname);
std::string type_to_classname(entity_type type);

} // namespace shared
