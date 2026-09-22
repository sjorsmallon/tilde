#include "canopy.hpp"

#include "entity_system.hpp"
#include "log.hpp"
#include "map_geometry.hpp"
#include "player_constants.hpp"

namespace shared
{

linalg::vec3f canopy_center_for(const entities::Canopy_Entity& canopy, const linalg::vec3f& carrier_feet)
{
  return carrier_feet + linalg::vec3f{0.f,
                                     2.f * player_half_height + CANOPY_CLEARANCE_ABOVE_HULL +
                                         canopy.half_extents.y,
                                     0.f};
}

void write_canopy_poses(entities::Canopy_Entity& canopy, const linalg::vec3f& carrier_feet)
{
  canopy.position_at_previous_tick = canopy.position;
  canopy.position                  = canopy_center_for(canopy, carrier_feet);
}

canopy_poses_t canopy_poses_for_tick(const entities::Canopy_Entity& canopy,
                                     const linalg::vec3f& carrier_velocity, uint32_t tick,
                                     uint32_t state_tick, float tick_interval_seconds)
{
  if (tick <= state_tick)
    fatal_error("canopy_poses_for_tick: a cut for tick {} against state written at tick {}; the "
                "state a cut reads is always older than the tick it is for",
                tick, state_tick);

  // pose(t): the previous position at state_tick - 1, the position at state_tick, and the
  // position carried along the carrier's velocity for every tick past it.
  const auto pose_at = [&](uint32_t t) -> linalg::vec3f
  {
    if (t + 1 <= state_tick)
      return canopy.position_at_previous_tick;
    if (t == state_tick)
      return canopy.position;
    return canopy.position +
           carrier_velocity * (tick_interval_seconds * static_cast<float>(t - state_tick));
  };

  return {.start = pose_at(tick - 2), .end = pose_at(tick - 1)};
}

void collect_canopies(const Entity_System& system, uint32_t tick, uint32_t state_tick,
                      float tick_interval_seconds, std::vector<mover_t>& out)
{
  for (const entities::Canopy_Entity& canopy : system.entities_of<entities::Canopy_Entity>())
  {
    const entities::Player_Entity* carrier = system.get<entities::Player_Entity>(canopy.carrier_uid);
    if (carrier == nullptr)
    {
      log_warning("canopy {} names carrier {} which this world does not hold; it is solid for nobody",
                  canopy.entity_id, canopy.carrier_uid);
      continue;
    }

    const canopy_poses_t poses =
        canopy_poses_for_tick(canopy, carrier->velocity, tick, state_tick, tick_interval_seconds);

    aabb_t box;
    box.center       = poses.end;
    box.half_extents = canopy.half_extents;

    mover_t cut;
    cut.uid                = canopy.entity_id;
    cut.pose_at_tick_start = {.position = poses.start};
    cut.pose_at_tick_end   = {.position = poses.end};
    cut.swept_bounds       = get_bounds(box);
    expand_aabb_to_include_point(cut.swept_bounds, poses.start - canopy.half_extents);
    expand_aabb_to_include_point(cut.swept_bounds, poses.start + canopy.half_extents);
    cut.crushes = false;
    cut.pieces.push_back(piece_from_aabb(box));
    out.push_back(std::move(cut));
  }
}

} // namespace shared
