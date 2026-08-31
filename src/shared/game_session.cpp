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
  session.geometry  = map.geometry;
  session.materials = map.materials;
  session.lightmap  = map.lightmap;

  // Build the BVH over the geometry. Collision_Id.index is the index into
  // session.geometry, which is frozen for the session's lifetime. (The editor's
  // BVH keys by uid instead — see build_editor_bvh.)
  //
  // ONE OBJECT IS N LEAVES: a brush decomposes into convex pieces and every one
  // of them carries the object's index, which is what lets a brush be any closed
  // polyhedron while player_move keeps seeing convex solids. Nothing resolves
  // this index back to a geometry today, and if something ever does it must
  // expect several leaves to answer with it.
  std::vector<BVH_Input> bvh_inputs;
  bvh_inputs.reserve(session.geometry.size());

  for (size_t i = 0; i < session.geometry.size(); ++i)
  {
    const map_geometry_t &entry = session.geometry[i];

    for (const collision_piece_t &piece : get_collision_pieces(entry.value, entry.uid))
    {
      BVH_Input input;
      input.aabb             = piece.bounds;
      input.id               = {Collision_Id::Type::Static_Geometry, (uint32_t)i};
      input.collision_planes = piece.planes;
      input.face_polygons    = piece.face_polygons;
      bvh_inputs.push_back(std::move(input));
    }
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
    case geometry_kind_t::Static_Mesh:
      // Skipped on purpose — see the note on the declaration.
      break;

    case geometry_kind_t::Brush:
    {
      const brush_geometry_t &brush = std::get<brush_geometry_t>(entry.value);
      register_static_convex_hull(state, entry.uid, brush.hull_points);
      break;
    }
    }
  }
}

} // namespace shared
