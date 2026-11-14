// shared/weapons -- advance_projectile is the closed form of constant
// acceleration, so it must be step-invariant: n small steps land where one
// step of their sum does.

#include "shared/weapons.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace shared;

namespace
{

constexpr float GRAVITY = 800.f;

bool nearly_equal(vec3f a, vec3f b, float tolerance)
{
  return std::fabs(a.x - b.x) <= tolerance && std::fabs(a.y - b.y) <= tolerance &&
         std::fabs(a.z - b.z) <= tolerance;
}

void test_four_quarter_steps_are_one_whole_step()
{
  const projectile_t grenade{.speed = 600.f, .gravity_scale = 1.f};
  const vec3f start_position = {10.f, 20.f, 30.f};
  const vec3f start_velocity = {400.f, 300.f, -100.f};

  const projectile_step_t whole =
      advance_projectile(grenade, GRAVITY, start_position, start_velocity, 1.f);

  projectile_step_t quartered = {start_position, start_velocity};
  for (int i = 0; i < 4; ++i)
    quartered = advance_projectile(grenade, GRAVITY, quartered.position, quartered.velocity, 0.25f);

  assert(nearly_equal(whole.position, quartered.position, 1e-3f));
  assert(nearly_equal(whole.velocity, quartered.velocity, 1e-3f));

  const vec3f expected_position = {410.f, 20.f + 300.f - 400.f, -70.f};
  assert(nearly_equal(whole.position, expected_position, 1e-3f));
  std::printf("  four quarter steps are one whole step: ok\n");
}

void test_zero_gravity_scale_flies_straight()
{
  const projectile_t rocket{.speed = 600.f, .gravity_scale = 0.f};
  const vec3f start_velocity = {600.f, 0.f, 0.f};

  const projectile_step_t step =
      advance_projectile(rocket, GRAVITY, {0.f, 0.f, 0.f}, start_velocity, 2.f);

  assert(nearly_equal(step.position, {1200.f, 0.f, 0.f}, 1e-4f));
  assert(nearly_equal(step.velocity, start_velocity, 0.f));
  std::printf("  zero gravity scale flies straight: ok\n");
}

void test_the_rocket_launcher_row_does_not_drop()
{
  const weapon_definition_t& launcher = get_weapon_definition(entities::Weapon::Rocket_Launcher);
  assert(launcher.primary_fire.resolution == entities::Fire_Resolution::Projectile);
  assert(launcher.primary_fire.projectile.gravity_scale == 0.f);
  std::printf("  the rocket launcher row does not drop: ok\n");
}

void test_steering_keeps_the_speed_and_heads_for_the_point()
{
  const vec3f position = {0.f, 0.f, 0.f};
  const vec3f velocity = {300.f, 0.f, 0.f};

  const vec3f steered = steer_velocity_toward_point(position, velocity, {0.f, 0.f, 50.f}, 0.f);
  assert(nearly_equal(steered, {0.f, 0.f, 300.f}, 1e-3f));

  const vec3f on_the_point = steer_velocity_toward_point(position, velocity, {0.5f, 0.f, 0.f}, 0.f);
  assert(nearly_equal(on_the_point, velocity, 0.f));
  std::printf("  steering keeps the speed and heads for the point: ok\n");
}

void test_steering_turns_no_further_than_its_limit()
{
  const vec3f position = {0.f, 0.f, 0.f};
  const vec3f velocity = {300.f, 0.f, 0.f};
  const float diagonal = 300.f * std::sqrt(0.5f);

  const vec3f halfway = steer_velocity_toward_point(position, velocity, {0.f, 0.f, 50.f}, 45.f);
  assert(nearly_equal(halfway, {diagonal, 0.f, diagonal}, 1e-2f));

  const vec3f within_the_limit = steer_velocity_toward_point(position, velocity, {0.f, 0.f, 50.f}, 120.f);
  assert(nearly_equal(within_the_limit, {0.f, 0.f, 300.f}, 1e-2f));

  const vec3f straight_behind = steer_velocity_toward_point(position, velocity, {-50.f, 0.f, 0.f}, 90.f);
  assert(std::fabs(linalg::length(straight_behind) - 300.f) <= 1e-2f);
  assert(std::fabs(linalg::dot(straight_behind, velocity)) <= 1e-1f);
  std::printf("  steering turns no further than its limit: ok\n");
}

} // namespace

int main()
{
  std::printf("projectile_step_test\n");
  test_four_quarter_steps_are_one_whole_step();
  test_zero_gravity_scale_flies_straight();
  test_the_rocket_launcher_row_does_not_drop();
  test_steering_keeps_the_speed_and_heads_for_the_point();
  test_steering_turns_no_further_than_its_limit();
  std::printf("all projectile_step tests passed\n");
  return 0;
}
