#pragma once
#include "bsp.hpp"
#include "plane.hpp"
#include <algorithm>
#include <print>
#include <vector>

template <typename T>
[[nodiscard]] std::vector<T> filter_duplicates(std::vector<T> input)
{
  std::sort(input.begin(), input.end());
  auto last = std::unique(input.begin(), input.end());
  input.erase(last, input.end());
  return input;
}

/// this should also not be here but it is for now.
struct Trace
{
  bool collided;
  vec3 face_normal;
};

struct AABB_Traces
{
  Trace ground_trace;
  Trace ceiling_trace;
  Trace pos_x_trace;
  Trace neg_x_trace;
  Trace pos_z_trace;
  Trace neg_z_trace;
};

// do we need to discriminate further?
struct Collider_Planes
{
  std::vector<Plane> ground_planes;
  std::vector<Plane> ceiling_planes;
  std::vector<Plane> wall_planes;
};

//@Note: this move input is serialized and sent across the wire. I don't think
// that this is the correct place to define it.
// but I will leave it here for now.
struct Move_Input
{
  bool forward_pressed;
  bool backward_pressed;
  bool left_pressed;
  bool right_pressed;
  bool jump_pressed;
};

// Velocity is a vector. i.e. velocity = {vx, vy, vz}. both magnitude and
// direction. SPEED is how we describe the magnitude of velocity.

// wish_direction is normalized, new_velocity is not.
[[nodiscard]] vec3 accelerate(vec3 new_velocity, vec3 wish_direction,
                              float wish_speed, float acceleration, float dt)
{
  // how hard are we already moving in that direction?
  // what is the delta we need to add in order to achieve the wish_speed?
  float current_speed_in_wish_direction = dot(new_velocity, wish_direction);
  float add_speed = wish_speed - current_speed_in_wish_direction;

  //  current_speed_in_wish_direction exceeds the wish_speed in that direction,
  //  that's fine. no need to do anything.
  if (add_speed < 0.0f)
    return new_velocity;

  float acceleration_speed = acceleration * dt * wish_speed;

  // even if we overshoot the wish speed, it will be clipped in step_slide_move.
  if (acceleration_speed > add_speed)
    acceleration_speed = add_speed;

  vec3 result = new_velocity + (acceleration_speed * wish_direction);

  return result;
}

[[nodiscard]] std::tuple<vec3, vec3>
step_air_move(const vec3 &old_position, vec3 &new_velocity, const float dt)
{
  constexpr auto pm_maxspeed = 320.f; //@VOLATILE: also in my move., slide_move.

  // clip the speed in the horizontal plane to maxspeed.
  float speed =
      sqrt(new_velocity.x * new_velocity.x + new_velocity.z * new_velocity.z);
  if (speed > pm_maxspeed)
  {
    speed = pm_maxspeed;
    float y = new_velocity.y;

    auto new_vector = vec3{new_velocity.x, 0.0f, new_velocity.z};
    new_vector = normalize(new_vector);
    new_vector = new_vector * speed;
    new_velocity = vec3{new_vector.x, y, new_vector.z};
  }

  vec3 position = old_position + (new_velocity * dt);

  // @FIXME: test if we can actually be at the new position (collide with the
  // environment and push back). for now, we take this to be y = 10.f; we need
  // to perform a new trace here to prevent tunneling / getting stuck in the
  // ground.

  return std::make_tuple(position, new_velocity);
}

