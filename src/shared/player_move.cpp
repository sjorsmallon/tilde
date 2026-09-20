#include "player_move.hpp"
#include "locomotion.hpp"
#include "log.hpp"
#include "movement_kernel.hpp"
#include "network/network_types.hpp"
#include "player_constants.hpp"
#include <algorithm>
#include <cmath>
#include "timed_function.hpp"

using namespace network;

namespace shared
{

move_input_t move_input_of(const shared::subtick_step_t& step)
{
  const float yaw_radians   = linalg::to_radians(step.view.yaw);
  const float pitch_radians = linalg::to_radians(step.view.pitch);
  const float cos_yaw       = std::cos(yaw_radians);
  const float sin_yaw       = std::sin(yaw_radians);
  const float cos_pitch     = std::cos(pitch_radians);
  const float sin_pitch     = std::sin(pitch_radians);

  const vec3 front = {cos_yaw * cos_pitch, sin_pitch, sin_yaw * cos_pitch};
  //@FIXME(SJM): up vector global?
  vec3        right        = linalg::cross(front, vec3{0.f, 1.f, 0.f});
  const float right_length = linalg::length(right);
  if (right_length > 0.001f)
  {
    right = right * (1.0f / right_length);
  }
  else
  {
    log_warning("arbitrarily deciding that right is {{1, 0, 0}} because the vector length was too small.");
    right = {1.f, 0.f, 0.f};
  }

  return {.buttons   = move_input_from_buttons(step.buttons),
          .front     = front,
          .right     = right,
          .aim_sweep = aim_sweep_of(step),
          .dt        = step.dt};
}

} // namespace shared

