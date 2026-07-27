#include "entity_system.hpp"

#include "log.hpp"

namespace shared
{

std::unique_ptr<Entity_Pool_Base> make_entity_pool(entities::entity_type type)
{
  switch (type)
  {
    case entities::entity_type::Player_Spawn_Entity:
      return std::make_unique<Entity_Pool<entities::Player_Spawn_Entity>>();
    case entities::entity_type::Player_Entity:
      return std::make_unique<Entity_Pool<entities::Player_Entity>>();
    case entities::entity_type::Weapon_Entity:
      return std::make_unique<Entity_Pool<entities::Weapon_Entity>>();
    case entities::entity_type::Rocket_Entity:
      return std::make_unique<Entity_Pool<entities::Rocket_Entity>>();
    case entities::entity_type::Particle_Emitter_Entity:
      return std::make_unique<Entity_Pool<entities::Particle_Emitter_Entity>>();
    case entities::entity_type::Trigger_Volume_Entity:
      return std::make_unique<Entity_Pool<entities::Trigger_Volume_Entity>>();
    case entities::entity_type::Light_Entity:
      return std::make_unique<Entity_Pool<entities::Light_Entity>>();
    case entities::entity_type::Physics_Body_Entity:
      return std::make_unique<Entity_Pool<entities::Physics_Body_Entity>>();
    case entities::entity_type::Invalid:
      break;
  }

  log_error("make_entity_pool: entity_type {} has no pool", (int)type);
  return nullptr;
}

void Entity_System::reset()
{
  for (auto &[type, pool] : pools)
    pool->reset();

  next_entity_id = 1; // Reset ID counter when clearing entities
}

void Entity_System::add_entity(entity_uid_t uid,
                               const std::shared_ptr<entities::Entity> &entity)
{
  if (!entity)
    return;

  // Map-loaded entities carry their canonical uid here. Runtime-spawned
  // entities go through Entity_System::spawn() instead, which assigns from
  // next_entity_id. Both ID spaces are unified at session init time (see
  // init_session_from_map).
  entity->entity_id = uid;

  auto it = pools.find(entity->type);
  if (it != pools.end())
  {
    it->second->add_existing(entity.get());
    return;
  }

  log_error("Entity_System::add_entity: entity (uid={}) has no matching pool for type {}", uid,
            entities::classname_of(entity.get()));
}

void Entity_System::populate_from_map(const map_t &map)
{
  reset();
  for (const map_entity_t &entry : map.entities)
    add_entity(entry.uid, entry.entity);

  // Continue runtime IDs from where the map left off so map-loaded and
  // runtime-spawned IDs share one monotonic space.
  if (map.next_uid > next_entity_id)
    next_entity_id = map.next_uid;
}

void Entity_System::register_all_known_entity_types()
{
  // Every tag in the generated enum gets a pool. No list to keep in sync: the
  // enum IS the list, and make_entity_pool is what warns if a type is added
  // without one.
  for (uint32_t index = 1; index < entities::ENTITY_TYPE_COUNT; ++index)
  {
    const entities::entity_type type = (entities::entity_type)index;
    pools[type] = make_entity_pool(type);
  }
}

} // namespace shared
