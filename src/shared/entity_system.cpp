#include "entity_system.hpp"

#include "log.hpp"

// We need the full entity definitions for registration, but we want to avoid
// polluting the header with them. So we request them here.
#define ENTITIES_WANT_INCLUDES
#include "entities/entity_list.hpp"

namespace shared
{

void Entity_System::reset()
{
  for (auto &[type, pool] : pools)
  {
    pool->reset();
  }
  next_entity_id = 1;  // Reset ID counter when clearing entities
}

void Entity_System::add_entity(shared::entity_uid_t uid,
                               const std::shared_ptr<network::Entity> &entity)
{
  if (!entity)
    return;

  // Map-loaded entities carry their canonical uid here. Runtime-spawned
  // entities go through Entity_System::spawn() instead, which assigns from
  // next_entity_id. Both ID spaces are unified at session init time (see
  // init_session_from_map).
  entity->entity_id = uid;

  auto it = pools.find(entity->get_type());
  if (it != pools.end())
  {
    it->second->add_existing(entity.get());
    return;
  }

  log_error("Entity_System::add_entity: entity (uid={}) has no matching pool — "
            "is the entity type registered?", uid);
}

void Entity_System::populate_from_map(const map_t &map)
{
  reset();
  for (const auto &entry : map.entities)
  {
    add_entity(entry.uid, entry.entity);
  }
  // Continue runtime IDs from where the map left off so map-loaded and
  // runtime-spawned IDs share one monotonic space.
  if (map.next_uid > next_entity_id)
    next_entity_id = map.next_uid;
}

void Entity_System::register_all_known_entity_types()
{
  log_terminal("Registering all known entity types");

#define REGISTER_GEN(enum_name, class_name, str_name, header_path)             \
  register_entity_type<class_name>(entity_type::enum_name);

  SHARED_ENTITIES_LIST(REGISTER_GEN)
#undef REGISTER_GEN
}

entity_type classname_to_type(const std::string &classname)
{
#define FROM_STRING_GEN(enum_name, class_name, str_name, header_path)          \
  if (classname == str_name)                                                   \
    return entity_type::enum_name;

  SHARED_ENTITIES_LIST(FROM_STRING_GEN)
#undef FROM_STRING_GEN

  return entity_type::UNKNOWN;
}

std::string type_to_classname(entity_type type)
{
  switch (type)
  {
#define TO_STRING_GEN(enum_name, class_name, str_name, header_path)            \
  case entity_type::enum_name:                                                 \
    return str_name;

    SHARED_ENTITIES_LIST(TO_STRING_GEN)
#undef TO_STRING_GEN

  default:
    return "entity_spawn";
  }
}

} // namespace shared
