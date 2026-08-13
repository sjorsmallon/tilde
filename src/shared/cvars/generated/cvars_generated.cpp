// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/cvars/cvars.def by def_gen. Do not edit.
#include "cvars_generated.hpp"

#include "log.hpp"

#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <format>
#include <optional>

namespace cvars
{

namespace
{

const cvar_info_t CVAR_INFO_TABLE[CVAR_COUNT] = {
    {.name = "pm_maxspeed",
     .description = "Maximum player speed",
     .flags = CVAR_FLAG_MIRRORED,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, pm_maxspeed),
     .size = sizeof(cvar_state_t::pm_maxspeed),
     .string_capacity = 0},
    {.name = "pm_stopspeed",
     .description = "Deceleration threshold",
     .flags = CVAR_FLAG_MIRRORED,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, pm_stopspeed),
     .size = sizeof(cvar_state_t::pm_stopspeed),
     .string_capacity = 0},
    {.name = "pm_friction",
     .description = "Ground friction",
     .flags = CVAR_FLAG_MIRRORED,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, pm_friction),
     .size = sizeof(cvar_state_t::pm_friction),
     .string_capacity = 0},
    {.name = "pm_ground_acceleration",
     .description = "Ground acceleration",
     .flags = CVAR_FLAG_MIRRORED,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, pm_ground_acceleration),
     .size = sizeof(cvar_state_t::pm_ground_acceleration),
     .string_capacity = 0},
    {.name = "pm_air_acceleration",
     .description = "Air acceleration",
     .flags = CVAR_FLAG_MIRRORED,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, pm_air_acceleration),
     .size = sizeof(cvar_state_t::pm_air_acceleration),
     .string_capacity = 0},
    {.name = "pm_overbounce",
     .description = "Plane clip overbounce factor",
     .flags = CVAR_FLAG_MIRRORED,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, pm_overbounce),
     .size = sizeof(cvar_state_t::pm_overbounce),
     .string_capacity = 0},
    {.name = "pm_jumpspeed",
     .description = "Jump velocity",
     .flags = CVAR_FLAG_MIRRORED,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, pm_jumpspeed),
     .size = sizeof(cvar_state_t::pm_jumpspeed),
     .string_capacity = 0},
    {.name = "g_gravity",
     .description = "Gravity",
     .flags = CVAR_FLAG_MIRRORED,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, g_gravity),
     .size = sizeof(cvar_state_t::g_gravity),
     .string_capacity = 0},
    {.name = "pm_speed_threshold",
     .description = "Speed below which friction snaps to zero",
     .flags = CVAR_FLAG_MIRRORED,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, pm_speed_threshold),
     .size = sizeof(cvar_state_t::pm_speed_threshold),
     .string_capacity = 0},
    {.name = "pm_step_height",
     .description = "Maximum step height the player can glide up",
     .flags = CVAR_FLAG_MIRRORED,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, pm_step_height),
     .size = sizeof(cvar_state_t::pm_step_height),
     .string_capacity = 0},
    {.name = "pm_minimum_land_impact_speed",
     .description = "Threshold for speed to cause audio to play when landing",
     .flags = CVAR_FLAG_MIRRORED,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, pm_minimum_land_impact_speed),
     .size = sizeof(cvar_state_t::pm_minimum_land_impact_speed),
     .string_capacity = 0},
    {.name = "game_rocket_speed",
     .description = "Rocket velocity",
     .flags = CVAR_FLAG_MIRRORED,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, game_rocket_speed),
     .size = sizeof(cvar_state_t::game_rocket_speed),
     .string_capacity = 0},
    {.name = "sv_aim_max_pitch",
     .description = "Pitch extent of the authored aim pose set, in degrees",
     .flags = CVAR_FLAG_MIRRORED,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, sv_aim_max_pitch),
     .size = sizeof(cvar_state_t::sv_aim_max_pitch),
     .string_capacity = 0},
    {.name = "sv_aim_max_yaw",
     .description = "Yaw extent of the authored aim pose set, in degrees",
     .flags = CVAR_FLAG_MIRRORED,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, sv_aim_max_yaw),
     .size = sizeof(cvar_state_t::sv_aim_max_yaw),
     .string_capacity = 0},
    {.name = "sv_aim_body_turn_rate",
     .description = "How fast a player model's feet chase their view yaw, degrees/second",
     .flags = CVAR_FLAG_MIRRORED,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, sv_aim_body_turn_rate),
     .size = sizeof(cvar_state_t::sv_aim_body_turn_rate),
     .string_capacity = 0},
    {.name = "sv_tickrate",
     .description = "Server tick rate in Hz",
     .flags = CVAR_FLAG_SERVER,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, sv_tickrate),
     .size = sizeof(cvar_state_t::sv_tickrate),
     .string_capacity = 0},
    {.name = "r_fov",
     .description = "Field of view in degrees",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, r_fov),
     .size = sizeof(cvar_state_t::r_fov),
     .string_capacity = 0},
    {.name = "r_zoom_fov",
     .description = "Field of view in degrees while zoomed",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, r_zoom_fov),
     .size = sizeof(cvar_state_t::r_zoom_fov),
     .string_capacity = 0},
    {.name = "r_zoom_easing_time_between_fovs",
     .description = "Seconds to ease between normal and zoomed FOV (0 = instant)",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, r_zoom_easing_time_between_fovs),
     .size = sizeof(cvar_state_t::r_zoom_easing_time_between_fovs),
     .string_capacity = 0},
    {.name = "m_sensitivity",
     .description = "Mouse look sensitivity",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, m_sensitivity),
     .size = sizeof(cvar_state_t::m_sensitivity),
     .string_capacity = 0},
    {.name = "m_zoom_sensitivity_ratio",
     .description = "How much zoom scales mouse sensitivity (1 = constant feel, 0 = none)",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, m_zoom_sensitivity_ratio),
     .size = sizeof(cvar_state_t::m_zoom_sensitivity_ratio),
     .string_capacity = 0},
    {.name = "cl_maxfps",
     .description = "Maximum client framerate (0 = unlimited)",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, cl_maxfps),
     .size = sizeof(cvar_state_t::cl_maxfps),
     .string_capacity = 0},
    {.name = "cl_draw_player_hull",
     .description = "Draw remote players as their collision hull, over the model",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_BOOL,
     .offset = offsetof(cvar_state_t, cl_draw_player_hull),
     .size = sizeof(cvar_state_t::cl_draw_player_hull),
     .string_capacity = 0},
    {.name = "cl_spectate_slot",
     .description = "Spectate a player slot through its eyes (-1 = off; bots start at 32)",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_I32,
     .offset = offsetof(cvar_state_t, cl_spectate_slot),
     .size = sizeof(cvar_state_t::cl_spectate_slot),
     .string_capacity = 0},
    {.name = "cl_player_unlit",
     .description = "Draw player models unlit -- easier to read a pose than under the sun",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_BOOL,
     .offset = offsetof(cvar_state_t, cl_player_unlit),
     .size = sizeof(cvar_state_t::cl_player_unlit),
     .string_capacity = 0},
    {.name = "cl_aim_debug",
     .description = "Force every remote player's aim blend to cl_aim_debug_pitch/_yaw and show the scrub panel",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_BOOL,
     .offset = offsetof(cvar_state_t, cl_aim_debug),
     .size = sizeof(cvar_state_t::cl_aim_debug),
     .string_capacity = 0},
    {.name = "cl_aim_debug_pitch",
     .description = "Aim blend pitch forced while cl_aim_debug is on, degrees",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, cl_aim_debug_pitch),
     .size = sizeof(cvar_state_t::cl_aim_debug_pitch),
     .string_capacity = 0},
    {.name = "cl_aim_debug_yaw",
     .description = "Aim blend yaw DEVIATION forced while cl_aim_debug is on, degrees",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, cl_aim_debug_yaw),
     .size = sizeof(cvar_state_t::cl_aim_debug_yaw),
     .string_capacity = 0},
    {.name = "cl_crosshair",
     .description = "Draw the crosshair",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_BOOL,
     .offset = offsetof(cvar_state_t, cl_crosshair),
     .size = sizeof(cvar_state_t::cl_crosshair),
     .string_capacity = 0},
    {.name = "cl_crosshair_dot",
     .description = "Draw the crosshair's center dot",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_BOOL,
     .offset = offsetof(cvar_state_t, cl_crosshair_dot),
     .size = sizeof(cvar_state_t::cl_crosshair_dot),
     .string_capacity = 0},
    {.name = "cl_crosshair_size",
     .description = "Length of each crosshair line in pixels",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, cl_crosshair_size),
     .size = sizeof(cvar_state_t::cl_crosshair_size),
     .string_capacity = 0},
    {.name = "cl_crosshair_gap",
     .description = "Pixels between the center and where each line starts",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, cl_crosshair_gap),
     .size = sizeof(cvar_state_t::cl_crosshair_gap),
     .string_capacity = 0},
    {.name = "cl_crosshair_thickness",
     .description = "Crosshair line thickness in pixels",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, cl_crosshair_thickness),
     .size = sizeof(cvar_state_t::cl_crosshair_thickness),
     .string_capacity = 0},
    {.name = "cl_crosshair_r",
     .description = "Crosshair red (0-255)",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_U32,
     .offset = offsetof(cvar_state_t, cl_crosshair_r),
     .size = sizeof(cvar_state_t::cl_crosshair_r),
     .string_capacity = 0},
    {.name = "cl_crosshair_g",
     .description = "Crosshair green (0-255)",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_U32,
     .offset = offsetof(cvar_state_t, cl_crosshair_g),
     .size = sizeof(cvar_state_t::cl_crosshair_g),
     .string_capacity = 0},
    {.name = "cl_crosshair_b",
     .description = "Crosshair blue (0-255)",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_U32,
     .offset = offsetof(cvar_state_t, cl_crosshair_b),
     .size = sizeof(cvar_state_t::cl_crosshair_b),
     .string_capacity = 0},
    {.name = "cl_crosshair_a",
     .description = "Crosshair alpha (0-255)",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_U32,
     .offset = offsetof(cvar_state_t, cl_crosshair_a),
     .size = sizeof(cvar_state_t::cl_crosshair_a),
     .string_capacity = 0},
    {.name = "editor_speed",
     .description = "default movement speed of the camera in the editor",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, editor_speed),
     .size = sizeof(cvar_state_t::editor_speed),
     .string_capacity = 0},
    {.name = "cl_timescale",
     .description = "Time scale factor (0.5 = slow-mo, 2.0 = fast)",
     .flags = CVAR_FLAG_NONE,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, cl_timescale),
     .size = sizeof(cvar_state_t::cl_timescale),
     .string_capacity = 0},
    {.name = "sound_reference_distance",
     .description = "Distance (world units) within which a 3D sound is at full volume",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, sound_reference_distance),
     .size = sizeof(cvar_state_t::sound_reference_distance),
     .string_capacity = 0},
    {.name = "sound_max_distance_cutoff",
     .description = "Distance (world units) past which 3D attenuation stops increasing",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, sound_max_distance_cutoff),
     .size = sizeof(cvar_state_t::sound_max_distance_cutoff),
     .string_capacity = 0},
    {.name = "sound_rolloff_factor",
     .description = "3D distance attenuation rolloff factor",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, sound_rolloff_factor),
     .size = sizeof(cvar_state_t::sound_rolloff_factor),
     .string_capacity = 0},
    {.name = "debug_show_collisions",
     .description = "Show collision faces in green",
     .flags = CVAR_FLAG_NONE,
     .type = CVAR_TYPE_BOOL,
     .offset = offsetof(cvar_state_t, debug_show_collisions),
     .size = sizeof(cvar_state_t::debug_show_collisions),
     .string_capacity = 0},
    {.name = "debug_show_hitboxes",
     .description = "Show entity hitboxes in wireframe",
     .flags = CVAR_FLAG_NONE,
     .type = CVAR_TYPE_BOOL,
     .offset = offsetof(cvar_state_t, debug_show_hitboxes),
     .size = sizeof(cvar_state_t::debug_show_hitboxes),
     .string_capacity = 0},
    {.name = "debug_show_navmesh",
     .description = "Show baked navmesh as a line grid",
     .flags = CVAR_FLAG_NONE,
     .type = CVAR_TYPE_BOOL,
     .offset = offsetof(cvar_state_t, debug_show_navmesh),
     .size = sizeof(cvar_state_t::debug_show_navmesh),
     .string_capacity = 0},
    {.name = "debug_show_box_volumes",
     .description = "Draw every entity's box volume as a wireframe AABB (triggers, clip volumes, ...)",
     .flags = CVAR_FLAG_NONE,
     .type = CVAR_TYPE_BOOL,
     .offset = offsetof(cvar_state_t, debug_show_box_volumes),
     .size = sizeof(cvar_state_t::debug_show_box_volumes),
     .string_capacity = 0},
    {.name = "debug_hide_geometry",
     .description = "Skip drawing map geometry",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_BOOL,
     .offset = offsetof(cvar_state_t, debug_hide_geometry),
     .size = sizeof(cvar_state_t::debug_hide_geometry),
     .string_capacity = 0},
    {.name = "debug_show_entity_counts",
     .description = "Per-type entity count overlay",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_BOOL,
     .offset = offsetof(cvar_state_t, debug_show_entity_counts),
     .size = sizeof(cvar_state_t::debug_show_entity_counts),
     .string_capacity = 0},
    {.name = "debug_show_physics_bodies",
     .description = "Draw Jolt bodies and constraints (JPH_DEBUG_RENDERER builds)",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_BOOL,
     .offset = offsetof(cvar_state_t, debug_show_physics_bodies),
     .size = sizeof(cvar_state_t::debug_show_physics_bodies),
     .string_capacity = 0},
    {.name = "net_snapshot_debug",
     .description = "Log each received snapshot's baseline tick and payload size (every 120 ticks)",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_BOOL,
     .offset = offsetof(cvar_state_t, net_snapshot_debug),
     .size = sizeof(cvar_state_t::net_snapshot_debug),
     .string_capacity = 0},
};

