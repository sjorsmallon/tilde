#include "fly_camera.hpp"

#include "hud/announcement.hpp"
#include "../shared/math.hpp"

#include <algorithm>
#include <format>

namespace client
{

void step_fly_speed_from_keypad(float& units_per_second)
{
  constexpr float SPEED_STEP_FACTOR = 1.25f;
  constexpr float MIN_FLY_SPEED     = 50.0f;
  constexpr float MAX_FLY_SPEED     = 20000.0f;

  int steps = 0;
  if (input::is_key_pressed(input::key_t::Keypad_Plus))
    ++steps;
  if (input::is_key_pressed(input::key_t::Keypad_Minus))
    --steps;
  if (steps == 0)
    return;

  const float factor = steps > 0 ? SPEED_STEP_FACTOR : 1.0f / SPEED_STEP_FACTOR;
  units_per_second   = std::clamp(units_per_second * factor, MIN_FLY_SPEED, MAX_FLY_SPEED);
  hud::set_announcement(std::format("Camera speed {:.0f}", units_per_second));
}

fly_camera_input_t read_fly_camera_keys()
{
  fly_camera_input_t keys;
  keys.forward  = input::is_key_down(input::key_t::W);
  keys.backward = input::is_key_down(input::key_t::S);
  keys.left     = input::is_key_down(input::key_t::A);
  keys.right    = input::is_key_down(input::key_t::D);
  keys.up       = input::is_key_down(input::key_t::Space);
  keys.down     = input::is_key_down(input::key_t::C);
  keys.fast     = input::current_modifiers().shift;
  return keys;
}

void fly_camera(camera_t& camera, const fly_camera_input_t& input,
                const fly_camera_settings_t& settings, float dt)
{
  camera.yaw += input.look_delta.x * settings.degrees_per_pixel;
  camera.pitch -= input.look_delta.y * settings.degrees_per_pixel;
  shared::clamp_this(camera.pitch, -89.0f, 89.0f);

  float speed = settings.units_per_second * dt;
  if (input.fast)
    speed *= settings.fast_multiplier;

  const camera_basis_t basis = get_orientation_vectors(camera);

  if (input.forward)
    camera.position = camera.position + basis.forward * speed;
  if (input.backward)
    camera.position = camera.position - basis.forward * speed;
  if (input.right)
  {
    camera.position.x += basis.right.x * speed;
    camera.position.z += basis.right.z * speed;
  }
  if (input.left)
  {
    camera.position.x -= basis.right.x * speed;
    camera.position.z -= basis.right.z * speed;
  }
  if (input.up)
    camera.position.y += speed;
  if (input.down)
    camera.position.y -= speed;
}

} // namespace client
