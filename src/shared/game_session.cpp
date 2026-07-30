#define ENTITIES_WANT_INCLUDES
#include "entities/entity_reflection.hpp"
#include "game_session.hpp"
#include "shapes.hpp"
#include "physics.hpp"

namespace shared
{

void init_session_from_map(game_session_t &session, const map_t &map)
{
  session.map_name = map.name;
  session.entity_system.reset();

  // Geometry: take a copy, in map order. No routing decision to make any more —
  // the map already separates geometry from entities, so this used to be an
  // is_collision_geometry() virtual call per entity deciding which of two
  // containers a shared_ptr got aliased into.
  session.geometry = map.geometry;

  // Entities: everything left is a real entity (spawn markers, weapons, ...).
  //
  // `const map_t&` is now true. This loop used to write `entry.entity->
  // entity_id = entry.uid` first, through a shared_ptr the MAP owns — so
  // initializing a session renumbered the caller's map, and re-serializing
  // afterwards saved runtime ids. add_entity takes the uid and stamps it on the
  // pool's own copy instead (P7 step 1, entity_storage_def.md §4).
  for (const map_entity_t &entry : map.entities)
  {
    if (!entry.entity)
      continue;

    session.entity_system.add_entity(entry.uid, entry.entity.get());
  }

  // Seed runtime spawn counter past the highest map uid so subsequent
  // spawn() calls produce IDs that can never collide with map-loaded ones.
  if (map.next_uid > session.entity_system.next_entity_id)
    session.entity_system.next_entity_id = map.next_uid;

  // Build the BVH over the geometry. Collision_Id.index is the index into
  // session.geometry, which is frozen for the session's lifetime. (The editor's
  // BVH keys by uid instead — see build_editor_bvh.)
  std::vector<BVH_Input> bvh_inputs;
  bvh_inputs.reserve(session.geometry.size());

  for (size_t i = 0; i < session.geometry.size(); ++i)
  {
    const geometry_value_t &geometry = session.geometry[i].value;
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
      // Skipped, matching pre-exit behavior exactly: Displacement_Entity was
      // never registered here either.
      //
      // This is a real inconsistency, not a decision — player movement (the BVH)
      // already collides with a displacement's box bound, so Jolt bodies pass
      // through terrain that the player stands on. Registering the box would
      // trade that for rockets exploding on an invisible lid above the surface,
      // which is not obviously better; the actual fix is heightmap collision
      // (see the TODO in get_collision_planes). Left alone on purpose — changing
      // it is a gameplay decision, not part of moving geometry out of the entity
      // system.
      break;

    case geometry_kind_t::Static_Mesh:
      // Skipped on purpose — see the note on the declaration.
      break;
    }
  }
}

} // namespace shared