[[nodiscard]] std::tuple<vec3, vec3> step_slide_move(const vec3 &old_position,
                                                     vec3 &new_velocity,
                                                     const Trace &trace,
                                                     const float dt)
{
  constexpr auto pm_maxspeed = 320.f; //@VOLATILE: also in my move.

  // clip the speed in the horizontal plane to maxspeed.
  float speed =
      sqrt(new_velocity.x * new_velocity.x + new_velocity.z * new_velocity.z);
  if (speed > pm_maxspeed)
  {
    speed = pm_maxspeed;
    float y = new_velocity.y;

    auto new_vector = vec3{new_velocity.x, 0.0f, new_velocity.z};
    new_vector = normalize(new_vector);
    new_vector = new_vector * speed;
    new_velocity = vec3{new_vector.x, y, new_vector.z};
  }

  vec3 position = old_position + (new_velocity * dt);

  // test if we can actually be at the new position (collide with the
  // environment and push back). for now, we take this to be y = 10.f;
  // @FIXME: we need to perform a new trace here to prevent tunneling / getting
  // stuck in the ground. did we collide with a trace, but are we moving down?
  if (trace.collided && new_velocity.y < 0.f)
  {
    std::print("snapping to floor (setting y velocity to 0.\n");
    new_velocity.y = 0.f;
  }

  if (new_velocity.y > 0.f)
    std::print("y velocity: {}", new_velocity.y);

  return std::make_tuple(position, new_velocity);
}

auto apply_friction(vec3 old_velocity, float dt) -> vec3
{
  //@Hardcode:
  auto pm_stopspeed = 100.0f;
  auto pm_friction = 6.0f;

  // snap to only planar movement.
  old_velocity.y = 0.f;

  float speed = length(old_velocity);
  //@Hardcode: what should this be?
  // if we are very small moving, instead of infinitely applying drag, just snap
  // stop.
  const float speed_treshold = 1.0f;
  if (speed < speed_treshold)
  {
    return vec3{};
  }

  float speed_drop = 0.0f;

  float control = speed < pm_stopspeed ? pm_stopspeed : speed;
  speed_drop += control * pm_friction * dt;

  // adjust the speed with the induced speed drop.
  float adjusted_speed = speed - speed_drop;

  // cannot move in the negatives.
  if (adjusted_speed < 0.0f)
    adjusted_speed = 0.0f;

  // normalize the velocity based on the ground velocity (why?) -> how much
  // faster or slower are we going w.r.t the original vector. we could also just
  // normalize the old_velocity and multiply it by the adjusted_speed without
  // the need for dividing adjusted_speed by 'speed'.
  if (adjusted_speed > 0.0f)
    adjusted_speed /= speed;

  return old_velocity * adjusted_speed;
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

inline bool check_jump(const Move_Input &input) { return input.jump_pressed; }

// old clip vector
inline vec3 clip_vector(vec3 in, vec3 normal, const float overbounce)
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

  vec3 change = vec3{0.f, 0.f, 0.f};

  change = normal * backoff;

  vec3 result = in - change; // how the fuck does this make sense? -> this
                             // doesn't if we are only moving away. we should
                             // never be here if we are moving away.

  return result;
}