// Exposed functions
shared::move_state_t player_move(const shared::movement_settings_t& settings,
                                 const Bounding_Volume_Hierarchy& bvh,
                                 const shared::predicted_world_t& world,
                                 shared::move_state_t state, const shared::move_input_t& input,
                                 Move_Events* out_events,
                                 debug_collision::Face_Bucket* debug_faces)
{
  timed_function();

  if (input.aim_sweep.push_count == 0)
    fatal_error("player_move: aim_sweep.push_count is {}, which divides the step by zero",
                input.aim_sweep.push_count);

  // Resolved once for the whole tick: every resolve_collisions call below must
  // agree about whether it is recording, or a mid-tick console toggle would
  // record half a frame's faces. A caller with no bucket records nothing whatever
  // the toggle says -- that is how the server opts out (debug_collision.hpp).
  debug_collision::Face_Bucket* recording_bucket =
      settings.record_collisions ? debug_faces : nullptr;

  const float         dt       = input.dt;
  entities::Movement& movement = state.movement;

  // --- 1 SENSE ---
  //
  // The caller's position is at the FEET; everything below works on the hull centre.
  const vec3 hull_center_offset{0.f, settings.shared.half_height, 0.f};
  vec3       hull_center = state.feet + hull_center_offset;
  const shared::contacts_t contacts =
      shared::resolve_collisions(settings, bvh, world, hull_center, recording_bucket);

  // we are grounded if (and only if):
  // - the ground trace hits.
  // - y velocity is going down. (at least not going up.)
  const bool grounded = contacts.has_ground() && (state.velocity.y <= 0.0f);

  // --- 2 DECIDE: a jump, then the reel or the model ---
  //
  // The jump is the one ability every model shares, and it is spent HERE
  // rather than in one of them: a charge is state, and two models spending it
  // two ways is the divergence the replay cannot see.
  const shared::jump_t jump = shared::try_jump(settings, grounded, state, input);

  // THE HOOK'S REEL. It is a branch rather than a velocity written from outside
  // because the hooked player predicts their own movement: an unpredicted pull
  // rubber-bands them for a round trip, which is the problem prediction_def.md
  // §1 solved for pads. The direction is re-derived from the CURRENT positions
  // every step, so the anchor moving drags the victim along for free and there
  // is no path to store.
  //
  // Detach is tested BEFORE the step, so the arrival radius is the distance the
  // reel stops at rather than one the step can carry the hull past.
  bool reeling               = movement.seconds_of_hook_pull_remaining > 0.f;
  bool hook_released         = false;
  vec3 hook_release_position = {};
  vec3 hook_release_velocity = {};
  if (reeling)
  {
    const vec3  to_anchor          = movement.hook_anchor_position - hull_center;
    const float distance_to_anchor = length(to_anchor);
    if (distance_to_anchor <= settings.hook.arrive_radius)
    {
      movement.seconds_of_hook_pull_remaining = 0.f;
      movement.hook_anchor_uid                = shared::null_entity_uid;
      shared::apply_impulse(settings, state,
                            {.horizontal = shared::impulse_mode_t::Set,
                             .vertical   = shared::impulse_mode_t::Set,
                             .velocity   = state.velocity});
      reeling                = false;
      hook_released          = true;
      hook_release_position  = hull_center;
      hook_release_velocity  = state.velocity;
    }
  }

  vec3 new_center   = hull_center;
  vec3 new_velocity = state.velocity;

  if (reeling)
  {
    // No accel, no friction, no gravity: the velocity IS the reel, so
    // anything else would be a second author of it. Walls are left to the
    // post-move resolve below -- a reel that grinds into a corner is held
    // there until the timer lets go.
    //
    // BOTH ends of the step are clamped so the split into sub-steps lands in
    // the same place. The DISTANCE cap targets the arrival radius rather than
    // the anchor, so a reel converges on exactly that sphere instead of
    // stopping wherever a step happened to carry it inside; the TIME cap
    // spends only the pull that is left, so the travel is reel_speed times
    // the pull duration however the tick was cut.
    const vec3  to_anchor          = movement.hook_anchor_position - hull_center;
    const float distance_to_anchor = length(to_anchor);
    const float distance_remaining =
        std::max(distance_to_anchor - settings.hook.arrive_radius, 0.f);
    const float reel_dt = std::min(dt, movement.seconds_of_hook_pull_remaining);
    const vec3  reel_direction =
        distance_to_anchor > 0.f ? to_anchor * (1.f / distance_to_anchor) : vec3{};

    // The distance cap clamps the STEP, never the speed: a reel that arrives
    // mid-step still lets go at full reel speed, so what you are flung with
    // is the tunable rather than whatever fraction of a step was left. That
    // is also what makes the release velocity the same however the tick was
    // cut -- the last step's leftover is a function of the split.
    const float travel = std::min(settings.hook.reel_speed * reel_dt, distance_remaining);

    new_velocity = reel_direction * settings.hook.reel_speed;
    new_center   = hull_center + reel_direction * travel;

    movement.seconds_of_hook_pull_remaining -= dt;
    if (movement.seconds_of_hook_pull_remaining <= 0.f)
    {
      movement.seconds_of_hook_pull_remaining = 0.f;
      movement.hook_anchor_uid                = shared::null_entity_uid;
      shared::apply_impulse(settings, state,
                            {.horizontal = shared::impulse_mode_t::Set,
                             .vertical   = shared::impulse_mode_t::Set,
                             .velocity   = new_velocity});
      hook_released         = true;
      hook_release_position = new_center;
      hook_release_velocity = new_velocity;
    }
  }
  else
  {
    const vec3           wish_direction = shared::flat_wish_direction(input);
    shared::stair_step_t stair{};
    if (grounded && !jump.from_ground && length(wish_direction) > 0.f &&
        !contacts.wall_planes.empty())
      stair = shared::try_stair_step(settings, bvh, world, contacts, state, input, hull_center,
                                     wish_direction, recording_bucket);

    if (stair.taken)
    {
      new_center   = stair.hull_center;
      new_velocity = stair.velocity;
    }
    else
    {
      // --- 3 SLIDE ---
      const shared::wanted_move_t wanted =
          shared::decide_move(settings, contacts, grounded, jump.velocity, state, input);
      const shared::slide_result_t slid =
          shared::slide(settings, contacts, grounded, wanted, hull_center, dt);
      new_center   = slid.hull_center;
      new_velocity = slid.velocity;
    }
  }

  const shared::settled_move_t settled = shared::resolve_after_move(
      settings, bvh, world, new_center, new_velocity, recording_bucket);

  state.feet     = settled.hull_center - hull_center_offset;
  state.velocity = settled.velocity;

  // --- 4 CLOCKS: the only place per-player movement state is written ---
  //
  // Off the POST-move resolve, which is the honest end-of-step answer: the
  // pre-move `grounded` above also demands a non-rising velocity, because that
  // is what makes a jump leave the ground, so it answers "may I walk" rather
  // than "am I touching floor". An ability budget keys off the second question
  // -- a player rising off a ledge has not left the ground yet.
  if (settled.grounded)
  {
    // Landing is what refills the budget, not jumping. Tying it to the jump
    // would let a player who walked off a ledge spend charges they never
    // earned back, and tying it to the pre-move test would refill them on the
    // rising half of a ground jump.
    movement.air_jumps_used              = 0;
    movement.time_since_grounded_seconds = 0.f;
  }
  else
  {
    // ACCUMULATED rather than re-derived from a tick number, which is what
    // makes it step-invariant: N sub-steps summing to one tick's dt add the
    // same total as one step of it.
    movement.time_since_grounded_seconds += dt;
  }

  movement.is_grounded      = settled.grounded;
  movement.ground_mover_uid = settled.ground_mover_uid;

  // Counted down HERE rather than at the fire site for the same reason
  // time_since_grounded_seconds is accumulated here: N sub-steps summing to one
  // tick's dt have to spend the same cooldown as one step of it, and this is
  // the function every step goes through. The impulse itself is applied by
  // try_apply_self_impulse (weapons.hpp) at the trigger edge, which is outside
  // -- player_move knows about movement state, not about what is in the hand.
  //
  // Clamped at zero rather than allowed to run negative, so "ready" is one
  // value and not any of them.
  movement.seconds_until_impulse_ready =
      std::max(0.f, movement.seconds_until_impulse_ready - dt);
  movement.seconds_until_speed_returns_to_base_speed =
      std::max(0.f, movement.seconds_until_speed_returns_to_base_speed - dt);

  // The edge's other half, written last so the next step compares against what
  // this one actually saw.
  movement.jump_was_held = input.buttons.jump_pressed;

  // --- 5 TOUCH: a pad launches the step that carried us into it ---
  //
  // After the ground snap on purpose. A player falling onto a pad LANDS and is
  // then thrown, so the land is still reported (it happened) and the launch is
  // the velocity the step ends with, instead of being flattened to zero by the
  // snap. Sub-tick invariant for free: the step that carries the hull in is the
  // step that launches it, on both sides, so an edge count changes nothing.
  //
  // After the clocks for the same reason: a launch at the end of a step has
  // not spent any of what it borrows yet.
  //
  // The latch fires on the first overlapped volume that is NOT the one we are
  // already latched to, which is what makes stepping from one pad straight onto
  // another fire twice while standing on one fires once.
  const shared::volume_touch_t touch =
      shared::touch_movement_volumes(settings, world.movement_volumes, state);

  if (out_events)
  {
    out_events->jumped            = jump.from_ground || jump.in_air;
    out_events->landed            = settled.land_impact_speed > 0.f;
    out_events->land_impact_speed = settled.land_impact_speed;
    out_events->launched_by_pad   = touch.launched;
    out_events->pad_uid           = touch.uid;
    out_events->pad_kind          = touch.kind;
    out_events->hook_released         = hook_released;
    out_events->hook_release_position = hook_release_position;
    out_events->hook_release_velocity = hook_release_velocity;
  }

  return state;
}

