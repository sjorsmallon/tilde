#pragma once

// The per-tick cut of the geometry that moves, beside the movement volumes and the disabled set. mover_def.md ss5.

#include "aabb.hpp"
#include "entity_uid.hpp"
#include "map_geometry.hpp"
#include "mover_path.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace shared
{

struct Entity_System;
struct map_t;

// The rest frame is the AUTHORED start node's pose: brushes are drawn where the lift starts. mover_def.md ss16.
struct mover_rest_t
{
  path_pose_t                    frame;
  std::vector<collision_piece_t> pieces;
};

using mover_rests_t = std::unordered_map<entity_uid_t, mover_rest_t>;

struct mover_t
{
  entity_uid_t                   uid = null_entity_uid;
  path_pose_t                    pose_at_tick_start;
  path_pose_t                    pose_at_tick_end;
  aabb_bounds_t                  swept_bounds = {};
  std::vector<collision_piece_t> pieces;
};

struct path_refusal_t
{
  entity_uid_t uid = null_entity_uid;
  std::string  reason;
};

[[nodiscard]] std::vector<path_refusal_t> validate_map_paths(const map_t& map);

// Reads `follow.from`, so it is the rest frame only while the follow is still the authored one:
// build_session latches it, the editor asks it of the map.
[[nodiscard]] path_pose_t mover_rest_frame(const Entity_System& system,
                                           const entities::Mover_Entity& mover);

[[nodiscard]] path_pose_t mover_pose_at(const Entity_System& system, const path_links_t& links,
                                        const entities::Mover_Entity& mover, const path_pose_t& rest,
                                        uint32_t tick, float tickrate);

[[nodiscard]] linalg::vec3f apply_mover_pose(const path_pose_t& rest, const path_pose_t& pose,
                                             const linalg::vec3f& point);

// A draw between two ticks: t in [0, 1] across ONE tick, so nlerp is exact enough.
[[nodiscard]] path_pose_t blend_path_poses(const path_pose_t& from, const path_pose_t& to, float t);

[[nodiscard]] linalg::vec3f carry_point_between_poses(const path_pose_t& from, const path_pose_t& to,
                                                      const linalg::vec3f& point);

[[nodiscard]] linalg::mat4f mover_model_matrix(const path_pose_t& rest, const path_pose_t& pose);

void collect_movers(const Entity_System& system, const path_links_t& links,
                    const mover_rests_t& rests, uint32_t tick, float tickrate,
                    std::vector<mover_t>& out);

} // namespace shared
