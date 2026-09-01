#define ENTITIES_WANT_INCLUDES
#include "lighting.hpp"

#include "entities/entity_reflection.hpp"

#include <cmath>

namespace shared
{

std::optional<scene_light_t> try_light_of(const entities::Entity &entity)
{
  if (const entities::Point_Light_Entity *point =
          entities::entity_as<entities::Point_Light_Entity>(&entity))
  {
    scene_light_t light;
    light.kind     = light_kind_t::Point;
    light.mode     = point->light.mode;
    light.position = point->position;
    light.radiance = radiance_of(point->light);
    light.range    = point->range;
    return light;
  }

  if (const entities::Spot_Light_Entity *spot =
          entities::entity_as<entities::Spot_Light_Entity>(&entity))
  {
    scene_light_t light;
    light.kind      = light_kind_t::Spot;
    light.mode      = spot->light.mode;
    light.position  = spot->position;
    light.forward   = linalg::basis_from(spot->orientation).forward;
    light.radiance  = radiance_of(spot->light);
    light.range     = spot->range;
    light.cos_inner = std::cos(linalg::to_radians(spot->inner_degrees));
    light.cos_outer = std::cos(linalg::to_radians(spot->outer_degrees));
    return light;
  }

  if (const entities::Directional_Light_Entity *directional =
          entities::entity_as<entities::Directional_Light_Entity>(&entity))
  {
    scene_light_t light;
    light.kind     = light_kind_t::Directional;
    light.mode     = directional->light.mode;
    light.forward  = linalg::basis_from(directional->orientation).forward;
    light.radiance = radiance_of(directional->light);
    return light;
  }

  return {};
}

} // namespace shared
