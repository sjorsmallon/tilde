#include "player_move.hpp"
#include "network/network_types.hpp"
#include "debug_collision.hpp"
#include <cmath>
#include <print>
#include "timed_function.hpp"

using namespace network;

using cvars::cvar_state_t;

namespace
{

// Protocol constant: input range is -127..+127, not a gameplay tunable.
constexpr float pm_input_axial_extreme = 127.f;

// wish_direction is normalized, new_velocity is not.
[[nodiscard]]
auto accelerate(vec3 new_velocity, vec3 wish_direction, float wish_speed,
                float acceleration, float dt) -> vec3
{
  float current_speed_in_wish_direction = dot(new_velocity, wish_direction);
  float add_speed = wish_speed - current_speed_in_wish_direction;

  if (add_speed < 0.0f)
    return new_velocity;

  float acceleration_speed = acceleration * dt * wish_speed;

  if (acceleration_speed > add_speed)
    acceleration_speed = add_speed;

  vec3 result = new_velocity + (acceleration_speed * wish_direction);

  return result;
}

[[nodiscard]]
auto step_air_move(const cvar_state_t &cvars, const vec3 &old_position,
                   vec3 &new_velocity, const float dt)
    -> std::tuple<vec3, vec3>
{
  const float maxspeed = cvars.pm_maxspeed;

  // clip the speed in the horizontal plane to maxspeed.
  float speed =
      sqrt(new_velocity.x * new_velocity.x + new_velocity.z * new_velocity.z);
  if (speed > maxspeed)
  {
    speed = maxspeed;
    float y = new_velocity.y;

    auto new_vector = vec3{new_velocity.x, 0.0f, new_velocity.z};
    new_vector = normalize(new_vector);
    new_vector = new_vector * speed;
    new_velocity = vec3{new_vector.x, y, new_vector.z};
  }

  vec3 position = old_position + (new_velocity * dt);

  // @FIXME: test if we can actually be at the new position (collide with the
  // environment and push back). we need to perform a new trace here to prevent
  // tunneling / getting stuck in the ground.

  return std::make_tuple(position, new_velocity);
}

[[nodiscard]] std::tuple<vec3, vec3> step_slide_move(const cvar_state_t &cvars,
                                                     const vec3 &old_position,
                                                     vec3 &new_velocity,
                                                     bool ground_collided,
                                                     const float dt)
{
  const float maxspeed = cvars.pm_maxspeed;

  // clip the speed in the horizontal plane to maxspeed.
  float speed =
      sqrt(new_velocity.x * new_velocity.x + new_velocity.z * new_velocity.z);
  if (speed > maxspeed)
  {
    speed = maxspeed;
    float y = new_velocity.y;

    auto new_vector = vec3{new_velocity.x, 0.0f, new_velocity.z};
    new_vector = normalize(new_vector);
    new_vector = new_vector * speed;
    new_velocity = vec3{new_vector.x, y, new_vector.z};
  }

  // NOTE: integrate position BEFORE snapping Y velocity. This order matters!
  // On slopes, the ground clip gives velocity a Y component so the player
  // follows the surface. If we zeroed Y first, the position would move
  // purely horizontally — floating off the slope, losing ground contact, and
  // falling into air mode (which has no friction and no jump). The snap
  // afterward prevents Y from accumulating across frames.
  //
  // We snap ALL Y (not just negative) because the overbounce factor in
  // clip_vector can produce tiny positive Y on slopes too. Even a small
  // positive Y fails the grounded check (vel_y <= 0) next frame, kicking
  // the player into air mode where gravity builds up negative Y.
  // The jump is injected by the caller AFTER this function returns, so
  // it is not affected by this snap.
  vec3 position = old_position + (new_velocity * dt);

  if (ground_collided)
  {
    new_velocity.y = 0.f;
  }

  return std::make_tuple(position, new_velocity);
}

//@NOTE(SJM):
// speed_drop = speed * friction * dt bills you for friction at the speed you're currently going. Take one step and that's fine. Take two half-steps and the second one is charged against a different, already-reduced speed:
// friction = 4, dt = 1/60  →  friction*dt = 0.0667
// start: speed = 300

// one full step:    drop = 300 * 0.0667  = 20.00        → 280.000

// two half steps:   drop = 300 * 0.0333  =  9.999       → 290.001
//                   drop = 290 * 0.0333  =  9.667       → 280.334
//                                           ^^^^^
//                               charged against 290, not 300
auto apply_friction(const cvar_state_t &cvars, vec3 old_velocity, float dt) -> vec3
{
  // snap to only planar movement.
  old_velocity.y = 0.f;

  float speed = length(old_velocity);
  // if we are very small moving, instead of infinitely applying drag, just snap
  // stop.
  if (speed < cvars.pm_speed_threshold)
  {
    return vec3{};
  }

  // exponential decay composes exactly under any subdivision of dt.
  if (speed >= cvars.pm_stopspeed)
    return old_velocity * std::exp(-cvars.pm_friction * dt);

  float adjusted_speed = speed - cvars.pm_stopspeed * cvars.pm_friction * dt;

  // cannot move in the negatives.
  if (adjusted_speed < 0.0f)
    adjusted_speed = 0.0f;

  return old_velocity * (adjusted_speed / speed);
}

// since input can be provided -127 -> +127, scale the movement vector based on
// the input delivered.
[[nodiscard]] float calculate_input_scale(const float forward_move,
                                          const float right_move,
                                          const float up_move,
                                          const float max_speed,
                                          const float input_axial_extreme)
{

  int max = abs(static_cast<int>(forward_move));
  if (abs(static_cast<int>(right_move)) > max)
    max = abs(static_cast<int>(right_move));

  if (abs(static_cast<int>(up_move)) > max)
    max = abs(static_cast<int>(up_move));

  if (!max)
    return 0.f;

  float total = sqrt(forward_move * forward_move + right_move * right_move +
                     up_move * up_move);
  float scale =
      max_speed * static_cast<float>(max) / (input_axial_extreme * total);
  return scale;
}

//@FIXME: this should be better.
[[nodiscard]] float calculate_input_scale(const float forward_move,
                                          const float right_move,
                                          const float max_speed,
                                          const float input_axial_extreme)
{

  int max = abs(static_cast<int>(forward_move));
  if (abs(static_cast<int>(right_move)) > max)
    max = abs(static_cast<int>(right_move));

  if (!max)
    return 0.f;

  float total = sqrt(forward_move * forward_move + right_move * right_move);
  float scale =
      max_speed * static_cast<float>(max) / (input_axial_extreme * total);
  return scale;
}

bool check_jump(const Move_Input &input) { return input.jump_pressed; }

vec3 clip_vector(vec3 in, vec3 normal, const float overbounce)
{
  // how strong is the incoming vector in the direction of the face normal?
  // (i.e. we split the incoming vector in two parts: the one that is parallel
  // to the normal, and the one that is perpendicular to it (along the wall).
  float backoff = dot(in, normal);

  if (backoff < 0.0f)
  {
    backoff *= overbounce;
  }
  else
  {
    backoff /= overbounce;
  }

  vec3 change = normal * backoff;

  vec3 result = in - change;

  return result;
}

std::tuple<vec3, vec3> my_walk_move(const cvar_state_t &cvars,
                                    const Move_Input &input,
                                    bool has_ground, const vec3 &ground_normal,
                                    const Collider_Planes &collider_planes,
                                    const vec3 old_position,
                                    const vec3 old_velocity, const vec3 front,
                                    const vec3 right, const float dt)
{
  const float maxspeed = cvars.pm_maxspeed;
  const float overbounce = cvars.pm_overbounce;
  const float jumpspeed = cvars.pm_jumpspeed;

  // we know we were walking when we got here.
  bool jump_pressed_this_frame = check_jump(input);

  // do not apply friction if we are intending to jump.
  vec3 old_velocity_with_friction_applied = old_velocity;
  if (!jump_pressed_this_frame)
  {
    // apply friction. this does not fully 'nullify' the velocity (or does it?).
    old_velocity_with_friction_applied = apply_friction(
        cvars, old_velocity, dt); // at this point, y velocity is already gone.
  }

  // what inputs did we provide?
  float forward_input = pm_input_axial_extreme * input.forward_pressed -
                        pm_input_axial_extreme * input.backward_pressed;
  float right_input = pm_input_axial_extreme * input.right_pressed -
                      pm_input_axial_extreme * input.left_pressed;
  float up_input = pm_input_axial_extreme * (jump_pressed_this_frame);

  // get rid of the y component: only look at the xz plane. the y-component is
  // handled by "a different subroutine". where are we looking?
  vec3 front_without_y = vec3{front.x, 0.0f, front.z};
  vec3 right_without_y = vec3{right.x, 0.0f, right.z};

  // look at the floor below you. this is known as a "ground trace". what is the
  // normal of that face? imagine it is steep, like an incline. we do not want
  // to move inside of that, but move smoothly perpendicular to that normal. so
  // we "clip" the velocity vector such that we redirect it along that
  // perpendicular axis.
  vec3 front_clipped = front_without_y;
  vec3 right_clipped = right_without_y;

  if (has_ground)
  {
    front_clipped = clip_vector(front_without_y, ground_normal, overbounce);
    right_clipped = clip_vector(right_without_y, ground_normal, overbounce);
  }

  // don't forget to normalize: if you don't, this will be really small if you
  // look up.
  front_clipped = normalize(front_clipped);
  right_clipped = normalize(right_clipped);

  bool received_input = (input.forward_pressed || input.backward_pressed ||
                         input.left_pressed || input.right_pressed);

  // what is the resulting direction we should take, based on the new clipped
  // front and right (accounting for the walls we might be colliding with), and
  // what buttons I pressed in relation to those vectors.
  vec3 wish_direction =
      front_clipped * forward_input + right_clipped * right_input;
  vec3 normalized_wish_direction = normalize(wish_direction);

  float input_scale =
      calculate_input_scale(forward_input, right_input, up_input, maxspeed,
                            pm_input_axial_extreme);
  float wish_speed =
      0.0f; // we set this because I think some float weirdness happens when
            // taking the length of wish_direction when it is 0.

  if (received_input)
  {
    wish_speed = input_scale * length(wish_direction);
  }

  vec3 new_velocity{};

  if (wish_speed < 0.0000001f) //@FIXME: formalize the treshold.
  {
    new_velocity = old_velocity_with_friction_applied;
  }
  else
  {
    float acceleration = cvars.pm_ground_acceleration;
    new_velocity =
        accelerate(old_velocity_with_friction_applied,
                   normalized_wish_direction, wish_speed, acceleration, dt);
  }

  // clip the new velocity against the ground plane. take the length before
  // it is clipped.
  float new_speed = length(new_velocity);
  new_velocity = clip_vector(new_velocity, ground_normal, overbounce);

  // since we take the velocity before clipping. it can be we clip the movement
  // vectors (effectively reducing player speed.) but we still want to retain
  // the speed we were moving in before.
  new_velocity = normalize(new_velocity);
  new_velocity = new_speed * new_velocity;

  // readjust the velocity for all the wall collider planes.
  for (auto &collider_plane : collider_planes.wall_planes)
  {
    // we should not collide with the plane if we are trying to move away from
    // it.
    new_speed = length(new_velocity);
    new_velocity = normalize(new_velocity);
    if (dot(new_velocity, collider_plane.normal) > 0)
    {
      new_velocity = new_velocity * new_speed;
      continue;
    }

    new_velocity =
        clip_vector(new_velocity, collider_plane.normal, overbounce);

    // Speed-preserving rescale: when sliding along a wall we normalize and
    // rescale to new_speed so that touching a wall doesn't bleed speed.
    // BUT: new_velocity was a unit vector entering clip_vector, so when
    // pressing nearly perpendicular into a wall the clip leaves only a tiny
    // residual (< 0.01) pointing slightly away. Normalizing that to a unit
    // vector and rescaling to new_speed launches the player backward at full
    // speed every frame — causing an oscillation that friction eventually
    // snaps to zero. Guard: only rescale when the tangential component is
    // meaningful. A tiny residual is left as-is; friction zeroes it next frame.
    {
      float clip_len = length(new_velocity);
      if (clip_len > 0.01f)
        new_velocity = (new_velocity * (1.0f / clip_len)) * new_speed;
    }
  }

  // Set jump velocity before step_slide_move so it's integrated into position
  // immediately (no one-frame delay). When jumping, pass ground_collided=false
  // so step_slide_move's Y snap doesn't kill the jump velocity — we're
  // leaving the ground, not staying on it.
  if (jump_pressed_this_frame)
  {
    new_velocity.y = jumpspeed;
  }

  return step_slide_move(cvars, old_position, new_velocity,
                         has_ground && !jump_pressed_this_frame, dt);
}

auto my_air_move(const cvar_state_t &cvars, const Move_Input &input,
                 bool has_ground, const vec3 &ground_normal,
                 bool has_ceiling, const vec3 &ceiling_normal,
                 Collider_Planes &collider_planes, const vec3 &old_position,
                 const vec3 &old_velocity, const vec3 &front, const vec3 &right,
                 const float dt) -> std::tuple<vec3, vec3>
{
  const float maxspeed = cvars.pm_maxspeed;
  const float overbounce = cvars.pm_overbounce;
  constexpr auto world_down = vec3{0.f, -1.f, 0.f};

  vec3 old_velocity_without_y = vec3{old_velocity.x, 0.f, old_velocity.z};
  // what inputs did we provide?
  float forward_input = pm_input_axial_extreme * input.forward_pressed -
                        pm_input_axial_extreme * input.backward_pressed;
  float right_input = pm_input_axial_extreme * input.right_pressed -
                      pm_input_axial_extreme * input.left_pressed;

  // get rid of the y component: only look at the xz plane.
  vec3 front_without_y = vec3{front.x, 0.0f, front.z};
  vec3 right_without_y = vec3{right.x, 0.0f, right.z};

  vec3 front_clipped = front_without_y;
  vec3 right_clipped = right_without_y;

  if (has_ground)
  {
    front_clipped = clip_vector(front_without_y, ground_normal, overbounce);
    right_clipped = clip_vector(right_without_y, ground_normal, overbounce);
  }

  // FIX #3: normalize front_clipped and right_clipped, same as my_walk_move
  // does. Without this, two things go wrong:
  //   (a) after stripping Y from a unit vector, the XZ remainder is shorter
  //       when looking up/down, so air control weakens at steep pitch angles;
  //   (b) after clip_vector redirects the vector along the ground plane, its
  //       length changes, making wish_speed depend on the ground slope.
  // my_walk_move normalizes these (lines 265-266); air move should too.
  front_clipped = normalize(front_clipped);
  right_clipped = normalize(right_clipped);

  bool received_input = (input.forward_pressed || input.backward_pressed ||
                         input.left_pressed || input.right_pressed);

  vec3 wish_direction =
      front_clipped * forward_input + right_clipped * right_input;
  vec3 normalized_wish_direction = normalize(wish_direction);

  float input_scale = calculate_input_scale(
      forward_input, right_input, maxspeed, pm_input_axial_extreme);
  float wish_speed =
      0.0f; // we set this because I think some float weirdness happens when
            // taking the length of wish_direction when it is 0.

  if (received_input)
  {
    wish_speed = input_scale * length(wish_direction);
  }

  vec3 new_velocity{};

  if (wish_speed < 0.0000001f) //@FIXME: formalize the treshold.
  {
    // FIX #1: was `old_velocity`, which includes Y. When the code below does
    // new_speed = length(new_velocity), that 3D length is dominated by the Y
    // component (gravity). After clipping strips Y and normalize-rescale runs,
    // all that vertical speed gets pumped into XZ — making the player speed up
    // horizontally just by falling. Use the XZ-only velocity so the
    // clip/normalize/rescale below only operates on horizontal speed.
    new_velocity = old_velocity_without_y;
  }
  else
  {
    // if we are in the air, you have less control.
    float acceleration = cvars.pm_air_acceleration;
    new_velocity = accelerate(old_velocity_without_y, normalized_wish_direction,
                              wish_speed, acceleration, dt);
  }

  float new_speed = length(new_velocity);
  new_velocity = clip_vector(new_velocity, ground_normal, overbounce);
  new_velocity = normalize(new_velocity);
  new_velocity = new_speed * new_velocity;

  float new_y_velocity = old_velocity.y;

  // clip if necessary
  for (auto &collider_plane : collider_planes.wall_planes)
  {
    // we should not collide with the plane if we are trying to move away from
    // it.
    new_speed = length(new_velocity);
    new_velocity = normalize(new_velocity);

    auto cos_angle = dot(new_velocity, collider_plane.normal);
    if (cos_angle > 0.f) // are we moving away? just keep your velocity.
    {
      new_velocity = new_velocity * new_speed;
      continue;
    }

    new_velocity =
        clip_vector(new_velocity, collider_plane.normal, overbounce);

    // Same guard as my_walk_move: clip operates on a unit vector, so a
    // near-perpendicular wall press leaves a tiny residual that must not be
    // rescaled to full speed (that would cause the same oscillation/snap).
    {
      float clip_len = length(new_velocity);
      if (clip_len > 0.01f)
        new_velocity = (new_velocity * (1.0f / clip_len)) * new_speed;
    }
  }

  // clip against the ceiling.
  if (has_ceiling)
  {
    auto cos_angle_plane_world_down = dot(ceiling_normal, world_down);
    if (cos_angle_plane_world_down > 0.707f)
    {
      // if we were already moving down, it does not matter.
      new_y_velocity = (new_y_velocity < 0.f ? new_y_velocity : 0.f);
    }
  }

  // Apply gravity as two halves around the position integration. Position then
  // integrates with the step's MIDPOINT y velocity, and under a constant g the
  // midpoint IS the exact mean of the endpoints -- so this reproduces
  // p0 + v0*dt - 0.5*g*dt^2 rather than the -1.0 that integrating with the END
  // velocity gave. The returned velocity is unchanged (v0 - g*dt either way),
  // so the grounded check, the land snap and the post-move clips see what they
  // always did.
  //
  // The point is not the extra 5% of jump apex, it is that the exact parabola
  // is the one answer that does not move when dt does: the old form dropped
  // 1.0*g*dt^2 whole, 0.75 split in two, 0.625 in four. Sub-tick makes that
  // difference reachable. See player_move_step_invariance_test.
  const float half_gravity_step = 0.5f * cvars.g_gravity * dt;

  new_velocity.y = new_y_velocity - half_gravity_step;

  auto [position, velocity] =
      step_air_move(cvars, old_position, new_velocity, dt);
  velocity.y -= half_gravity_step;
  return std::make_tuple(position, velocity);
}
// Resolve collisions against the BVH using hull-plane-based penetration test.
// Pushes player_pos out of overlapping hulls and classifies contact normals.
//
// `debug_faces` is the caller's sink, already gated on debug_show_collisions by
// the caller: the recording happens in SHARED code but the drawing is
// client-side, so both the flag and the destination have to arrive from
// whichever side is simulating rather than from a global. Null means the caller
// has no reader for them (the server, every time) -- see debug_collision.hpp.
Collider_Planes resolve_collisions(const Bounding_Volume_Hierarchy &bvh,
                                   vec3 &player_pos,
                                   float half_width, float half_height,
                                   debug_collision::Face_Sink *debug_faces)
{
  Collider_Planes result;
  constexpr float cos_45 = 0.707f;

  shared::aabb_bounds_t player_aabb;
  player_aabb.min = player_pos - vec3{half_width, half_height, half_width};
  player_aabb.max = player_pos + vec3{half_width, half_height, half_width};


  std::vector<const BVH_Primitive *> overlapping;
  bvh_intersect_aabb(bvh, player_aabb, overlapping);

  for (const auto *prim : overlapping)
  {
    if (prim->collision_planes.empty())
      continue;

    // Rebuild player AABB from (potentially updated) player_pos each iteration
    player_aabb.min = player_pos - vec3{half_width, half_height, half_width};
    player_aabb.max = player_pos + vec3{half_width, half_height, half_width};

    // Hull-plane penetration test:
    // For each plane of the convex hull, compute how far the player AABB
    // penetrates past it. If the player is fully outside any face, it's
    // not inside the hull. Otherwise, push out along the least-penetrated face.
    float min_penetration = -1e30f;
    int min_plane_idx = -1;
    vec3 push_normal = {0, 0, 0};
    bool outside = false;

    for (int pi = 0; pi < (int)prim->collision_planes.size(); ++pi)
    {
      const auto &plane = prim->collision_planes[pi];
      float signed_dist = dot(player_pos - plane.point, plane.normal);
      // Support radius: how far the AABB extends along the plane normal direction
      float support_radius = half_width * fabsf(plane.normal.x) +
                              half_height * fabsf(plane.normal.y) +
                              half_width * fabsf(plane.normal.z);
      float penetration = signed_dist - support_radius;

      if (penetration >= 0.f)
      {
        // Player is fully outside this face -> not inside the hull
        outside = true;
        break;
      }

      // At ledge corners the side face often has slightly less penetration
      // than the top face, so the resolver pushes sideways (wall) instead of
      // upward (ground). Bias toward upward-facing normals so the player
      // lands on top rather than bouncing off the side.
      bool is_ground_normal = plane.normal.y > cos_45;
      float biased_penetration = penetration;
      if (is_ground_normal)
        biased_penetration += 8.f; // make ground faces win when close

      if (biased_penetration > min_penetration)
      {
        min_penetration = penetration; // store actual penetration for push amount
        push_normal = plane.normal;
        min_plane_idx = pi;
      }
    }

    if (outside)
      continue;

    // Push player out along the least-penetrated face, but keep a small
    // skin width of penetration. Without this, the player lands exactly at
    // the surface (penetration = 0), which the penetration test reads as
    // "outside" next frame — so ground contact is never detected. Leaving
    // a tiny margin ensures the next frame's test finds penetration < 0
    // and properly classifies the contact (ground/wall/ceiling).
    constexpr float skin_width = 0.01f;
    float push_amount = -min_penetration - skin_width;
    if (push_amount > 0.f)
      player_pos = player_pos + push_normal * push_amount;

    // Create a collision plane at the contact point
    Plane p;
    p.normal = push_normal;
    p.point = player_pos - push_normal * 0.01f;

    // Record collision for debug visualization
    if (debug_faces && min_plane_idx >= 0 &&
        min_plane_idx < (int)prim->face_polygons.size())
      debug_collision::record_collision(*debug_faces, p,
                                        prim->face_polygons[min_plane_idx]);

    // Classify: ground (normal pointing up), ceiling (down), wall (horizontal)
    if (push_normal.y > cos_45)
    {
      result.ground_planes.push_back(p);
    }
    else if (push_normal.y < -cos_45)
    {
      result.ceiling_planes.push_back(p);
    }
    else
    {
      result.wall_planes.push_back(p);
    }
  }

  return result;
}

} // namespace

