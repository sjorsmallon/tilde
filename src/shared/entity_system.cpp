#include "entity_system.hpp"

#include "log.hpp"

// We need the full entity definitions for registration, but we want to avoid
// polluting the header with them. So we request them here.
#define ENTITIES_WANT_INCLUDES
#include "entities/entity_list.hpp"

namespace shared
{

void EntitySystem::reset()
{
  for (auto &[type, pool] : pools)
  {
    pool->reset();
  }
}

void EntitySystem::populate_from_map(const map_t &map)
{
  reset();

  for (const auto &ent : map.entities)
  {
    auto it = pools.find(ent.type);
    if (it != pools.end())
    {
      it->second->instantiate(ent);
    }
    else
    {
      log_error("Unknown entity type: {}", (int)ent.type);
    }
  }
}

void EntitySystem::register_all_entities()
{
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
