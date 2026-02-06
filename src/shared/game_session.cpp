#include "game_session.hpp"
#include "shapes.hpp"

namespace shared
{

void init_session_from_map(game_session_t &session, const map_t &map)
{
  session.map_name = map.name;

  // 1. Populate Entities
  // This will instantiate entities and call init_from_map on them.
  // Properties like "origin" will be mapped to "position" via the schema update
  // we made.
  session.entity_system.populate_from_map(map);

  // 2. Setup Static Geometry
  // We copy the static geometry from the map.
  session.static_geometry = map.static_geometry;

  // 3. Build BVH
  std::vector<BVH_Input> bvh_inputs;
  bvh_inputs.reserve(session.static_geometry.size());

  for (size_t i = 0; i < session.static_geometry.size(); ++i)
  {
    // Convert center/half_extents to min/max using type-aware helper
    aabb_bounds_t bounds = shared::get_bounds(session.static_geometry[i]);

    BVH_Input input;
    input.aabb.min = bounds.min;
    input.aabb.max = bounds.max;
    // Point to the index in the session's static_geometry vector
    input.id = {Collision_Id::Type::Static_Geometry, (uint32_t)i};

    bvh_inputs.push_back(input);
  }

  session.bvh = build_bvh(bvh_inputs);
}

} // namespace shared
