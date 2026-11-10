#include "entities/generated/entities/player_entity_generated.hpp"
#include "entities/generated/entities/trail_entity_generated.hpp"
#include "trail_system.hpp"

#include "../../shared/collision/trail.hpp"
#include "../../shared/log.hpp"
#include "../../shared/movement/player_constants.hpp"
#include "../damage.hpp"

#include <optional>
#include <vector>

namespace server
{

namespace
{

void end_trail(entities::Trail_Entity& trail, uint32_t tick)
{
  trail.owner_uid  = shared::null_entity_uid;
  trail.ended_tick = tick;
}

// The edges of a trail: one between each pair of points, and the live one from the last point to the owner's
// feet while it has an owner. Edge `index` starts at points[index].
uint32_t count_trail_edges(const entities::Trail_Entity& trail, bool owned)
{
  if (trail.points.empty())
    return 0;
  return trail.points.size() - 1 + (owned ? 1u : 0u);
}

void grow_trails(server_context_t& context)
{
  shared::Entity_System& system = context.world.session.entity_system;
  for (entities::Trail_Entity& trail : system.entities_of_type<entities::Trail_Entity>())
  {
    if (trail.owner_uid == shared::null_entity_uid)
      continue;

    const entities::Player_Entity* owner = system.get<entities::Player_Entity>(trail.owner_uid);
    if (owner == nullptr || owner->health.current_health <= 0)
    {
      end_trail(trail, context.tick_number);
      continue;
    }

    if (trail.points.empty())
    {
      if (!trail.points.try_push_back(owner->position))
        fatal_error("update_trails: an empty trail has no room for a point");
      continue;
    }

    const uint32_t                     count = trail.points.size();
    const std::optional<linalg::vec3f> moving = shared::try_compute_direction(owner->velocity);
    const std::optional<linalg::vec3f> along_last_edge =
        count >= 2 ? shared::try_compute_direction(trail.points[count - 1] - trail.points[count - 2]) : std::nullopt;
    if (!moving && !along_last_edge)
      continue;

    const linalg::vec3f heading         = moving ? *moving : *along_last_edge;
    const linalg::vec3f heading_at_last = along_last_edge ? *along_last_edge : heading;
    const std::optional<linalg::vec3f> next = shared::try_next_flight_path_vertex(
        trail.points[count - 1], heading_at_last, shared::TRAIL_RULE, owner->position, heading);
    if (!next)
      continue;

    if (!trail.points.try_push_back(*next))
    {
      log_warning("update_trails: trail {} of player {} is full at {} points and ends", trail.entity_id,
                  owner->entity_id, trail.points.capacity);
      end_trail(trail, context.tick_number);
    }
  }
}

void hurt_in_trails(server_context_t& context)
{
  shared::Entity_System&         system   = context.world.session.entity_system;
  const shared::trail_settings_t settings = {.half_width = context.cvars->mp_trail_half_width,
                                             .height     = context.cvars->mp_trail_height};
  const float                    damage   = context.cvars->mp_trail_damage;

  std::vector<pending_hit_t> hits;
  for (const entities::Trail_Entity& trail : system.entities_of_type<entities::Trail_Entity>())
  {
    const entities::Player_Entity* owner =
        trail.owner_uid != shared::null_entity_uid ? system.get<entities::Player_Entity>(trail.owner_uid) : nullptr;
    const uint32_t edge_count = count_trail_edges(trail, owner != nullptr);
    if (edge_count == 0)
      continue;

    // ignore the last placed element since we partially occupy that space.
    const uint32_t owners_first_lagging_edge =
        owner != nullptr ? shared::find_first_edge_within_lag_distance(trail.points, owner->position,
                                                                       context.cvars->mp_trail_lag_distance)
                         : edge_count;

    for (uint32_t index = 0; index < edge_count; ++index)
    {
      const linalg::vec3f end = index + 1 < trail.points.size() ? trail.points[index + 1] : owner->position;
      const std::optional<shared::oriented_box_t> box =
          shared::try_compute_trail_segment_box(trail.points[index], end, settings);
      if (!box)
        continue;
      const shared::aabb_bounds_t box_bounds = shared::get_bounds(*box);

      for (const entities::Player_Entity& player : system.entities_of_type<entities::Player_Entity>())
      {
        if (player.health.current_health <= 0)
          continue;
        if (player.entity_id == trail.owner_uid && index >= owners_first_lagging_edge)
          continue;

        const shared::aabb_bounds_t hull = shared::player_hull_bounds(player.position);
        if (!shared::aabbs_intersect(box_bounds, hull) ||
            !shared::oriented_boxes_intersect(*box, shared::oriented_box_from_bounds(hull)))
          continue;

        hits.push_back({.info         = {.victim_uid      = player.entity_id,
                                         .attacker_uid    = trail.owner_uid,
                                         .inflictor_uid   = trail.entity_id,
                                         .amount          = damage,
                                         .source_position = box->center,
                                         .type            = entities::Damage_Type::Normal},
                        .impact_point = player.position});
      }
    }
  }

  if (!hits.empty())
    inflict_damage_batch(context, hits);
}

} // namespace

void begin_trail_for_player(server_context_t& context, const entities::Player_Entity& owner, uint32_t tick)
{
  end_trails_of_player(context, owner.entity_id, tick);

  shared::Entity_System&     system = context.world.session.entity_system;
  const shared::entity_uid_t uid    = system.spawn<entities::Trail_Entity>();
  entities::Trail_Entity*    trail  = system.get<entities::Trail_Entity>(uid);
  if (trail == nullptr)
  {
    log_error("begin_trail: no room to spawn a trail for player {}", owner.entity_id);
    return;
  }

  trail->owner_uid = owner.entity_id;
  trail->born_tick = tick;
  trail->position  = owner.position;
  if (!trail->points.try_push_back(owner.position))
    fatal_error("begin_trail: a new trail has no room for its first point");
}

void end_trails_of_player(server_context_t& context, shared::entity_uid_t owner_uid, uint32_t tick)
{
  for (entities::Trail_Entity& trail : context.world.session.entity_system.entities_of_type<entities::Trail_Entity>())
    if (trail.owner_uid == owner_uid)
      end_trail(trail, tick);
}

void update_trails(server_context_t& context)
{
  grow_trails(context);
  hurt_in_trails(context);
}

} // namespace server
