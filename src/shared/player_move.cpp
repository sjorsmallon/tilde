#include "player_move.hpp"
#include "network/network_types.hpp"
#include <print>

using namespace network;

// Movement cvars
cvar::CVar<float> pm_maxspeed("pm_maxspeed", 320.f, "Maximum player speed", cvar::flags::Replicated);
cvar::CVar<float> pm_stopspeed("pm_stopspeed", 100.f, "Deceleration threshold", cvar::flags::Replicated);
cvar::CVar<float> pm_friction("pm_friction", 6.f, "Ground friction", cvar::flags::Replicated);
cvar::CVar<float> pm_ground_acceleration("pm_ground_acceleration", 10.f, "Ground acceleration", cvar::flags::Replicated);
cvar::CVar<float> pm_air_acceleration("pm_air_acceleration", 5.f, "Air acceleration", cvar::flags::Replicated);
cvar::CVar<float> pm_overbounce("pm_overbounce", 1.001f, "Plane clip overbounce factor", cvar::flags::Replicated);
cvar::CVar<float> pm_jumpspeed("pm_jumpspeed", 270.f, "Jump velocity", cvar::flags::Replicated);
cvar::CVar<float> g_gravity("g_gravity", 800.f, "Gravity", cvar::flags::Replicated);
cvar::CVar<float> pm_speed_threshold("pm_speed_threshold", 1.f, "Speed below which friction snaps to zero", cvar::flags::Replicated);

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
auto step_air_move(const vec3 &old_position, vec3 &new_velocity, const float dt)
    -> std::tuple<vec3, vec3>
{
  const float maxspeed = pm_maxspeed.Get();

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

[[nodiscard]] std::tuple<vec3, vec3> step_slide_move(const vec3 &old_position,
                                                     vec3 &new_velocity,
                                                     bool ground_collided,
                                                     const float dt)
{
  const float maxspeed = pm_maxspeed.Get();

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

  // @FIXME: we need to perform a new trace here to prevent tunneling / getting
  // stuck in the ground. did we collide with a trace, but are we moving down?
  if (ground_collided && new_velocity.y < 0.f)
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
  // snap to only planar movement.
  old_velocity.y = 0.f;

  float speed = length(old_velocity);
  // if we are very small moving, instead of infinitely applying drag, just snap
  // stop.
  if (speed < pm_speed_threshold.Get())
  {
    return vec3{};
  }

  float speed_drop = 0.0f;

  float stopspeed = pm_stopspeed.Get();
  float control = speed < stopspeed ? stopspeed : speed;
  speed_drop += control * pm_friction.Get() * dt;

  // adjust the speed with the induced speed drop.
  float adjusted_speed = speed - speed_drop;

  // cannot move in the negatives.
  if (adjusted_speed < 0.0f)
    adjusted_speed = 0.0f;

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

std::tuple<vec3, vec3> my_walk_move(Move_Input &input,
                                    bool has_ground, const vec3 &ground_normal,
                                    const Collider_Planes &collider_planes,
                                    const vec3 old_position,
                                    const vec3 old_velocity, const vec3 front,
                                    const vec3 right, const float dt)
{
  const float maxspeed = pm_maxspeed.Get();
  const float overbounce = pm_overbounce.Get();
  const float jumpspeed = pm_jumpspeed.Get();

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
    float acceleration = pm_ground_acceleration.Get();
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
    new_velocity = new_velocity * new_speed;
  }

  // we are missing where to inject the jump. so let me just do that here.
  // a jump is not a velocity, but just a "set speed" for one particular frame,
  // that gets removed over time with gravity.
  if (jump_pressed_this_frame)
  {
    std::print("induced jump speed: {}\n", jumpspeed * input_scale);
    new_velocity.y = jumpspeed;
  }

  return step_slide_move(old_position, new_velocity, has_ground, dt);
}

auto my_air_move(Move_Input &input,
                 bool has_ground, const vec3 &ground_normal,
                 bool has_ceiling, const vec3 &ceiling_normal,
                 Collider_Planes &collider_planes, const vec3 &old_position,
                 const vec3 &old_velocity, const vec3 &front, const vec3 &right,
                 const float dt) -> std::tuple<vec3, vec3>
{
  const float maxspeed = pm_maxspeed.Get();
  const float overbounce = pm_overbounce.Get();
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
    new_velocity = old_velocity; // uh.. how do we apply gravity now?
  }
  else
  {
    // if we are in the air, you have less control.
    float acceleration = pm_air_acceleration.Get();
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
    new_velocity = new_velocity * new_speed;
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

  // apply gravity.
  new_velocity.y = new_y_velocity;
  new_velocity.y -= g_gravity.Get() * dt;

  return step_air_move(old_position, new_velocity, dt);
}
// Resolve collisions against the BVH using hull-plane-based penetration test.
// Pushes player_pos out of overlapping hulls and classifies contact normals.
Collider_Planes resolve_collisions(const Bounding_Volume_Hierarchy &bvh,
                                   vec3 &player_pos,
                                   float half_width, float half_height)
{
  Collider_Planes result;
  constexpr float cos_45 = 0.707f;

  AABB player_aabb;
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
    vec3 push_normal = {0, 0, 0};
    bool outside = false;

    for (const auto &plane : prim->collision_planes)
    {
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

      if (penetration > min_penetration)
      {
        min_penetration = penetration;
        push_normal = plane.normal;
      }
    }

    if (outside)
      continue;

    // Push player out along the least-penetrated face
    player_pos = player_pos + push_normal * (-min_penetration);

    // Create a collision plane at the contact point
    Plane p;
    p.normal = push_normal;
    p.point = player_pos - push_normal * 0.01f;

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

std::tuple<vec3, vec3> player_move(
    Move_Input &input,
    const Bounding_Volume_Hierarchy &bvh,
    const vec3 &old_position, const vec3 &old_velocity, const vec3 &front,
    const vec3 &right, float half_width, float half_height, const float dt)
{
  // Resolve collisions: push player out of entities and collect contact planes
  vec3 player_pos = old_position;
  Collider_Planes collider_planes =
      resolve_collisions(bvh, player_pos, half_width, half_height);

  bool has_ground = !collider_planes.ground_planes.empty();
  bool has_ceiling = !collider_planes.ceiling_planes.empty();
  vec3 ground_normal = has_ground ? collider_planes.ground_planes[0].normal : vec3{0, 1, 0};
  vec3 ceiling_normal = has_ceiling ? collider_planes.ceiling_planes[0].normal : vec3{0, -1, 0};

  // we are grounded if (and only if):
  // - the ground trace hits.
  // - y velocity is going down. (at least not going up.)
  bool grounded = has_ground && (old_velocity.y <= 0.0f);

  if (grounded)
  {
    //@FIXME: currently, we set the y_velocity to 0 here already. because
    // my_walk_move assumes that we are grounded.
    // I do not really like that.
    vec3 old_velocity_without_y = vec3{old_velocity.x, 0.f, old_velocity.z};
    return my_walk_move(input, has_ground, ground_normal, collider_planes,
                        player_pos, old_velocity_without_y, front, right, dt);
  }
  else
  {
    return my_air_move(input, has_ground, ground_normal, has_ceiling,
                       ceiling_normal, collider_planes, player_pos,
                       old_velocity, front, right, dt);
  }
}
