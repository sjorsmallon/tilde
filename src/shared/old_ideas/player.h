#pragma once

#include "ecs.hpp"
#include "game.pb.h"
#include "shared/components/transform.h"
#include "shared/components/type.h"
#include "shared/linalg.hpp"

namespace entities
{

inline ecs::Entity create_player(ecs::Registry &registry, linalg::vec3 position)
{
  ecs::Entity entity = registry.create_entity();

  registry.add_component(entity, components::type_t{game::EntityType::PLAYER});
  registry.add_component(entity, components::position_t{{position}});
  registry.add_component(entity,
                         components::orientation_t{{linalg::vec4{0, 0, 0, 1}}});

  return entity;
}

} // namespace entities