std::tuple<vec3, vec3> my_walk_move(Move_Input &input,
                                    const AABB_Traces &traces,
                                    const Collider_Planes &collider_planes,
                                    const vec3 old_position,
                                    const vec3 old_velocity, const vec3 front,
                                    const vec3 right, const float dt)
{
  constexpr auto pm_input_axial_extreme = 127.0f;
  constexpr auto pm_maxspeed = 320.f;
  constexpr auto pm_ground_acceleration = 10.f;
  // constexpr auto pm_air_acceleration = 5.0f; FIXME: this is disabled
  constexpr auto pm_overbounce = 1.001f;
  // constexpr auto pm_movement_treshold = 0.00001f; //minimum necessary
  // movement in either axis.
  constexpr auto pm_jumpspeed = 270.f;

  // we know we were walking when we got here.
  bool jump_pressed_this_frame = check_jump(input);

  // do not apply friction if we are intending to jump.
  vec3 old_velocity_with_friction_applied = old_velocity;
  if (!jump_pressed_this_frame)
  {
    // apply friction. this does not fully 'nullify' the velocity (or does it?).
    old_velocity_with_friction_applied = apply_friction(
        old_velocity, dt); // at this point, y velocity is already gone.
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
  // perpendicular axis. for now, no inclines. just flat surfaces. so the normal
  // is always {0.0f, 1.0f, 0.0f};
  vec3 front_clipped = front_without_y;
  vec3 right_clipped = right_without_y;

  // this does not scale. that's kind of annoying.
  if (traces.ground_trace.collided)
  {
    front_clipped = clip_vector(front_without_y,
                                traces.ground_trace.face_normal, pm_overbounce);
    right_clipped = clip_vector(right_without_y,
                                traces.ground_trace.face_normal, pm_overbounce);
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
      calculate_input_scale(forward_input, right_input, up_input, pm_maxspeed,
                            pm_input_axial_extreme);
  float wish_speed =
      0.0f; // we set this because I think some float weirdness happens when
            // taking the length of wish_direction when it is 0.

  if (received_input)
  {
    // how hard did we move the joystick? -127... +127. button presses are
    // always max (127) or min (127). this makes the "wish" direction (the one
    // purely based on input) less strong.
    wish_speed = input_scale * length(wish_direction);
  }

  vec3 new_velocity{};

  if (wish_speed < 0.0000001f) //@FIXME: formalize the treshold.
  {
    new_velocity = old_velocity_with_friction_applied;
  }
  else
  {
    // if we are in the air, you have less control.
    // float acceleration = (grounded) ? pm_ground_acceleration :
    // pm_air_acceleration;
    float acceleration = pm_ground_acceleration;
    new_velocity =
        accelerate(old_velocity_with_friction_applied,
                   normalized_wish_direction, wish_speed, acceleration, dt);
  }

  // clip the new velocity it against the ground plane. take the length before
  // it is clipped.
  float new_speed = length(new_velocity);
  new_velocity =
      clip_vector(new_velocity, traces.ground_trace.face_normal, pm_overbounce);

  //@Note: this is disabled for now because it apparently does not matter (yet)
  // in the new implementation.
  // if we start seeing nan's, i'll re-enable it. - Sjors, 22-10-2024

  // not moving enough? early exit. set x and z velocity to zero, keep y in case
  // the player is falling. if (fabs(new_velocity.x) < pm_movement_treshold &&
  // fabs(new_velocity.z) < pm_movement_treshold)
  // {
  //     if (jump_pressed_this_frame && !player_is_airborne)
  //     {

  //         new_velocity.y = (pm_jumpspeed * input_scale);
  //     }else if (player_is_airborne)
  //     {
  //         new_velocity.y = old_velocity.y;
  //         new_velocity.y -= g_gravity * dt;
  //     }

  //     new_velocity = vec3{0.0f, new_velocity.y, 0.0f};
  //     vec3 new_position = old_position + new_velocity * dt;
  //     return std::make_tuple(new_position, new_velocity);
  // }

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
        clip_vector(new_velocity, collider_plane.normal, pm_overbounce);
    new_velocity = new_velocity * new_speed;
  }

  //@Note: this is disabled for now because it apparently does not matter (yet)
  // in the new implementation.
  // if we start seeing nan's, i'll re-enable it. - Sjors, 22-10-2024

  // skip checking the movement vector if the movement vector is too small.
  // if (fabs(new_velocity.x) < pm_movement_treshold && fabs(new_velocity.z) <
  // pm_movement_treshold)
  // {
  //     if (jump_pressed_this_frame && !player_is_airborne)
  //     {
  //         new_velocity.y = (pm_jumpspeed * input_scale);
  //     }else if (player_is_airborne)
  //     {
  //         new_velocity.y = old_velocity.y;
  //         new_velocity.y -= g_gravity * dt;
  //     }

  //     new_velocity = vec3{0.0f, new_velocity.y, 0.0f};
  //     vec3 new_position = old_position + new_velocity * dt;
  //     return std::make_tuple(new_position, new_velocity);
  // }

  // if the player is not airborne, return grounded to true.
  // if (!player_is_airborne)
  // {
  //     grounded = true;
  // }

  // just before step_slide-move, reassign the y velocity (that has not been
  // touched up until now.) new_velocity.y = old_velocity.y;

  // we are missing where to inject the jump. so let me just do that here.
  // a jump is not a velocity, but just a "set speed" for one particular frame,
  // that gets removed over time with gravity.
  if (jump_pressed_this_frame)
  {
    std::print("induced jump speed: {}\n", pm_jumpspeed * input_scale);
    new_velocity.y = pm_jumpspeed;
  }

  return step_slide_move(old_position, new_velocity, traces.ground_trace, dt);
}

std::tuple<vec3, vec3> my_air_move(Move_Input &input, AABB_Traces &traces,
                                   Collider_Planes &collider_planes,
                                   const vec3 &old_position,
                                   const vec3 &old_velocity, const vec3 &front,
                                   const vec3 &right, const float dt)
{
  constexpr auto g_gravity = 800.f;
  constexpr auto pm_input_axial_extreme = 127.f;
  constexpr auto pm_overbounce = 1.001f;
  constexpr auto pm_maxspeed = 320.f;
  constexpr auto pm_air_acceleration = 5.0f;
  constexpr auto world_down = vec3{0.f, -1.f, 0.f};

  vec3 old_velocity_without_y = vec3{old_velocity.x, 0.f, old_velocity.z};
  // what inputs did we provide?
  float forward_input = pm_input_axial_extreme * input.forward_pressed -
                        pm_input_axial_extreme * input.backward_pressed;
  float right_input = pm_input_axial_extreme * input.right_pressed -
                      pm_input_axial_extreme * input.left_pressed;
  // this does not matter.
  // float up_input      = pm_input_axial_extreme * (jump_pressed_this_frame);

  // get rid of the y component: only look at the xz plane.
  // where are we looking?
  vec3 front_without_y = vec3{front.x, 0.0f, front.z};
  vec3 right_without_y = vec3{right.x, 0.0f, right.z};

  //@Note: this is naively assuming we are only checking the ground trace. how
  // do we do collisions with faces?
  // do we do that later on?

  // look at the floor below you. this is known as a "ground trace". what is the
  // normal of that face? imagine it is steep, like an incline. we do not want
  // to move inside of that, but move smoothly perpendicular to that normal. so
  // we "clip" the velocity vector such that we redirect it along that
  // perpendicular axis. for now, no inclines. just flat surfaces. so the normal
  // is always {0.0f, 1.0f, 0.0f};
  vec3 front_clipped = front_without_y;
  vec3 right_clipped = right_without_y;

  // for all planes:
  if (traces.ground_trace.collided)
  {
    front_clipped = clip_vector(front_without_y,
                                traces.ground_trace.face_normal, pm_overbounce);
    right_clipped = clip_vector(right_without_y,
                                traces.ground_trace.face_normal, pm_overbounce);
  }

  bool received_input = (input.forward_pressed || input.backward_pressed ||
                         input.left_pressed || input.right_pressed);

  // what is the resulting direction we should take, based on the new clipped
  // front and right (accounting for the walls we might be colliding with), and
  // what buttons I pressed in relation to those vectors.
  vec3 wish_direction =
      front_clipped * forward_input + right_clipped * right_input;
  vec3 normalized_wish_direction = normalize(wish_direction);

  float input_scale = calculate_input_scale(
      forward_input, right_input, pm_maxspeed, pm_input_axial_extreme);
  float wish_speed =
      0.0f; // we set this because I think some float weirdness happens when
            // taking the length of wish_direction when it is 0.

  if (received_input)
  {
    // how hard did we move the joystick? -127... +127. button presses are
    // always max (127) or min (127). this makes the "wish" direction (the one
    // purely based on input) less strong.
    wish_speed = input_scale * length(wish_direction);
  }

  vec3 new_velocity{};

  if (wish_speed < 0.0000001f) //@FIXME: formalize the treshold.
  {
    new_velocity = old_velocity; // uh.. how do we apply gravity now?
  }
  else
  {
    // if we are in the air, you have less control.
    float acceleration = pm_air_acceleration;
    new_velocity = accelerate(old_velocity_without_y, normalized_wish_direction,
                              wish_speed, acceleration, dt);
  }

  float new_speed = length(new_velocity);
  new_velocity =
      clip_vector(new_velocity, traces.ground_trace.face_normal, pm_overbounce);
  new_velocity = normalize(new_velocity);
  new_velocity = new_speed * new_velocity;

  float new_y_velocity = old_velocity.y;

  // clip if necessary
  for (auto &collider_plane : collider_planes.wall_planes)
  {
    //@FIXME: I don't understand if this will fix it, but i want to try anyway.
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
        clip_vector(new_velocity, collider_plane.normal, pm_overbounce);
    new_velocity = new_velocity * new_speed;
  }

  // clip against the ceiling.
  if (traces.ceiling_trace.collided)
  {
    auto cos_angle_plane_world_down =
        dot(traces.ceiling_trace.face_normal, world_down);
    if (cos_angle_plane_world_down > 0.707f)
    {
      // if we were alreading moving down, it does not matter.
      new_y_velocity = (new_y_velocity < 0.f ? new_y_velocity : 0.f);
    }
  }

  // apply gravity.
  new_velocity.y = new_y_velocity;
  new_velocity.y -= g_gravity * dt;

  return step_air_move(old_position, new_velocity, dt);
}

// new_player_position, new_player_velocity
std::tuple<vec3, vec3> player_move(
    Move_Input &input,
    Collider_Planes &collider_planes, // self-evident, I guess.@FIXME: this is
                                      // not const because we remove an element
                                      // later for the ground plane. yikes.
    const vec3 &old_position, const vec3 &old_velocity, const vec3 &front,
    const vec3 &right, const float dt)
{
  auto traces = AABB_Traces{};

  //@FIXME: just pick the first ground plane? how do we even deal with this?
  // if (collider_planes.ground_planes.size() > 1) std::print("[WARNING] more
  // than one ground plane found. picking the first one.\n");

  if (!collider_planes.ground_planes.empty())
  {
    // pick the first ground plane.
    traces.ground_trace.collided = true;
    traces.ground_trace.face_normal = collider_planes.ground_planes[0].normal;
  }

  if (collider_planes.ceiling_planes.size() > 1)
    std::print("[WARNING] more than one ceiling plane found. picking the first "
               "one.\n");

  if (!collider_planes.ceiling_planes.empty())
  {
    traces.ceiling_trace.collided = true;
    traces.ceiling_trace.face_normal = collider_planes.ceiling_planes[0].normal;
  }

  auto &ground_trace = traces.ground_trace;
  // we are grounded if (and only if):
  // - the ground trace hits.
  // - y velocity is going down. (at least not going up.)
  bool grounded = ((ground_trace.collided) && (old_velocity.y <= 0.0f));

  if (grounded)
  {
    //@FIXME: currently, we set the y_velocity to 0 here already. because
    // my_walk_move assumes that we are grounded.
    // I do not really like that.
    vec3 old_velocity_without_y = vec3{old_velocity.x, 0.f, old_velocity.z};
    return my_walk_move(input, traces, collider_planes, old_position,
                        old_velocity_without_y, front, right, dt);
  }
  else
  {
    return my_air_move(input, traces, collider_planes, old_position,
                       old_velocity, front, right, dt);
  }
}

//@Note(SMIA): I know all of this is wildly inefficient, but I need to think
// about how this works
// before I write anything more classy.
// helper functions to classify planes in the BSP.
std::tuple<Collider_Planes, std::vector<size_t>>
collect_and_classify_intersecting_planes(
    const BSP *bsp, const std::vector<vertex_xnc> &bsp_vertices,
    AABB &colliding_aabb)
{
  constexpr auto WORLD_UP = vec3{0.f, 1.f, 0.f};
  // FIXME(Sjors): formalize this value. I "found" it by walking across multiple
  // aabb and getting the lowest one, which I think is the side of the adjacent
  // aabb. are we intersecting by a large enough "penetration depth"?
  constexpr auto PENETRATION_DEPTH_TRESHOLD = 2.f;

  constexpr auto CEILING_ANGLE_COS_TRESHOLD =
      -.707f; // 45 degree angle -(cos(45 degrees)) -> -cos(0,785398 rads)
  constexpr auto FLOOR_ANGLE_COS_TRESHOLD =
      .707f; // 45 degree angle (cos(45 degrees)) -> cos(0,785398 rads)

  //@Note(Sjors): this number is pulled out of my ass. but I want to check if
  // this resolves at least the horizontal collisions.
  constexpr auto HEIGHT_OVERLAP_TRESHOLD = 5.f;

  auto all_face_indices = bsp_trace_AABB(bsp, colliding_aabb, bsp_vertices);
  all_face_indices = filter_duplicates(all_face_indices);

  auto ground_face_indices = std::vector<size_t>{};
  auto ceiling_face_indices = std::vector<size_t>{};
  auto wall_face_indices = std::vector<size_t>{};

  // filter aabb
  for (auto &face_idx : all_face_indices)
  {
    const auto &v0 = bsp_vertices[face_idx].position;
    const auto &v1 = bsp_vertices[face_idx + 1].position;
    const auto &v2 = bsp_vertices[face_idx + 2].position;

    auto normal = compute_triangle_normal(v0, v1, v2);

    float max_penetration_depth =
        calculate_max_penetration_depth(colliding_aabb, v0, v1, v2);

    if (max_penetration_depth > PENETRATION_DEPTH_TRESHOLD)
    {
      auto triangle_aabb = aabb_from_triangle(v0, v1, v2);
      auto overlap =
          vec3{.x = fabs(colliding_aabb.min.x - triangle_aabb.max.x),
               .y = fabs(colliding_aabb.min.y - triangle_aabb.max.y),
               .z = fabs(colliding_aabb.min.z - triangle_aabb.max.z)};
      auto cos_angle = dot(normal, WORLD_UP);
      if ((cos_angle > FLOOR_ANGLE_COS_TRESHOLD)) //  floor (45 degree angle)
      {
        // edge case where we come at a "floor" from the side, and we stick to
        // it. I have a feeling I need to revisit this very soon.
        if (triangle_aabb.max.y - colliding_aabb.min.y < 5.f)
        {
          ground_face_indices.push_back(face_idx);
        }
      }
      else if ((cos_angle <
                CEILING_ANGLE_COS_TRESHOLD)) // ceiling (45 degree angle)
      {
        // edge case where we come at a "ceiling" from the side, and we stick to
        // it. I have a feeling I need to revisit this very soon.
        if (fabs(triangle_aabb.max.y - colliding_aabb.max.y) < 5.f)
        {
          ceiling_face_indices.push_back(face_idx);
        }
      }
      else
      {
        // what is the height overlap? (this prevents us from "tripping" over
        // floor tiles that have a normal facing the player direction, but are
        // actually "underneath" the floor. image running over the top of
        // aligned AABBS, and then "tripping" over the transition from one AABB
        // to the other because you are "colliding" with the side of the AABB
        // you want to cross over.)
        if (overlap.y > HEIGHT_OVERLAP_TRESHOLD)
        {
          wall_face_indices.push_back(face_idx);
        }
      }
    }
  }

  // plane is combination of v0 and the calculated normal.
  auto create_planes_from_face_indices =
      [](const std::vector<size_t> &face_indices,
         const std::vector<vertex_xnc> &vertices) -> std::vector<Plane>
  {
    auto planes = std::vector<Plane>{};
    for (auto &face_idx : face_indices)
    {
      planes.push_back(
          Plane{vertices[face_idx].position,
                compute_triangle_normal(vertices[face_idx].position,
                                        vertices[face_idx + 1].position,
                                        vertices[face_idx + 2].position)});
    }

    return planes;
  };

  auto ground_planes =
      create_planes_from_face_indices(ground_face_indices, bsp_vertices);
  auto ceiling_planes =
      create_planes_from_face_indices(ceiling_face_indices, bsp_vertices);
  auto wall_planes =
      create_planes_from_face_indices(wall_face_indices, bsp_vertices);
  auto collider_planes =
      Collider_Planes{.ground_planes = std::move(ground_planes),
                      .ceiling_planes = std::move(ceiling_planes),
                      .wall_planes = std::move(wall_planes)};

  return {collider_planes, all_face_indices};
}