const command_info_t COMMAND_INFO_TABLE[COMMAND_COUNT] = {
    {.name = "spawn_bot",
     .description = "Spawn a bot",
     .usage = "spawn_bot [mode: idle|chase|regular]",
     .flags = CVAR_FLAG_SERVER},
    {.name = "spawn_cube",
     .description = "Spawn a physics cube in front of the calling player",
     .usage = "spawn_cube",
     .flags = CVAR_FLAG_SERVER},
    {.name = "spawn_sphere",
     .description = "Spawn a physics sphere in front of the calling player",
     .usage = "spawn_sphere",
     .flags = CVAR_FLAG_SERVER},
    {.name = "map",
     .description = "Switch the server to a new map",
     .usage = "map <path>",
     .flags = CVAR_FLAG_SERVER},
    {.name = "noclip",
     .description = "Disable movement Vector Clipping",
     .usage = "noclip [enabled]",
     .flags = CVAR_FLAG_SERVER},
    {.name = "bind",
     .description = "Bind a key (a-z) to a command line",
     .usage = "bind <key> <command...>",
     .flags = CVAR_FLAG_CLIENT},
    {.name = "connect",
     .description = "Connect to a server (ip or ip:port) and enter play",
     .usage = "connect <address>",
     .flags = CVAR_FLAG_CLIENT},
};

