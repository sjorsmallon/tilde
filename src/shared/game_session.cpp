#define ENTITIES_WANT_INCLUDES
#include "entities/entity_reflection.hpp"
#include "game_session.hpp"
#include "shapes.hpp"

namespace shared
{

bool entity_type_can_own_geometry(entities::entity_type type)
{
  for (entities::entity_type owner_type : GEOMETRY_OWNER_TYPES)
    if (owner_type == type)
      return true;
  return false;
}

// The wiring, checked and then indexed. ONE check, two policies: here every
// refused row is logged and DROPPED, so the drain can keep treating a null
// dispatch cell as a generator bug rather than a map's; the server's map
// load asks validate_map_connections itself and refuses the whole map, which
// is what stops a broken level going live. An editor that could not open a
// map with one bad row could not repair it either.
static void index_connections(game_session_t &session, const map_t &map)
{
  session.connections_by_sender.clear();

  std::vector<bool> refused(map.connections.size(), false);
  for (const connection_refusal_t &refusal : validate_map_connections(map))
  {
    log_error("build_session: connection {} dropped — {}", refusal.index, refusal.reason);
    refused[refusal.index] = true;
  }

  for (size_t index = 0; index < map.connections.size(); ++index)
  {
    if (refused[index])
      continue;
    session.connections_by_sender[map.connections[index].sender].push_back(
        {map.connections[index], false});
  }
}

static void derive_mover_rest_frames(game_session_t &session)
{
  for (const entities::Mover_Entity &mover : session.entity_system.entities_of<entities::Mover_Entity>())
    session.mover_rests[mover.entity_id].frame = mover_rest_frame(session.entity_system, mover);
}

void restore_map_entities(game_session_t &session, const map_t &map)
{
  for (const map_entity_t &entry : map.entities)
    if (entry.entity && session.entity_system.try_find(entry.uid) == nullptr)
      session.entity_system.add_entity(entry.uid, entry.entity.get());

  index_connections(session, map);
  session.path_links = derive_path_links(session.entity_system);
  derive_mover_rest_frames(session);
}

game_session_t build_session(const map_t &map)
{
  game_session_t session;

  session.map_name = map.name;
  session.entity_system.populate_from_map(map);
  session.geometry  = map.geometry;
  session.materials = map.materials;
  session.lightmap  = map.lightmap;

  index_connections(session, map);

  session.path_links = derive_path_links(session.entity_system);
  for (const path_refusal_t &refusal : validate_map_paths(map))
    log_error("build_session: {}", refusal.reason);
  for (entity_uid_t node : session.path_links.named_by_several)
    log_warning("build_session: {} is the next of several path nodes, so it has no way back",
                describe_map_entity(map, node));
  derive_mover_rest_frames(session);

  // The tie, derived and checked ONCE. A geometry naming an owner that is not a
  // live geometry owner keeps no entry, so the collect that runs every tick has
  // nothing left to refuse and the brush simply stays solid.
  session.owner_of.assign(session.geometry.size(), null_entity_uid);
  for (size_t index = 0; index < session.geometry.size(); ++index)
  {
    const entity_uid_t owner = get_owner_uid(session.geometry[index].value);
    if (owner == null_entity_uid)
      continue;

    const entities::Entity *entity = session.entity_system.try_find(owner);
    if (entity == nullptr)
    {
      log_error("build_session: geometry {} is tied to uid {}, which this map does not hold "
                "— it stays solid",
                session.geometry[index].uid, owner);
      continue;
    }
    if (!entity_type_can_own_geometry(entity->type))
    {
      log_error("build_session: geometry {} is tied to uid {}, which is a {} and neither a "
                "geometry_owner_entity nor a mover_entity — it stays solid",
                session.geometry[index].uid, owner,
                entities::entity_info(entity->type).classname);
      continue;
    }

    session.owner_of[index] = owner;
  }

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

    const entities::Entity *owner = session.entity_system.try_find(session.owner_of[i]);
    if (owner != nullptr && owner->type == entities::entity_type::Mover_Entity)
    {
      std::vector<collision_piece_t> pieces = get_collision_pieces(entry.value, entry.uid);
      std::vector<collision_piece_t> &rest  = session.mover_rests[session.owner_of[i]].pieces;
      rest.insert(rest.end(), std::make_move_iterator(pieces.begin()),
                  std::make_move_iterator(pieces.end()));
      continue;
    }

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

} // namespace shared
