#pragma once

// A mover's pose is a pure function of its Path_Follow, the node chain and the tick. mover_def.md.

#include "entities/generated/entities_generated.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace shared
{

struct Entity_System;

struct path_links_t
{
  std::unordered_map<entity_uid_t, entity_uid_t> previous_of;
  std::vector<entity_uid_t>                      named_by_several;
};

struct path_segment_t
{
  entity_uid_t      from_uid = null_entity_uid;
  entity_uid_t      to_uid   = null_entity_uid;
  linalg::vec3f     from_position    = {};
  linalg::quatf     from_orientation = linalg::quatf::identity();
  linalg::vec3f     to_position      = {};
  linalg::quatf     to_orientation   = linalg::quatf::identity();
  uint32_t          traversal_ticks  = 0;
  uint32_t          wait_ticks       = 0;
  entities::Easing  easing           = entities::Easing::Linear;
};

struct path_pose_t
{
  linalg::vec3f position    = {};
  linalg::quatf orientation = linalg::quatf::identity();
};

[[nodiscard]] path_links_t derive_path_links(const Entity_System& system);

[[nodiscard]] entity_uid_t previous_node_of(const path_links_t& links, entity_uid_t node);

[[nodiscard]] std::optional<path_segment_t> try_cut_path_segment(const Entity_System& system,
                                                                 const path_links_t&  links,
                                                                 entity_uid_t         from,
                                                                 int32_t              direction,
                                                                 float                tickrate);

[[nodiscard]] path_pose_t transform_at(const path_segment_t& segment, uint32_t segment_start_tick,
                                       uint32_t tick);

[[nodiscard]] std::optional<path_pose_t> try_path_pose_at(const Entity_System&          system,
                                                          const path_links_t&           links,
                                                          const entities::Path_Follow& follow,
                                                          uint32_t tick, float tickrate);

[[nodiscard]] uint32_t path_clock_tick(const entities::Path_Follow& follow, uint32_t tick);

void advance_path_follow(const Entity_System& system, const path_links_t& links,
                         entities::Path_Follow& follow, uint32_t tick, float tickrate,
                         std::vector<entity_uid_t>& out_reached);

void freeze_path_follow(entities::Path_Follow& follow, uint32_t tick);

void resume_path_follow(entities::Path_Follow& follow, uint32_t tick);

[[nodiscard]] bool try_reverse_path_follow(const Entity_System& system, const path_links_t& links,
                                           entities::Path_Follow& follow, uint32_t tick,
                                           float tickrate);

[[nodiscard]] bool try_path_follow_go_to(const Entity_System& system, const path_links_t& links,
                                         entities::Path_Follow& follow, entity_uid_t node,
                                         uint32_t tick, float tickrate);

} // namespace shared
