#define ENTITIES_WANT_INCLUDES
#include "entities/entity_reflection.hpp"
#include "game_session.hpp"
#include "shapes.hpp"
#include "physics.hpp"

namespace shared
{

game_session_t build_session(const map_t &map)
{
  game_session_t session;

  session.map_name = map.name;
  session.entity_system.populate_from_map(map);
  session.geometry = map.geometry;

  // Build the BVH over the geometry. Collision_Id.index is the index into
  // session.geometry, which is frozen for the session's lifetime. (The editor's
  // BVH keys by uid instead — see build_editor_bvh.)
  std::vector<BVH_Input> bvh_inputs;
  bvh_inputs.reserve(session.geometry.size());

  for (size_t i = 0; i < session.geometry.size(); ++i)
  {
    const geometry_value_t& geometry = session.geometry[i].value;
    const aabb_bounds_t bounds = get_bounds(geometry);

    BVH_Input input;
    input.aabb.min = bounds.min;
    input.aabb.max = bounds.max;
    input.id = {Collision_Id::Type::Static_Geometry, (uint32_t)i};
    input.collision_planes = get_collision_planes(geometry);
    input.face_polygons    = get_face_polygons(geometry);
    bvh_inputs.push_back(input);
  }

  session.bvh = build_bvh(bvh_inputs);

  session.navmesh = map.navmesh;

  return session;
}

void populate_static_physics_bodies(physics_state_t &state, const map_t &map)
{
  for (const map_geometry_t &entry : map.geometry)
  {
    switch (get_kind(entry.value))
    {
    case geometry_kind_t::Box:
    {
      const box_geometry_t &box = std::get<box_geometry_t>(entry.value);
      register_static_box(state, entry.uid, box.position, box.half_extents);
      break;
    }

    case geometry_kind_t::Displacement:
      log_warning("Skipping populating displacement because what actually are we going to do with it.");
      break;

    case geometry_kind_t::Static_Mesh:
      // Skipped on purpose — see the note on the declaration.
      break;
    }
  }
}

} // namespace shared
