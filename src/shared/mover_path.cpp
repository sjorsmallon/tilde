#include "mover_path.hpp"

#include "entity_system.hpp"
#include "subtick.hpp"
#include "tween.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

namespace
{

uint32_t elapsed_ticks(uint32_t segment_start_tick, uint32_t tick)
{
  const int32_t elapsed = (int32_t)(tick - segment_start_tick);
  return elapsed > 0 ? (uint32_t)elapsed : 0;
}

uint32_t segment_duration_ticks(const path_segment_t& segment)
{
  return std::max(1u, segment.traversal_ticks + segment.wait_ticks);
}

void advance(const Entity_System& system, const path_links_t& links, entities::Path_Follow& follow,
             uint32_t tick, float tickrate, std::vector<entity_uid_t>* out_reached)
{
  for (;;)
  {
    std::optional<path_segment_t> segment =
        try_cut_path_segment(system, links, follow.from, follow.direction, tickrate);
    if (!segment)
    {
      segment = try_cut_path_segment(system, links, follow.from, -follow.direction, tickrate);
      if (!segment)
        return;
      follow.direction = -follow.direction;
    }

    const uint32_t duration = segment_duration_ticks(*segment);
    if (elapsed_ticks(follow.segment_start_tick, tick) < duration)
      return;

    follow.from = segment->to_uid;
    follow.segment_start_tick += duration;
    if (out_reached != nullptr)
      out_reached->push_back(segment->to_uid);
  }
}

} // namespace

path_links_t derive_path_links(const Entity_System& system)
{
  path_links_t links;
  std::unordered_map<entity_uid_t, uint32_t> predecessor_count;

  for (const entities::Path_Node_Entity& node : system.entities_of<entities::Path_Node_Entity>())
  {
    if (node.next == null_entity_uid)
      continue;
    if (++predecessor_count[node.next] == 1)
      links.previous_of[node.next] = node.entity_id;
    else
      links.previous_of[node.next] = null_entity_uid;
  }

  for (const auto& [node, count] : predecessor_count)
  {
    if (count > 1)
      links.named_by_several.push_back(node);
  }
  std::sort(links.named_by_several.begin(), links.named_by_several.end());
  return links;
}

entity_uid_t previous_node_of(const path_links_t& links, entity_uid_t node)
{
  const auto found = links.previous_of.find(node);
  return found == links.previous_of.end() ? null_entity_uid : found->second;
}

std::optional<path_segment_t> try_cut_path_segment(const Entity_System& system,
                                                   const path_links_t& links, entity_uid_t from,
                                                   int32_t direction, float tickrate)
{
  const entities::Path_Node_Entity* from_node = system.get<entities::Path_Node_Entity>(from);
  if (from_node == nullptr)
    return std::nullopt;

  const entity_uid_t to = direction >= 0 ? from_node->next : previous_node_of(links, from);
  const entities::Path_Node_Entity* to_node = system.get<entities::Path_Node_Entity>(to);
  if (to_node == nullptr)
    return std::nullopt;

  const entities::Path_Node_Entity& owner = direction >= 0 ? *from_node : *to_node;

  return path_segment_t{
      .from_uid         = from,
      .to_uid           = to,
      .from_position    = from_node->position,
      .from_orientation = from_node->orientation,
      .to_position      = to_node->position,
      .to_orientation   = to_node->orientation,
      .traversal_ticks  = ticks_from_seconds(owner.traversal_seconds, tickrate),
      .wait_ticks       = ticks_from_seconds(owner.wait_seconds, tickrate),
      .easing           = owner.easing,
  };
}

path_pose_t transform_at(const path_segment_t& segment, uint32_t segment_start_tick, uint32_t tick)
{
  const uint32_t elapsed = elapsed_ticks(segment_start_tick, tick);
  const float linear_t =
      segment.traversal_ticks == 0
          ? 1.0f
          : (float)std::min(elapsed, segment.traversal_ticks) / (float)segment.traversal_ticks;
  const float t = apply_easing(segment.easing, linear_t);

  return {
      .position    = segment.from_position + (segment.to_position - segment.from_position) * t,
      .orientation = slerp(segment.from_orientation, segment.to_orientation, t),
  };
}