const cvar_id MIRRORED_CVAR_TABLE[15] = {
    cvar_id::pm_maxspeed,
    cvar_id::pm_stopspeed,
    cvar_id::pm_friction,
    cvar_id::pm_ground_acceleration,
    cvar_id::pm_air_acceleration,
    cvar_id::pm_overbounce,
    cvar_id::pm_jumpspeed,
    cvar_id::g_gravity,
    cvar_id::pm_speed_threshold,
    cvar_id::pm_step_height,
    cvar_id::pm_minimum_land_impact_speed,
    cvar_id::game_rocket_speed,
    cvar_id::sv_aim_max_pitch,
    cvar_id::sv_aim_max_yaw,
    cvar_id::sv_aim_body_turn_rate,
};

// The value's bytes inside the state struct. Every text conversion goes
// through here rather than naming members, so the pair below is one
// switch over five types instead of one case per cvar.
void* value_bytes(cvar_state_t& state, const cvar_info_t& info)
{
  return reinterpret_cast<uint8_t*>(&state) + info.offset;
}

const void* value_bytes(const cvar_state_t& state, const cvar_info_t& info)
{
  return reinterpret_cast<const uint8_t*>(&state) + info.offset;
}

} // namespace

Span<const cvar_info_t> cvar_infos()
{
  return {CVAR_INFO_TABLE, CVAR_COUNT};
}

