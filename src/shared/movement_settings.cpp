#include "movement_settings.hpp"

namespace shared
{

namespace
{

quake_settings_t quake_settings_from(const cvars::cvar_state_t& cvars)
{
  quake_settings_t quake{.friction            = cvars.pm_quake_friction,
                         .stop_speed          = cvars.pm_quake_stop_speed,
                         .ground_acceleration = cvars.pm_quake_ground_acceleration,
                         .air_acceleration    = cvars.pm_quake_air_acceleration};

  switch (cvars.pm_quake_bunnyhop)
  {
    case cvars::Bunnyhop_Mode::none:
      break;
    case cvars::Bunnyhop_Mode::hl2:
      quake.jump_boost_speed     = cvars.pm_quake_jump_boost;
      quake.jump_boost_max_speed = cvars.pm_quake_jump_boost_max_speed;
      break;
    case cvars::Bunnyhop_Mode::cs:
      quake.clip_air_speed   = false;
      quake.air_target_speed = cvars.pm_quake_air_speed_cap;
      break;
  }

  return quake;
}

} // namespace

movement_settings_t movement_settings_from(const cvars::cvar_state_t& cvars)
{
  movement_settings_t settings;

  settings.model = cvars.pm_model;

  settings.shared = {.run_speed       = cvars.pm_maxspeed,
                     .jump_speed      = cvars.pm_jumpspeed,
                     .air_jump_count  = cvars.pm_air_jump_count,
                     .air_jump_speed  = cvars.pm_air_jump_speed,
                     .gravity         = cvars.g_gravity,
                     .step_height     = cvars.pm_step_height,
                     .overbounce      = cvars.pm_overbounce,
                     .speed_threshold = cvars.pm_speed_threshold,
                     .half_width      = player_half_width,
                     .half_height     = player_half_height};

  // A model's group is read only when pm_model names it, so a value the model
  // does not read cannot reach it and every other group stays at its defaults.
  switch (settings.model)
  {
    case cvars::Locomotion_Model::quake:
      settings.quake = quake_settings_from(cvars);
      break;
    case cvars::Locomotion_Model::instant:
      settings.instant = {.speed_return_seconds = cvars.pm_instant_speed_return_seconds};
      break;
  }

  settings.record_collisions = cvars.debug_show_collisions;

  return settings;
}

std::optional<cvars::Locomotion_Model> locomotion_model_a_cvar_belongs_to(std::string_view name)
{
  if (name.starts_with("pm_quake_"))
    return cvars::Locomotion_Model::quake;
  if (name.starts_with("pm_instant_"))
    return cvars::Locomotion_Model::instant;
  return std::nullopt;
}

} // namespace shared
