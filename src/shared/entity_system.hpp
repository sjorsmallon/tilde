#pragma once

#include "map.hpp" // For map_t and entity_type
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace shared
{

struct Entity_Pool_Base
{
  virtual ~Entity_Pool_Base() = default;
  virtual void reset() = 0;
  virtual void instantiate(const entity_spawn_t &spawn) = 0;
};

template <typename T> struct EntityPool : Entity_Pool_Base
{
  std::vector<T> entities;

  void reset() override { entities.clear(); }

  void instantiate(const entity_spawn_t &spawn) override
  {
    auto &ent = entities.emplace_back();
    // Default transformation from spawn
    // Note: The schema might not have "yaw" or "position" if the entity doesn't
    // use it, but we can try to set it via init_from_map if it's in properties.
    // However, the spawn struct has explicit pos/yaw.
    // We should probably rely on properties for generic parsing,
    // BUT we might need to manually inject pos/yaw into properties if not
    // present? Or we assume the specific Entity types have a way to set this.
    // Ideally, we just rely on init_from_map.

    ent.init_from_map(spawn.properties);

    // If the entity has a "position" field compliant with our schema,
    // init_from_map handles it IF the map parser put "origin" or "position"
    // into properties.
  }
};

struct EntitySystem
{
  std::map<entity_type, std::unique_ptr<struct Entity_Pool_Base>> pools;

  template <typename T> void register_entity_type(entity_type type)
  {
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

  void reset();
  void populate_from_map(const map_t &map);
  void register_all_entities();
};

// Helpers migrated from EntityFactory
entity_type classname_to_type(const std::string &classname);
std::string type_to_classname(entity_type type);

} // namespace shared
