#define ENTITIES_WANT_INCLUDES
#include "game_session.hpp"
#include "entities/entity_list.hpp"
#include "shapes.hpp"
#include "physics.hpp"

namespace shared
{

void init_session_from_map(game_session_t &session, const map_t &map)
{
  session.map_name = map.name;
  session.entity_system.reset();
  session.static_entities.clear();

  // Route map entities: collision geometry → static_entities (feeds BVH),
  // everything else → entity_system (includes spawn markers, weapons, etc.).
  // Collision-geometry uids are stamped onto the Entity directly so static
  // bodies and dynamic bodies share one ID space.
  for (const auto &entry : map.entities)
  {
    if (!entry.entity)
      continue;

    entry.entity->entity_id = entry.uid;

    if (entry.entity->is_collision_geometry())
      session.static_entities.push_back(entry.entity);
    else
      session.entity_system.add_entity(entry.uid, entry.entity);
  }

  // Seed runtime spawn counter past the highest map uid so subsequent
  // spawn() calls produce IDs that can never collide with map-loaded ones.
  if (map.next_uid > session.entity_system.next_entity_id)
    session.entity_system.next_entity_id = map.next_uid;

  // 2. Build BVH from Static Entities
  std::vector<BVH_Input> bvh_inputs;
  bvh_inputs.reserve(session.static_entities.size());

  for (size_t i = 0; i < session.static_entities.size(); ++i)
  {
    auto *ent = session.static_entities[i].get();
    auto bounds = compute_entity_bounds(ent);
    BVH_Input input;
    input.aabb.min = bounds.min;
    input.aabb.max = bounds.max;
    input.id = {Collision_Id::Type::Static_Geometry, (uint32_t)i};
    input.collision_planes = compute_entity_collision_planes(ent);
    input.face_polygons    = compute_entity_face_polygons(ent);
    bvh_inputs.push_back(input);
  }

  session.bvh = build_bvh(bvh_inputs);

  session.navmesh = map.navmesh;
}

void populate_static_physics_bodies(physics_state_t &state, const map_t &map)
{
  for (const auto &entry : map.entities)
  {
    if (!entry.entity)
      continue;

    if (auto *aabb = dynamic_cast<network::AABB_Entity *>(entry.entity.get()))
    {
      register_static_box(state, entry.uid, aabb->position, aabb->volume.half_extents);
    }
    else if (auto *wedge = dynamic_cast<network::Wedge_Entity *>(entry.entity.get()))
    {
      // Approximate the wedge with its bounding box. The BVH handles exact
      // wedge collision for player movement; Jolt bodies are for projectiles.
      register_static_box(state, entry.uid, wedge->position, wedge->half_extents);
    }
    // Static_Mesh_Entity: skipped — no shape can be derived from schema fields alone.
  }
}

} // namespace shared