uint32_t path_clock_tick(const entities::Path_Follow& follow, uint32_t tick)
{
  return follow.frozen_at_tick != 0 ? std::min(follow.frozen_at_tick, tick) : tick;
}

std::optional<path_pose_t> try_path_pose_at(const Entity_System& system, const path_links_t& links,
                                            const entities::Path_Follow& written_follow,
                                            uint32_t tick, float tickrate)
{
  const uint32_t clock_tick = path_clock_tick(written_follow, tick);
  entities::Path_Follow follow = written_follow;
  advance(system, links, follow, clock_tick, tickrate, nullptr);

  const std::optional<path_segment_t> segment =
      try_cut_path_segment(system, links, follow.from, follow.direction, tickrate);
  if (segment)
    return transform_at(*segment, follow.segment_start_tick, clock_tick);

  const entities::Path_Node_Entity* parked = system.get<entities::Path_Node_Entity>(follow.from);
  if (parked == nullptr)
    return std::nullopt;
  return path_pose_t{.position = parked->position, .orientation = parked->orientation};
}

void advance_path_follow(const Entity_System& system, const path_links_t& links,
                         entities::Path_Follow& follow, uint32_t tick, float tickrate,
                         std::vector<entity_uid_t>& out_reached)
{
  advance(system, links, follow, path_clock_tick(follow, tick), tickrate, &out_reached);
}

void freeze_path_follow(entities::Path_Follow& follow, uint32_t tick)
{
  if (follow.frozen_at_tick == 0)
    follow.frozen_at_tick = std::max(tick, 1u);
}

void resume_path_follow(entities::Path_Follow& follow, uint32_t tick)
{
  if (follow.frozen_at_tick == 0)
    return;
  follow.segment_start_tick += tick - follow.frozen_at_tick;
  follow.frozen_at_tick = 0;
}


bool try_reverse_path_follow(const Entity_System& system, const path_links_t& links,
                             entities::Path_Follow& follow, uint32_t tick, float tickrate)
{
  const std::optional<path_segment_t> segment =
      try_cut_path_segment(system, links, follow.from, follow.direction, tickrate);
  if (!segment)
  {
    if (!try_cut_path_segment(system, links, follow.from, -follow.direction, tickrate))
      return false;
    follow.direction          = -follow.direction;
    follow.segment_start_tick = tick;
    return true;
  }

  const std::optional<path_segment_t> retrace =
      try_cut_path_segment(system, links, segment->to_uid, -follow.direction, tickrate);
  if (!retrace || retrace->to_uid != segment->from_uid)
    return false;

  const uint32_t travelled =
      std::min(elapsed_ticks(follow.segment_start_tick, tick), segment->traversal_ticks);
  follow.from               = segment->to_uid;
  follow.direction          = -follow.direction;
  follow.segment_start_tick = tick - (segment->traversal_ticks - travelled);
  return true;
}

bool try_path_follow_go_to(const Entity_System& system, const path_links_t& links,
                           entities::Path_Follow& follow, entity_uid_t node, uint32_t tick,
                           float tickrate)
{
  const std::optional<path_segment_t> segment =
      try_cut_path_segment(system, links, follow.from, follow.direction, tickrate);
  if (segment)
  {
    if (segment->to_uid == node)
      return true;
    if (segment->from_uid == node)
      return try_reverse_path_follow(system, links, follow, tick, tickrate);
    return false;
  }

  if (follow.from == node)
    return true;

  const std::optional<path_segment_t> other_way =
      try_cut_path_segment(system, links, follow.from, -follow.direction, tickrate);
  if (!other_way || other_way->to_uid != node)
    return false;
  follow.direction          = -follow.direction;
  follow.segment_start_tick = tick;
  return true;
}

} // namespace shared