mover_push_t push_player_by_movers(const Bounding_Volume_Hierarchy& bvh,
                                   const shared::predicted_world_t& world,
                                   const entities::Movement& movement, const vec3& feet,
                                   float half_width, float half_height)
{
  const Span<const shared::mover_t> movers = world.movers;

  const vec3 hull_center_offset{0.f, half_height, 0.f};
  mover_push_t result{.feet = feet};
  std::vector<shared::entity_uid_t> pushers;

  for (const shared::mover_t& mover : movers)
  {
    if (mover.pieces.empty())
      continue;

    if (movement.ground_mover_uid != mover.uid)
    {
      const vec3 center = result.feet + hull_center_offset;
      if (!shared::aabbs_intersect(shared::hull_aabb(center, half_width, half_height),
                                   mover.swept_bounds))
        continue;

      bool struck = false;
      for (const shared::collision_piece_t& piece : mover.pieces)
        struck = struck || shared::hull_penetration_depth(piece.planes, center, half_width,
                                                          half_height) > MOVER_STRIKE_DEPTH;
      if (!struck)
        continue;
    }

    const vec3 local = linalg::rotate(linalg::inverse(mover.pose_at_tick_start.orientation),
                                      result.feet - mover.pose_at_tick_start.position);
    result.feet = mover.pose_at_tick_end.position +
                  linalg::rotate(mover.pose_at_tick_end.orientation, local);
    pushers.push_back(mover.uid);
  }

  if (pushers.empty())
    return result;

  const vec3 center = result.feet + hull_center_offset;
  std::vector<shared::collision_candidate_t> overlapping;
  shared::collect_collision_candidates(
      bvh, world, shared::hull_aabb(center, half_width, half_height), overlapping);
  for (const shared::collision_candidate_t& candidate : overlapping)
  {
    if (std::find(pushers.begin(), pushers.end(), candidate.mover_uid) != pushers.end())
      continue;
    if (shared::hull_penetration_depth(*candidate.collision_planes, center, half_width,
                                       half_height) > MOVER_CRUSH_DEPTH)
    {
      result.crushed_by = pushers.back();
      break;
    }
  }
  return result;
}