Span<const command_info_t> command_infos()
{
  return {COMMAND_INFO_TABLE, COMMAND_COUNT};
}

const cvar_info_t& cvar_info(cvar_id id)
{
  assert((uint32_t)id < CVAR_COUNT);
  return CVAR_INFO_TABLE[(uint32_t)id];
}

const command_info_t& command_info(command_id id)
{
  assert((uint32_t)id < COMMAND_COUNT);
  return COMMAND_INFO_TABLE[(uint32_t)id];
}

std::optional<cvar_id> try_find_cvar(std::string_view name)
{
  for (uint32_t index = 0; index < CVAR_COUNT; ++index)
  {
    if (name == CVAR_INFO_TABLE[index].name)
      return (cvar_id)index;
  }
  return std::nullopt;
}

std::optional<command_id> try_find_command(std::string_view name)
{
  for (uint32_t index = 0; index < COMMAND_COUNT; ++index)
  {
    if (name == COMMAND_INFO_TABLE[index].name)
      return (command_id)index;
  }
  return std::nullopt;
}

Span<const cvar_id> mirrored_cvars()
{
  return {MIRRORED_CVAR_TABLE, 15};
}

std::optional<std::string> try_cvar_to_text(const cvar_state_t& state, cvar_id id)
{
  const cvar_info_t& info  = cvar_info(id);
  const void*        bytes = value_bytes(state, info);

  switch (info.type)
  {
    case CVAR_TYPE_F32:
    {
      float value = 0.0f;
      std::memcpy(&value, bytes, sizeof(value));
      // Shortest representation that round-trips, so a config save/load
      // cycle is exact and a config diff shows only real changes.
      return std::format("{}", value);
    }

    case CVAR_TYPE_I32:
    {
      int32_t value = 0;
      std::memcpy(&value, bytes, sizeof(value));
      return std::format("{}", value);
    }

    case CVAR_TYPE_U32:
    {
      uint32_t value = 0;
      std::memcpy(&value, bytes, sizeof(value));
      return std::format("{}", value);
    }

    case CVAR_TYPE_BOOL:
    {
      bool value = false;
      std::memcpy(&value, bytes, sizeof(value));
      return std::string(value ? "1" : "0");
    }

    case CVAR_TYPE_STRING:
    {
      // pascal_string_t<N> is `uint8 length; char data[N + 1]` with
      // alignment 1, so the bytes are addressed directly -- the table
      // hands out a void*, not a typed pointer.
      const uint8_t* raw = static_cast<const uint8_t*>(bytes);
      return std::string(reinterpret_cast<const char*>(raw + 1), raw[0]);
    }
  }

  fatal_error("try_cvar_to_text: cvar '{}' carries an invalid type tag {}",
              info.name, (int)info.type);
}

