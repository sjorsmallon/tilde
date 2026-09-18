#include "movers.hpp"

#include "entity_system.hpp"
#include "map.hpp"
#include "map_connection.hpp"

#include <format>

namespace shared
{

namespace
{

linalg::quatf rotation_from_rest(const path_pose_t& rest, const path_pose_t& pose)
{
  return pose.orientation * linalg::inverse(rest.orientation);
}

void expand_by_moved_corners(aabb_bounds_t& bounds, const path_pose_t& rest,
                             const path_pose_t& pose, const aabb_bounds_t& rest_bounds)
{
  for (uint32_t corner = 0; corner < 8; ++corner)
  {
    const linalg::vec3f point = {(corner & 1) ? rest_bounds.max.x : rest_bounds.min.x,
                                 (corner & 2) ? rest_bounds.max.y : rest_bounds.min.y,
                                 (corner & 4) ? rest_bounds.max.z : rest_bounds.min.z};
    expand_aabb_to_include_point(bounds, apply_mover_pose(rest, pose, point));
  }
}

} // namespace

std::vector<path_refusal_t> validate_map_paths(const map_t& map)
{
  std::vector<path_refusal_t> refusals;

  const auto names_a_node = [&](entity_uid_t uid)
  {
    const map_entity_t* named = map.find_by_uid(uid);
    return named != nullptr && entities::entity_as<entities::Path_Node_Entity>(named->entity.get());
  };

  for (const map_entity_t& entry : map.entities)
  {
    if (const entities::Path_Node_Entity* node =
            entities::entity_as<entities::Path_Node_Entity>(entry.entity.get()))
    {
      if (node->next != null_entity_uid && !names_a_node(node->next))
        refusals.push_back({entry.uid, std::format("{}: next names {}, which is not a path node",
                                                   describe_map_entity(map, entry.uid),
                                                   describe_map_entity(map, node->next))});
    }
    else if (const entities::Mover_Entity* mover =
                 entities::entity_as<entities::Mover_Entity>(entry.entity.get()))
    {
      if (!names_a_node(mover->follow.from))
        refusals.push_back({entry.uid, std::format("{}: starts from {}, which is not a path node",
                                                   describe_map_entity(map, entry.uid),
                                                   describe_map_entity(map, mover->follow.from))});
    }
  }
  return refusals;
}

path_pose_t mover_rest_frame(const Entity_System& system, const entities::Mover_Entity& mover)
{
  if (const entities::Path_Node_Entity* start = system.get<entities::Path_Node_Entity>(mover.follow.from))
    return {.position = start->position, .orientation = start->orientation};
  return {.position = mover.position, .orientation = mover.orientation};
}

path_pose_t mover_pose_at(const Entity_System& system, const path_links_t& links,
                          const entities::Mover_Entity& mover, const path_pose_t& rest,
                          uint32_t tick, float tickrate)
{
  const std::optional<path_pose_t> pose =
      try_path_pose_at(system, links, mover.follow, tick, tickrate);
  if (pose)
    return *pose;
  return rest;
}

linalg::vec3f apply_mover_pose(const path_pose_t& rest, const path_pose_t& pose,
                               const linalg::vec3f& point)
{
  return pose.position + linalg::rotate(rotation_from_rest(rest, pose), point - rest.position);
}

path_pose_t blend_path_poses(const path_pose_t& from, const path_pose_t& to, float t)
{
  return {.position    = from.position + (to.position - from.position) * t,
          .orientation = linalg::nlerp(from.orientation, to.orientation, t)};
}

linalg::vec3f carry_point_between_poses(const path_pose_t& from, const path_pose_t& to,
                                        const linalg::vec3f& point)
{
  const linalg::quatf turn = to.orientation * linalg::inverse(from.orientation);
  return to.position + linalg::rotate(turn, point - from.position);
}

linalg::mat4f mover_model_matrix(const path_pose_t& rest, const path_pose_t& pose)
{
  const linalg::quatf rotation = rotation_from_rest(rest, pose);
  return linalg::compose_transform(pose.position - linalg::rotate(rotation, rest.position),
                                   rotation, {1.0f, 1.0f, 1.0f});
}

void collect_movers(const Entity_System& system, const path_links_t& links,
                    const mover_rests_t& rests, uint32_t tick, float tickrate,
                    std::vector<mover_t>& out)
{
  Span<const entities::Mover_Entity> movers = system.entities_of<entities::Mover_Entity>();
  out.resize(movers.size());

  for (uint32_t index = 0; index < movers.size(); ++index)
  {
    const entities::Mover_Entity& mover = movers[index];
    mover_t& cut = out[index];

    const auto found = rests.find(mover.entity_id);
    const path_pose_t frame = found != rests.end() ? found->second.frame : mover_rest_frame(system, mover);

    cut.uid                = mover.entity_id;
    cut.pose_at_tick_start = mover_pose_at(system, links, mover, frame, tick > 0 ? tick - 1 : tick, tickrate);
    cut.pose_at_tick_end   = mover_pose_at(system, links, mover, frame, tick, tickrate);
    cut.swept_bounds       = {cut.pose_at_tick_end.position, cut.pose_at_tick_end.position};

    if (found == rests.end() || found->second.pieces.empty())
    {
      expand_aabb_to_include_point(cut.swept_bounds, cut.pose_at_tick_start.position);
      cut.pieces.clear();
      continue;
    }

    cut.pieces = found->second.pieces;
    const linalg::quatf rotation = rotation_from_rest(frame, cut.pose_at_tick_end);
    for (uint32_t piece_index = 0; piece_index < cut.pieces.size(); ++piece_index)
    {
      collision_piece_t& piece = cut.pieces[piece_index];
      const aabb_bounds_t& rest_bounds = found->second.pieces[piece_index].bounds;

      for (Plane& plane : piece.planes)
      {
        plane.point  = apply_mover_pose(frame, cut.pose_at_tick_end, plane.point);
        plane.normal = linalg::rotate(rotation, plane.normal);
      }
      for (std::vector<linalg::vec3>& polygon : piece.face_polygons)
        for (linalg::vec3& vertex : polygon)
          vertex = apply_mover_pose(frame, cut.pose_at_tick_end, vertex);

      piece.bounds = {apply_mover_pose(frame, cut.pose_at_tick_end, rest_bounds.min),
                      apply_mover_pose(frame, cut.pose_at_tick_end, rest_bounds.min)};
      expand_by_moved_corners(piece.bounds, frame, cut.pose_at_tick_end, rest_bounds);

      cut.swept_bounds = piece_index == 0 ? piece.bounds : union_aabb(cut.swept_bounds, piece.bounds);
      expand_by_moved_corners(cut.swept_bounds, frame, cut.pose_at_tick_start, rest_bounds);
    }
  }
}

} // namespace shared
