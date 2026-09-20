#include "movement_settings.hpp"

namespace shared
{

movement_settings_t movement_settings_from(const cvars::cvar_state_t& cvars)
{
  movement_settings_t settings;

  settings.model = cvars.pm_acceleration;

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

  settings.quake = {.friction            = cvars.pm_friction,
                    .stop_speed          = cvars.pm_stopspeed,
                    .ground_acceleration = cvars.pm_ground_acceleration,
                    .air_acceleration    = cvars.pm_air_acceleration};

  switch (cvars.pm_bunnyhop)
  {
    case cvars::Bunnyhop_Mode::none:
      break;
    case cvars::Bunnyhop_Mode::hl2:
      settings.quake.jump_boost_speed     = cvars.pm_jump_boost;
      settings.quake.jump_boost_max_speed = cvars.pm_jump_boost_max_speed;
      break;
    case cvars::Bunnyhop_Mode::cs:
      settings.quake.clip_air_speed   = false;
      settings.quake.air_target_speed = cvars.pm_air_speed_cap;
      break;
  }

  settings.instant = {.speed_return_seconds = cvars.pm_speed_return_seconds};

  settings.hook = {.reel_speed    = cvars.pm_hook_reel_speed,
                   .arrive_radius = cvars.pm_hook_arrive_radius};

  settings.record_collisions = cvars.debug_show_collisions;

  return settings;
}

} // namespace shared
