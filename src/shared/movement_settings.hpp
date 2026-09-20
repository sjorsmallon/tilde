#pragma once
#include "cvars/generated/cvars_generated.hpp"
#include "player_constants.hpp"
#include <limits>

namespace shared
{

// What every model answers, whatever it is.
struct shared_movement_settings_t
{
  float   run_speed       = 320.f;
  float   jump_speed      = 270.f;
  int32_t air_jump_count  = 0;
  float   air_jump_speed  = 270.f;
  float   gravity         = 800.f;
  float   step_height     = 18.f;
  float   overbounce      = 1.001f;
  float   speed_threshold = 1.f;
  float   half_width      = player_half_width;
  float   half_height     = player_half_height;
};

// Friction plus accelerate, with pm_bunnyhop already resolved into values.
struct quake_settings_t
{
  float friction             = 6.f;
  float stop_speed           = 100.f;
  float ground_acceleration  = 10.f;
  float air_acceleration     = 5.f;
  bool  clip_air_speed       = true;
  float air_target_speed     = std::numeric_limits<float>::infinity();
  float jump_boost_speed     = 0.f;
  float jump_boost_max_speed = 0.f;
};

struct instant_settings_t
{
  float speed_return_seconds = 0.5f;
};

struct hook_settings_t
{
  float reel_speed    = 900.f;
  float arrive_radius = 48.f;
};

struct movement_settings_t
{
  cvars::Acceleration_Mode   model = cvars::Acceleration_Mode::quake;
  shared_movement_settings_t shared;
  quake_settings_t           quake;
  instant_settings_t         instant;
  hook_settings_t            hook;
  bool                       record_collisions = false;
};

[[nodiscard]] movement_settings_t movement_settings_from(const cvars::cvar_state_t& cvars);

} // namespace shared