namespace
{

// Requires the WHOLE token to parse. `pm_maxspeed 320abc` is a typo, and
// accepting 320 from it would set a value the author never wrote.
template <typename T> std::optional<T> try_parse_whole(std::string_view text)
{
  T           value  = {};
  const char* begin  = text.data();
  const char* end    = text.data() + text.size();
  auto        result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end)
    return std::nullopt;
  return value;
}

} // namespace

bool try_cvar_from_text(cvar_state_t& state, cvar_id id, std::string_view text)
{
  const cvar_info_t& info  = cvar_info(id);
  void*              bytes = value_bytes(state, info);

  switch (info.type)
  {
    case CVAR_TYPE_F32:
    {
      const std::optional<float> value = try_parse_whole<float>(text);
      if (!value)
        return false;
      std::memcpy(bytes, &*value, sizeof(*value));
      return true;
    }

    case CVAR_TYPE_I32:
    {
      const std::optional<int32_t> value = try_parse_whole<int32_t>(text);
      if (!value)
        return false;
      std::memcpy(bytes, &*value, sizeof(*value));
      return true;
    }

    case CVAR_TYPE_U32:
    {
      const std::optional<uint32_t> value = try_parse_whole<uint32_t>(text);
      if (!value)
        return false;
      std::memcpy(bytes, &*value, sizeof(*value));
      return true;
    }

    case CVAR_TYPE_BOOL:
    {
      // A closed set both ways. The old CVar<bool> mapped anything that
      // was not "1/true/yes/on" to FALSE, so `debug_show_navmesh tru`
      // silently turned it off. Unrecognised text is now a rejection and
      // the value is left alone.
      bool value = false;
      if (text == "1" || text == "true" || text == "yes" || text == "on")
        value = true;
      else if (text == "0" || text == "false" || text == "no" || text == "off")
        value = false;
      else
        return false;
      std::memcpy(bytes, &value, sizeof(value));
      return true;
    }

    case CVAR_TYPE_STRING:
    {
      if (text.size() > info.string_capacity)
        return false;
      uint8_t* raw = static_cast<uint8_t*>(bytes);
      raw[0]       = (uint8_t)text.size();
      std::memcpy(raw + 1, text.data(), text.size());
      // Restore the canonical zero-padding invariant (see
      // pascal_string_t): every byte past the last character must be
      // zero, or two equal strings stop comparing equal under memcmp and
      // the mirror replicates a phantom change every tick.
      std::memset(raw + 1 + text.size(), 0, info.size - 1 - text.size());
      return true;
    }
  }

  fatal_error("try_cvar_from_text: cvar '{}' carries an invalid type tag {}",
              info.name, (int)info.type);
}

const char* to_string(Bot_Mode value)
{
  switch (value)
  {
    case Bot_Mode::idle: return "idle";
    case Bot_Mode::chase: return "chase";
    case Bot_Mode::regular: return "regular";
  }
  assert(false && "invalid Bot_Mode");
  return "";
}

template <> std::optional<Bot_Mode> try_from_string<Bot_Mode>(std::string_view text)
{
  if (text == "idle") return Bot_Mode::idle;
  if (text == "chase") return Bot_Mode::chase;
  if (text == "regular") return Bot_Mode::regular;
  return std::nullopt;
}

} // namespace cvars
