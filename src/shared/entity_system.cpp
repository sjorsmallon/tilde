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
}

void Entity_System::add_entity(const std::shared_ptr<network::Entity> &entity)
{
  if (!entity)
    return;

#define ADD_IF_TYPE(enum_name, class_name, str_name, header_path)              \
  if (auto *casted = dynamic_cast<const class_name *>(entity.get()))           \
  {                                                                            \
    auto it = pools.find(entity_type::enum_name);                              \
    if (it != pools.end())                                                     \
    {                                                                          \
      it->second->add_existing(entity.get());                                  \
      return;                                                                  \
    }                                                                          \
  }

  SHARED_ENTITIES_LIST(ADD_IF_TYPE)

#undef ADD_IF_TYPE

  // Optional: Log warning if not handled, but some entities might not be in the
  // system? log_error("Could not add entity of unknown type");
}

void Entity_System::populate_from_map(const map_t &map)
{
  reset();
  for (const auto &placement : map.entities)
  {
    add_entity(placement.entity);
  }
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
