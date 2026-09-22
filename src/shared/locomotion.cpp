#include "locomotion.hpp"
#include "log.hpp"
#include <algorithm>
#include <cmath>

namespace shared
{

namespace
{

// Protocol constant: input range is -127..+127, not a gameplay tunable.
constexpr float pm_input_axial_extreme = 127.f;

// since input can be provided -127 -> +127, scale the movement vector based on
// the input delivered.
//@FIXME: this should be better.
[[nodiscard]] float calculate_input_scale(const float forward_move, const float right_move,
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

} // namespace

// wish_direction is normalized, new_velocity is not.
vec3 accelerate(vec3 new_velocity, vec3 wish_direction, float wish_speed, float target_speed,
                float acceleration, float dt)
{
  float current_speed_in_wish_direction = dot(new_velocity, wish_direction);
  float add_speed = target_speed - current_speed_in_wish_direction;

  if (add_speed < 0.0f)
    return new_velocity;

  float acceleration_speed = acceleration * dt * wish_speed;

  if (acceleration_speed > add_speed)
    acceleration_speed = add_speed;

  vec3 result = new_velocity + (acceleration_speed * wish_direction);

  return result;
}

vec3 rotate_about_y(const vec3& vector, const float radians)
{
  const float cosine = std::cos(radians);
  const float sine   = std::sin(radians);
  return vec3{vector.x * cosine - vector.z * sine, vector.y, vector.x * sine + vector.z * cosine};
}

vec3 last_push_direction_of(const vec3& direction, const aim_sweep_t& sweep)
{
  const float pushes = static_cast<float>(sweep.push_count);
  return rotate_about_y(direction,
                        linalg::to_radians(sweep.yaw_change_degrees) * (pushes - 0.5f) / pushes);
}

//@NOTE(SJM):
// speed_drop = speed * friction * dt bills you for friction at the speed you're currently going. Take one step and that's fine. Take two half-steps and the second one is charged against a different, already-reduced speed:
// friction = 4, dt = 1/60  →  friction*dt = 0.0667

// v' = v - friction * dt;
// start: speed = 300

// one full step:    drop = 300 * 0.0667  = 20.00        → 280.000

// two half steps:   drop = 300 * 0.0333  =  9.999       → 290.001
//                   drop = 290 * 0.0333  =  9.667       → 280.334
//                                           ^^^^^
//                               charged against 290, not 300
// Friction and acceleration are ONE system on the ground, not two operators
// applied in turn: v' = -k*v + a*w. Running them alternately is what made a
// split tick diverge even after both halves became individually exact, so this
// reports the duration the acceleration must integrate over as well as the
// decayed velocity. The exponential branch hands back (1-exp(-k*dt))/k rather
// than dt, and two halves of THAT sum to the whole:
//   t(h)*(1+exp(-k*h)) = (1-exp(-k*h))(1+exp(-k*h))/k = (1-exp(-2*k*h))/k
// which plain dt does not do.
friction_step_t apply_friction(const movement_settings_t& settings, vec3 old_velocity, float dt)
{
  // snap to only planar movement.
  old_velocity.y = 0.f;

  float speed = length(old_velocity);
  // if we are very small moving, instead of infinitely applying drag, just snap
  // stop.
  if (speed < settings.shared.speed_threshold)
  {
    return {vec3{}, dt};
  }

  // Frictionless: the decay below divides by it.
  if (settings.quake.friction <= 0.f)
    return {old_velocity, dt};

  // exponential decay composes exactly under any subdivision of dt.
  if (speed >= settings.quake.stop_speed)
  {
    const float decay = std::exp(-settings.quake.friction * dt);
    return {old_velocity * decay, (1.f - decay) / settings.quake.friction};
  }

  float adjusted_speed = speed - settings.quake.stop_speed * settings.quake.friction * dt;

  // cannot move in the negatives.
  if (adjusted_speed < 0.0f)
    adjusted_speed = 0.0f;

  // The floor is a CONSTANT drop, so there is no decay for the acceleration to
  // be weighted against and dt is already the exact duration.
  return {old_velocity * (adjusted_speed / speed), dt};
}

vec3 flat_wish_direction(const move_input_t& input)
{
  const vec3 front_xz = normalize(vec3{input.front.x, 0.f, input.front.z});
  const vec3 right_xz = normalize(vec3{input.right.x, 0.f, input.right.z});
  const float forward  = (input.buttons.forward_pressed ? 1.f : 0.f) -
                        (input.buttons.backward_pressed ? 1.f : 0.f);
  const float sideways =
      (input.buttons.right_pressed ? 1.f : 0.f) - (input.buttons.left_pressed ? 1.f : 0.f);
  return normalize(front_xz * forward + right_xz * sideways);
}

wish_t wish_of(const movement_settings_t& settings, const move_input_t& input, bool has_ground,
               const vec3& ground_normal)
{
  const Move_Input& buttons    = input.buttons;
  const float       overbounce = settings.shared.overbounce;

  // what inputs did we provide?
  float forward_input = pm_input_axial_extreme * buttons.forward_pressed -
                        pm_input_axial_extreme * buttons.backward_pressed;
  float right_input = pm_input_axial_extreme * buttons.right_pressed -
                      pm_input_axial_extreme * buttons.left_pressed;

  // get rid of the y component: only look at the xz plane. the y-component is
  // handled by "a different subroutine". where are we looking?
  vec3 front_without_y = vec3{input.front.x, 0.0f, input.front.z};
  vec3 right_without_y = vec3{input.right.x, 0.0f, input.right.z};

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

  bool received_input = (buttons.forward_pressed || buttons.backward_pressed ||
                         buttons.left_pressed || buttons.right_pressed);

  // what is the resulting direction we should take, based on the new clipped
  // front and right (accounting for the walls we might be colliding with), and
  // what buttons I pressed in relation to those vectors.
  vec3 wish_direction = front_clipped * forward_input + right_clipped * right_input;

  float input_scale = calculate_input_scale(forward_input, right_input, settings.shared.run_speed,
                                            pm_input_axial_extreme);
  float wish_speed =
      0.0f; // we set this because I think some float weirdness happens when
            // taking the length of wish_direction when it is 0.

  if (received_input)
  {
    wish_speed = input_scale * length(wish_direction);
  }

  return {.direction = normalize(wish_direction), .speed = wish_speed};
}

float horizontal_speed_limit_of(const movement_settings_t& settings, bool walking,
                                float speed_entering_move)
{
  if (!walking && !settings.quake.clip_air_speed)
    return std::numeric_limits<float>::infinity();
  return std::max(speed_entering_move, settings.shared.run_speed);
}

jump_t try_jump(const movement_settings_t& settings, bool grounded, move_state_t& state,
                const move_input_t& input)
{
  entities::Movement& movement = state.movement;

  const bool jump_edge = input.buttons.jump_pressed && !movement.jump_was_held;

  jump_t jump{.velocity    = state.velocity,
              .from_ground = grounded && input.buttons.jump_pressed,
              .in_air      = !grounded && jump_edge &&
                        (int32_t)movement.air_jumps_used < settings.shared.air_jump_count};

  // Both jumps fly their whole step, so gravity starts at the impulse whatever the step's length.
  if (jump.from_ground)
  {
    jump.velocity.y = settings.shared.jump_speed;
  }
  if (jump.in_air)
  {
    jump.velocity.y = settings.shared.air_jump_speed;
    ++movement.air_jumps_used;
  }

  const vec3 wish_direction = flat_wish_direction(input);
  if (jump.from_ground && length(wish_direction) > 0.f && settings.quake.jump_boost_speed > 0.f)
  {
    const vec3 horizontal_velocity{state.velocity.x, 0.f, state.velocity.z};
    const vec3 boosted_velocity = clip_horizontal_speed(
        horizontal_velocity + wish_direction * settings.quake.jump_boost_speed,
        std::max(length(horizontal_velocity), settings.quake.jump_boost_max_speed));
    jump.velocity.x = boosted_velocity.x;
    jump.velocity.z = boosted_velocity.z;
  }

  return jump;
}

vec3 velocity_after_impulse(const move_state_t& state, const impulse_t& impulse)
{
  vec3 result = state.velocity;
  switch (impulse.horizontal)
  {
    case impulse_mode_t::Keep:
      break;
    case impulse_mode_t::Add:
      result.x += impulse.velocity.x;
      result.z += impulse.velocity.z;
      break;
    case impulse_mode_t::Set:
      result.x = impulse.velocity.x;
      result.z = impulse.velocity.z;
      break;
  }
  switch (impulse.vertical)
  {
    case impulse_mode_t::Keep:
      break;
    case impulse_mode_t::Add:
      result.y += impulse.velocity.y;
      break;
    case impulse_mode_t::Set:
      result.y = impulse.velocity.y;
      break;
  }
  return result;
}

void apply_impulse(const movement_settings_t& settings, move_state_t& state,
                   const impulse_t& impulse)
{
  switch (settings.model)
  {
    case cvars::Locomotion_Model::quake:
      quake_impulse(settings, state, impulse);
      return;
    case cvars::Locomotion_Model::instant:
      instant_impulse(settings, state, impulse);
      return;
    case cvars::Locomotion_Model::instant_momentum:
      instant_momentum_impulse(settings, state, impulse);
      return;
    case cvars::Locomotion_Model::instant_redirect:
      instant_redirect_impulse(settings, state, impulse);
      return;
  }
  fatal_error("apply_impulse: no arm for locomotion model {}", (int)settings.model);
}

void clip_model_memory(const movement_settings_t& settings, move_state_t& state,
                       Span<const Plane> wall_planes)
{
  switch (settings.model)
  {
    // These three keep no horizontal memory beside the velocity the walls already clipped.
    case cvars::Locomotion_Model::quake:
    case cvars::Locomotion_Model::instant:
    case cvars::Locomotion_Model::instant_redirect:
      return;
    case cvars::Locomotion_Model::instant_momentum:
      instant_momentum_clip_memory(settings, state, wall_planes);
      return;
  }
  fatal_error("clip_model_memory: no arm for locomotion model {}", (int)settings.model);
}

wanted_move_t decide_move(const movement_settings_t& settings, const contacts_t& contacts,
                          bool grounded, const vec3& velocity_entering_move, move_state_t& state,
                          const move_input_t& input)
{
  switch (settings.model)
  {
    case cvars::Locomotion_Model::quake:
      return quake_step(settings, contacts, grounded, velocity_entering_move, state, input);
    case cvars::Locomotion_Model::instant:
      return instant_step(settings, contacts, grounded, velocity_entering_move, state, input);
    case cvars::Locomotion_Model::instant_momentum:
      return instant_momentum_step(settings, contacts, grounded, velocity_entering_move, state,
                                   input);
    case cvars::Locomotion_Model::instant_redirect:
      return instant_redirect_step(settings, contacts, grounded, velocity_entering_move, state,
                                   input);
  }
  fatal_error("decide_move: no arm for locomotion model {}", (int)settings.model);
}

} // namespace shared
