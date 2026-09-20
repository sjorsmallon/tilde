#pragma once
#include "cvars/generated/cvars_generated.hpp"
#include "player_constants.hpp"
#include <limits>
#include <optional>
#include <string_view>

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

// Friction plus accelerate, with pm_quake_bunnyhop already resolved into values.
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

// The same input rule over a real memory, so there is no timer here: what the
// drag bleeds is the momentum an impulse landed in.
struct instant_momentum_settings_t
{
  float ground_drag = 10.f;
  float air_drag    = 0.f;
};

// The velocity is the memory here, so the input's only power over carried speed
// is a turn rate: it cannot add to one and cannot cancel one.
struct instant_redirect_settings_t
{
  float turn_degrees_per_second = 360.f;
  float ground_drag             = 10.f;
  float air_drag                = 0.f;
};

struct movement_settings_t
{
  cvars::Locomotion_Model     model = cvars::Locomotion_Model::quake;
  shared_movement_settings_t  shared;
  quake_settings_t            quake;
  instant_settings_t          instant;
  instant_momentum_settings_t instant_momentum;
  instant_redirect_settings_t instant_redirect;
  bool                        record_collisions = false;
};

[[nodiscard]] movement_settings_t movement_settings_from(const cvars::cvar_state_t& cvars);

// The model a cvar's prefix names, or nothing when every model reads it. A map
// setting one whose model is not its own pm_model is setting a dead number.
[[nodiscard]] std::optional<cvars::Locomotion_Model>
locomotion_model_a_cvar_belongs_to(std::string_view name);

} // namespace shared
