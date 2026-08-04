// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/cvars/cvars.def by def_gen. Do not edit.
#include "cvars_generated.hpp"

#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <format>

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
    {.name = "r_zoom_time",
     .description = "Seconds to ease between normal and zoomed FOV (0 = instant)",
     .flags = CVAR_FLAG_CLIENT,
     .type = CVAR_TYPE_F32,
     .offset = offsetof(cvar_state_t, r_zoom_time),
     .size = sizeof(cvar_state_t::r_zoom_time),
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

const cvar_id MIRRORED_CVAR_TABLE[12] = {
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

bool find_cvar(std::string_view name, cvar_id* out_id)
{
  for (uint32_t index = 0; index < CVAR_COUNT; ++index)
  {
    if (name == CVAR_INFO_TABLE[index].name)
    {
      *out_id = (cvar_id)index;
      return true;
    }
  }
  return false;
}

bool find_command(std::string_view name, command_id* out_id)
{
  for (uint32_t index = 0; index < COMMAND_COUNT; ++index)
  {
    if (name == COMMAND_INFO_TABLE[index].name)
    {
      *out_id = (command_id)index;
      return true;
    }
  }
  return false;
}

Span<const cvar_id> mirrored_cvars()
{
  return {MIRRORED_CVAR_TABLE, 12};
}

bool cvar_to_text(const cvar_state_t& state, cvar_id id, std::string& out_text)
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
      out_text = std::format("{}", value);
      return true;
    }

    case CVAR_TYPE_I32:
    {
      int32_t value = 0;
      std::memcpy(&value, bytes, sizeof(value));
      out_text = std::format("{}", value);
      return true;
    }

    case CVAR_TYPE_U32:
    {
      uint32_t value = 0;
      std::memcpy(&value, bytes, sizeof(value));
      out_text = std::format("{}", value);
      return true;
    }

    case CVAR_TYPE_BOOL:
    {
      bool value = false;
      std::memcpy(&value, bytes, sizeof(value));
      out_text = value ? "1" : "0";
      return true;
    }

    case CVAR_TYPE_STRING:
    {
      // pascal_string_t<N> is `uint8 length; char data[N + 1]` with
      // alignment 1, so the bytes are addressed directly -- the table
      // hands out a void*, not a typed pointer.
      const uint8_t* raw = static_cast<const uint8_t*>(bytes);
      out_text.assign(reinterpret_cast<const char*>(raw + 1), raw[0]);
      return true;
    }
  }

  assert(false && "cvar_to_text: cvar carries an invalid type tag");
  return false;
}

namespace
{

// Requires the WHOLE token to parse. `pm_maxspeed 320abc` is a typo, and
// accepting 320 from it would set a value the author never wrote.
template <typename T> bool parse_whole(std::string_view text, T* out_value)
{
  const char* begin = text.data();
  const char* end   = text.data() + text.size();
  auto        result = std::from_chars(begin, end, *out_value);
  return result.ec == std::errc{} && result.ptr == end;
}

} // namespace

bool cvar_from_text(cvar_state_t& state, cvar_id id, std::string_view text)
{
  const cvar_info_t& info  = cvar_info(id);
  void*              bytes = value_bytes(state, info);

  switch (info.type)
  {
    case CVAR_TYPE_F32:
    {
      float value = 0.0f;
      if (!parse_whole(text, &value))
        return false;
      std::memcpy(bytes, &value, sizeof(value));
      return true;
    }

    case CVAR_TYPE_I32:
    {
      int32_t value = 0;
      if (!parse_whole(text, &value))
        return false;
      std::memcpy(bytes, &value, sizeof(value));
      return true;
    }

    case CVAR_TYPE_U32:
    {
      uint32_t value = 0;
      if (!parse_whole(text, &value))
        return false;
      std::memcpy(bytes, &value, sizeof(value));
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

  assert(false && "cvar_from_text: cvar carries an invalid type tag");
  return false;
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

bool from_string(std::string_view text, Bot_Mode* out_value)
{
  if (text == "idle") { *out_value = Bot_Mode::idle; return true; }
  if (text == "chase") { *out_value = Bot_Mode::chase; return true; }
  if (text == "regular") { *out_value = Bot_Mode::regular; return true; }
  return false;
}

} // namespace cvars
