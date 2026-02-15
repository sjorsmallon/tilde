#define ENTITIES_WANT_INCLUDES
#include "game_session.hpp"
#include "entities/entity_list.hpp"
#include "shapes.hpp"

namespace shared
{

void init_session_from_map(game_session_t &session, const map_t &map)
{
  session.map_name = map.name;
  session.entity_system.reset();
  session.static_entities.clear();

  // 1. Separate Static vs Dynamic Entities
  for (const auto &entry : map.entities)
  {
    if (!entry.entity)
      continue;

    if (dynamic_cast<const network::AABB_Entity *>(entry.entity.get()) ||
        dynamic_cast<const network::Wedge_Entity *>(entry.entity.get()) ||
        dynamic_cast<const network::Static_Mesh_Entity *>(entry.entity.get()))
    {
      session.static_entities.push_back(entry.entity);
    }
    else
    {
      session.entity_system.add_entity(entry.entity);
    }
  }

  // 2. Build BVH from Static Entities
  std::vector<BVH_Input> bvh_inputs;
  bvh_inputs.reserve(session.static_entities.size());

  for (size_t i = 0; i < session.static_entities.size(); ++i)
  {
    auto bounds = compute_entity_bounds(session.static_entities[i].get());
    BVH_Input input;
    input.aabb.min = bounds.min;
    input.aabb.max = bounds.max;
    input.id = {Collision_Id::Type::Static_Geometry, (uint32_t)i};
    bvh_inputs.push_back(input);
  }

  session.bvh = build_bvh(bvh_inputs);
}

} // namespace shared