// Exposed functions
// new position, new velocity.
std::tuple<vec3, vec3> player_move(
    const cvars::cvar_state_t &cvars,
    const Move_Input &input,
    const Bounding_Volume_Hierarchy &bvh,
    const vec3 &old_position, const vec3 &old_velocity, const vec3 &front,
    const vec3 &right, const float half_width, const float half_height,
    const float dt, Move_Events *out_events,
    debug_collision::Face_Sink *debug_faces)
{
  timed_function();

  // Resolved once for the whole tick: every resolve_collisions call below must
  // agree about whether it is recording, or a mid-tick console toggle would
  // record half a frame's faces. A caller with no sink records nothing whatever
  // the toggle says -- that is how the server opts out (debug_collision.hpp).
  debug_collision::Face_Sink *recording_sink =
      cvars.debug_show_collisions ? debug_faces : nullptr;

  // Resolve collisions: push player out of entities and collect contact planes
  vec3 player_pos = old_position;
  Collider_Planes collider_planes =
      resolve_collisions(bvh, player_pos, half_width, half_height,
                         recording_sink);

  bool has_ground = !collider_planes.ground_planes.empty();
  bool has_ceiling = !collider_planes.ceiling_planes.empty();
  vec3 ground_normal = has_ground ? collider_planes.ground_planes[0].normal : vec3{0, 1, 0};
  vec3 ceiling_normal = has_ceiling ? collider_planes.ceiling_planes[0].normal : vec3{0, -1, 0};

  // we are grounded if (and only if):
  // - the ground trace hits.
  // - y velocity is going down. (at least not going up.)
  bool grounded = has_ground && (old_velocity.y <= 0.0f);


  vec3 new_pos, new_vel;

  // Flat wish direction from input — used to determine which walls are
  // actually being pressed into. We use this instead of old_velocity because
  // old_velocity gets clipped to near-zero against a wall after the first
  // frame of contact, so it stops reporting "moving into wall" even while
  // the player keeps pressing forward.
  vec3 front_xz = normalize(vec3{front.x, 0.f, front.z});
  vec3 right_xz = normalize(vec3{right.x, 0.f, right.z});
  float fwd_in = (input.forward_pressed ? 1.f : 0.f) - (input.backward_pressed ? 1.f : 0.f);
  float rgt_in = (input.right_pressed  ? 1.f : 0.f) - (input.left_pressed     ? 1.f : 0.f);
  vec3 wish_dir_xz = front_xz * fwd_in + right_xz * rgt_in;
  bool has_wish = length(wish_dir_xz) > 0.f;
  if (has_wish)
    wish_dir_xz = normalize(wish_dir_xz);

  // Stair-step glide: if grounded and pressing into a wall, try raising the
  // player by pm_step_height and re-testing. If no wall at the raised height
  // blocks our wish direction, the obstacle is short enough to step over.
  // Walk from the raised position, then drop back down onto the surface.
  bool used_step = false;
  if (grounded && has_wish && !collider_planes.wall_planes.empty())
  {
    // Only proceed if we're actually pressing toward at least one wall.
    bool pressing_into_wall = false;
    for (const auto &plane : collider_planes.wall_planes)
    {
      if (dot(wish_dir_xz, plane.normal) < 0.f)
      {
        pressing_into_wall = true;
        break;
      }
    }

    if (pressing_into_wall)
    {
      const float step_height = cvars.pm_step_height;
      vec3 raised_pos = player_pos + vec3{0.f, step_height, 0.f};
      Collider_Planes raised_planes =
          resolve_collisions(bvh, raised_pos, half_width, half_height,
                             recording_sink);

      // Only abort if a raised wall specifically blocks our wish direction.
      // Walls from other nearby obstacles that we're not moving into are ignored.
      bool raised_blocks_wish = false;
      for (const auto &plane : raised_planes.wall_planes)
      {
        if (dot(wish_dir_xz, plane.normal) < 0.f)
        {
          raised_blocks_wish = true;
          break;
        }
      }

      if (!raised_blocks_wish && raised_planes.ceiling_planes.empty())
      {
        bool raised_has_ground = !raised_planes.ground_planes.empty();
        vec3 raised_ground_normal = raised_has_ground
                                        ? raised_planes.ground_planes[0].normal
                                        : vec3{0.f, 1.f, 0.f};

        // old_velocity may be near-zero: it was clipped against the wall that
        // triggered this step-up and then zeroed by friction. Reconstruct to
        // the intended running speed so the player carries momentum through
        // the step rather than shuffling across it at ~0 units/s.
        vec3 old_vel_xz = vec3{old_velocity.x, 0.f, old_velocity.z};
        if (length(old_vel_xz) < cvars.pm_speed_threshold)
          old_vel_xz = wish_dir_xz * cvars.pm_maxspeed;
        vec3 step_pos, step_vel;
        std::tie(step_pos, step_vel) =
            my_walk_move(cvars, input, true, raised_ground_normal, raised_planes,
                         raised_pos, old_vel_xz, front, right, dt);

        // Drop back down by step_height. resolve_collisions will push the
        // player up to sit on top of whatever surface is below (the step top,
        // or the original floor if we overshot).
        //
        // IMPORTANT: we also push extra forward by step_height in the wish
        // direction before dropping. Without this, the player's center is only
        // (velocity * dt) ≈ 5 units past the step edge. The step's side face
        // then has less absolute penetration than the top face, so
        // resolve_collisions pushes the player sideways (wall) instead of up
        // (ground). Adding step_height of extra horizontal offset guarantees
        // the top face always wins the SAT test regardless of step size.
        vec3 drop_pos = step_pos;
        drop_pos.x += wish_dir_xz.x * step_height;
        drop_pos.z += wish_dir_xz.z * step_height;
        drop_pos.y -= step_height;
        Collider_Planes drop_planes =
            resolve_collisions(bvh, drop_pos, half_width, half_height,
                               recording_sink);

        // Reject the step if a wall still blocks the wish direction at the
        // drop position — that means we ran into a real obstacle, not just
        // the edge of the stair we were climbing.
        bool drop_wall_blocks = false;
        for (const auto &p : drop_planes.wall_planes)
        {
          if (dot(wish_dir_xz, p.normal) < 0.f)
          {
            drop_wall_blocks = true;
            break;
          }
        }

        if (!drop_planes.ground_planes.empty() && !drop_wall_blocks)
        {
          new_pos = drop_pos;
          // Do not use step_vel here. old_velocity may have been partially
          // clipped by the wall (anywhere from 0..maxspeed), so step_vel
          // inherits that reduced speed after friction — giving the player
          // a visible slowdown on every step. The step bypasses the wall
          // entirely, so carry full speed in the wish direction instead.
          new_vel = wish_dir_xz * cvars.pm_maxspeed;
          new_vel.y = 0.f;
          used_step = true;
        }
      }
    } // if (pressing_into_wall)
  }   // if (grounded && has_wish && ...)

  if (!used_step)
  {
    if (grounded)
    {
      //@FIXME: currently, we set the y_velocity to 0 here already. because
      // my_walk_move assumes that we are grounded.
      // I do not really like that.
      vec3 old_velocity_without_y = vec3{old_velocity.x, 0.f, old_velocity.z};
      std::tie(new_pos, new_vel) =
          my_walk_move(cvars, input, has_ground, ground_normal, collider_planes,
                       player_pos, old_velocity_without_y, front, right, dt);
    }
    else
    {
      std::tie(new_pos, new_vel) =
          my_air_move(cvars, input, has_ground, ground_normal, has_ceiling,
                      ceiling_normal, collider_planes, player_pos,
                      old_velocity, front, right, dt);
    }
  }

  // A jump impulse is applied exactly when we ran the grounded walk path (not
  // the step-glide path) with jump held — that's where my_walk_move sets
  // new_velocity.y = jumpspeed. The step path discards any jump, so exclude it.
  bool jumped = grounded && input.jump_pressed && !used_step;

  // Post-move collision resolve: push position out of any geometry we
  // tunneled into, and correct velocity so it doesn't fight the surface.
  Collider_Planes post_planes =
      resolve_collisions(bvh, new_pos, half_width, half_height,
                         recording_sink);

  const float overbounce = cvars.pm_overbounce;

  // Ground: the pre-move resolve uses a penetration test, so the player must
  // be *inside* geometry for has_ground to be true. But this resolve pushes
  // the player to exactly the surface (penetration = 0), which reads as
  // "outside" next frame — so the pre-move resolve won't detect ground.
  // Without this snap, the player enters air_move, gravity accumulates
  // unchecked (-3800+), and the post-move keeps pushing them back each frame
  // in an invisible free-fall loop.
  //
  // We do a plain Y=0 snap here, NOT clip_vector — overbounce would push
  // velocity slightly upward, which fails the grounded check (vel_y <= 0).
  // This snap firing on a downward velocity *is* a landing: the player came in
  // falling (air_move leaves new_vel.y negative) and the ground arrests it.
  // Walking/resting on flat ground never trips this — my_walk_move zeroes y, so
  // new_vel.y is already 0. The arrested speed is the impact magnitude.
  float land_impact_speed = 0.f;
  if (!post_planes.ground_planes.empty() && new_vel.y < 0.f)
  {
    land_impact_speed = -new_vel.y;
    new_vel.y = 0.f;
  }

  for (const auto &plane : post_planes.ceiling_planes)
  {
    if (dot(new_vel, plane.normal) < 0.f)
      new_vel = clip_vector(new_vel, plane.normal, overbounce);
  }
  for (const auto &plane : post_planes.wall_planes)
  {
    if (dot(new_vel, plane.normal) < 0.f)
      new_vel = clip_vector(new_vel, plane.normal, overbounce);
  }

  if (out_events)
  {
    out_events->jumped            = jumped;
    out_events->landed            = land_impact_speed > 0.f;
    out_events->land_impact_speed = land_impact_speed;
  }

  return {new_pos, new_vel};
}
