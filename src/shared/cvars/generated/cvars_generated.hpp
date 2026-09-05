// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/cvars/cvars.def by def_gen. Do not edit.
#pragma once

#include "array.hpp"
#include "cvars/cvar_runtime.hpp"
#include "reflection.hpp"
#include "network/network_types.hpp"
#include "span.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace cvars
{

template <typename T> std::optional<T> try_from_string(std::string_view text);

// Ownership, and nothing else. No flag at all is the common case and
// means shared-local: both sides hold the value, each process owns its
// own, nothing is synced.
enum cvar_flags : uint32_t
{
  CVAR_FLAG_NONE     = 0,
  CVAR_FLAG_CLIENT   = 1 << 0, // client-owned; meaningless on a dedicated server
  CVAR_FLAG_SERVER   = 1 << 1, // server-owned; clients forward the console line
  CVAR_FLAG_MIRRORED = 1 << 2, // server-owned, pushed to clients as a read-only mirror
};

// Every enum below is DENSE and starts at 0, so its _COUNT is both the
// number of declared names and one past the largest value -- which is
// what makes it safe as an array size.

enum class Game_Mode : uint8_t
{
  deathmatch = 0,
  rounds = 1,
  speedrun = 2,
};

constexpr uint32_t Game_Mode_COUNT = 3;

const char* to_string(Game_Mode value);
template <> std::optional<Game_Mode> try_from_string<Game_Mode>(std::string_view text);

enum class Debug_Channel : uint8_t
{
  off = 0,
  normals = 1,
  uv = 2,
  parallax_uv = 3,
  shadow_visibility = 4,
  shadow_cascades = 5,
  direct_light = 6,
  baked_light = 7,
  probe_visibility = 8,
  shadow_penumbra = 9,
};

constexpr uint32_t Debug_Channel_COUNT = 10;

const char* to_string(Debug_Channel value);
template <> std::optional<Debug_Channel> try_from_string<Debug_Channel>(std::string_view text);

enum class Bot_Mode : uint8_t
{
  idle = 0,
  chase = 1,
  regular = 2,
};

constexpr uint32_t Bot_Mode_COUNT = 3;

const char* to_string(Bot_Mode value);
template <> std::optional<Bot_Mode> try_from_string<Bot_Mode>(std::string_view text);

// THE values. One per process, created by the launcher and handed to
// each module's init -- so the integrated build's client and server
// share the one instance the old singleton only pretended to be.
//
// Reading a cvar is a field access (`cvars.pm_maxspeed`), not a string
// lookup and not a virtual call. Names exist at runtime only in the
// console.
//
// Declaration order is the .def's order, which is also the config-file
// save order -- so a saved config is diffable.
struct cvar_state_t
{
  float pm_maxspeed = 320.0f;
  float pm_stopspeed = 100.0f;
  float pm_friction = 6.0f;
  float pm_ground_acceleration = 10.0f;
  float pm_air_acceleration = 5.0f;
  float pm_overbounce = 1.001f;
  float pm_jumpspeed = 270.0f;
  float g_gravity = 800.0f;
  float pm_speed_threshold = 1.0f;
  float pm_step_height = 18.0f;
  float pm_minimum_land_impact_speed = 150.0f;
  int32_t pm_air_jump_count = 0;
  float pm_air_jump_speed = 270.0f;
  float game_rocket_speed = 600.0f;
  Game_Mode sv_gamemode = Game_Mode::deathmatch;
  float mp_warmup_seconds = 0.0f;
  float mp_countdown_seconds = 3.0f;
  float mp_round_seconds = 180.0f;
  float mp_round_end_seconds = 5.0f;
  float mp_game_over_seconds = 10.0f;
  int32_t mp_players_to_start = 1;
  int32_t mp_frag_limit = 20;
  float sv_aim_max_pitch = 45.0f;
  float sv_aim_max_yaw = 45.0f;
  float sv_aim_body_turn_rate = 540.0f;
  bool sv_lag_compensation = true;
  int32_t sv_max_rewind_ticks = 12;
  bool sv_lag_compensation_debug = false;
  bool sv_shot_debug = false;
  float sv_tickrate = 60.0f;
  float sv_timeout = 30.0f;
  int32_t sv_max_move_backlog = 8;
  int32_t sv_map_transfer_fragments_per_tick = 8;
  network::pascal_string_t<32> name = "Player";
  int32_t cl_max_unacked_inputs = 8;
  float r_fov = 90.0f;
  float r_zoom_fov = 30.0f;
  float r_zoom_easing_time_between_fovs = 0.0f;
  int32_t r_shadow_map_size = 1024;
  int32_t r_shadow_layer_count = 12;
  float r_shadow_light_offset = 1.0f;
  float r_shadow_bias_slope = 2.5f;
  float r_shadow_normal_offset = 1.5f;
  int32_t r_shadow_pcf_radius = 1;
  bool r_shadow_pcss = true;
  float r_shadow_pcss_max_radius = 16.0f;
  int32_t r_shadow_debug_light = 0;
  int32_t r_shadow_cascade_count = 3;
  float r_shadow_cascade_lambda = 0.7f;
  float r_shadow_cascade_distance = 4096.0f;
  float r_shadow_cascade_blend = 0.1f;
  float r_shadow_cascade_caster_extent = 8192.0f;
  bool r_shadow_freeze = false;
  float m_sensitivity = 0.1f;
  float m_zoom_sensitivity_ratio = 1.0f;
  float cl_maxfps = 1000.0f;
  float cl_interpolation_delay_ticks = 2.0f;
  bool cl_interpolation_debug = false;
  float cl_display_latency_ms = 0.0f;
  bool cl_draw_player_hull = false;
  int32_t cl_spectate_slot = -1;
  bool cl_player_unlit = false;
  bool cl_aim_debug = false;
  float cl_aim_debug_pitch = 0.0f;
  float cl_aim_debug_yaw = 0.0f;
  bool cl_show_deploy_timer = false;
  bool cl_crosshair = true;
  bool cl_crosshair_dot = true;
  float cl_crosshair_size = 7.0f;
  float cl_crosshair_gap = 5.0f;
  float cl_crosshair_thickness = 2.0f;
  uint32_t cl_crosshair_r = 0;
  uint32_t cl_crosshair_g = 255;
  uint32_t cl_crosshair_b = 0;
  uint32_t cl_crosshair_a = 255;
  float editor_speed = 1600.0f;
  float cl_timescale = 1.0f;
  float sound_reference_distance = 150.0f;
  float sound_max_distance_cutoff = 4000.0f;
  float sound_rolloff_factor = 1.0f;
  float map_respawn_delay_seconds = 3.0f;
  int32_t map_kill_limit = 25;
  float map_round_time_limit_seconds = 6000.0f;
  bool pin_main_thread = true;
  Debug_Channel r_debug_channel = Debug_Channel::off;
  float r_exposure = 1.4f;
  bool debug_show_collisions = false;
  bool debug_show_hitboxes = true;
  bool debug_show_navmesh = false;
  bool debug_show_box_volumes = false;
  bool debug_hide_geometry = false;
  float cl_shot_debug_seconds = 4.0f;
  bool debug_show_entity_counts = false;
  bool debug_show_physics_bodies = false;
  bool net_snapshot_debug = false;
  bool sv_event_debug = false;
  bool cl_event_debug = false;
  bool sv_reliable_debug = false;
};

// Load-bearing for mirroring: change detection is a member compare
// against a retained copy of this struct, so a DIRECT field write in
// game code replicates correctly. There is no "must call Set()" trap
// because there is no Set().
static_assert(std::is_trivially_copyable_v<cvar_state_t>,
              "cvar_state_t must stay trivially copyable: mirroring compares it "
              "against a retained copy");

enum class cvar_id : uint16_t
{
  pm_maxspeed = 0,
  pm_stopspeed = 1,
  pm_friction = 2,
  pm_ground_acceleration = 3,
  pm_air_acceleration = 4,
  pm_overbounce = 5,
  pm_jumpspeed = 6,
  g_gravity = 7,
  pm_speed_threshold = 8,
  pm_step_height = 9,
  pm_minimum_land_impact_speed = 10,
  pm_air_jump_count = 11,
  pm_air_jump_speed = 12,
  game_rocket_speed = 13,
  sv_gamemode = 14,
  mp_warmup_seconds = 15,
  mp_countdown_seconds = 16,
  mp_round_seconds = 17,
  mp_round_end_seconds = 18,
  mp_game_over_seconds = 19,
  mp_players_to_start = 20,
  mp_frag_limit = 21,
  sv_aim_max_pitch = 22,
  sv_aim_max_yaw = 23,
  sv_aim_body_turn_rate = 24,
  sv_lag_compensation = 25,
  sv_max_rewind_ticks = 26,
  sv_lag_compensation_debug = 27,
  sv_shot_debug = 28,
  sv_tickrate = 29,
  sv_timeout = 30,
  sv_max_move_backlog = 31,
  sv_map_transfer_fragments_per_tick = 32,
  name = 33,
  cl_max_unacked_inputs = 34,
  r_fov = 35,
  r_zoom_fov = 36,
  r_zoom_easing_time_between_fovs = 37,
  r_shadow_map_size = 38,
  r_shadow_layer_count = 39,
  r_shadow_light_offset = 40,
  r_shadow_bias_slope = 41,
  r_shadow_normal_offset = 42,
  r_shadow_pcf_radius = 43,
  r_shadow_pcss = 44,
  r_shadow_pcss_max_radius = 45,
  r_shadow_debug_light = 46,
  r_shadow_cascade_count = 47,
  r_shadow_cascade_lambda = 48,
  r_shadow_cascade_distance = 49,
  r_shadow_cascade_blend = 50,
  r_shadow_cascade_caster_extent = 51,
  r_shadow_freeze = 52,
  m_sensitivity = 53,
  m_zoom_sensitivity_ratio = 54,
  cl_maxfps = 55,
  cl_interpolation_delay_ticks = 56,
  cl_interpolation_debug = 57,
  cl_display_latency_ms = 58,
  cl_draw_player_hull = 59,
  cl_spectate_slot = 60,
  cl_player_unlit = 61,
  cl_aim_debug = 62,
  cl_aim_debug_pitch = 63,
  cl_aim_debug_yaw = 64,
  cl_show_deploy_timer = 65,
  cl_crosshair = 66,
  cl_crosshair_dot = 67,
  cl_crosshair_size = 68,
  cl_crosshair_gap = 69,
  cl_crosshair_thickness = 70,
  cl_crosshair_r = 71,
  cl_crosshair_g = 72,
  cl_crosshair_b = 73,
  cl_crosshair_a = 74,
  editor_speed = 75,
  cl_timescale = 76,
  sound_reference_distance = 77,
  sound_max_distance_cutoff = 78,
  sound_rolloff_factor = 79,
  map_respawn_delay_seconds = 80,
  map_kill_limit = 81,
  map_round_time_limit_seconds = 82,
  pin_main_thread = 83,
  r_debug_channel = 84,
  r_exposure = 85,
  debug_show_collisions = 86,
  debug_show_hitboxes = 87,
  debug_show_navmesh = 88,
  debug_show_box_volumes = 89,
  debug_hide_geometry = 90,
  cl_shot_debug_seconds = 91,
  debug_show_entity_counts = 92,
  debug_show_physics_bodies = 93,
  net_snapshot_debug = 94,
  sv_event_debug = 95,
  cl_event_debug = 96,
  sv_reliable_debug = 97,
};

// Not a member of the enum above, so `switch` over a cvar_id still
// warns on an unhandled case.
constexpr uint32_t CVAR_COUNT = 98;

enum class command_id : uint16_t
{
  spawn_bot = 0,
  spawn_cube = 1,
  spawn_sphere = 2,
  map = 3,
  noclip = 4,
  join_game = 5,
  spectate = 6,
  sv_mem_report = 7,
  sv_frame_report = 8,
  sv_hitch_report = 9,
  bind = 10,
  connect = 11,
  announce = 12,
  mem_report = 13,
  mem_frame = 14,
  mem_stacks = 15,
  frame_report = 16,
  frame_reset = 17,
  hitch_report = 18,
};

constexpr uint32_t COMMAND_COUNT = 19;

enum cvar_type : uint8_t
{
  CVAR_TYPE_F32 = 0,
  CVAR_TYPE_I32,
  CVAR_TYPE_U32,
  CVAR_TYPE_BOOL,
  CVAR_TYPE_STRING,
  CVAR_TYPE_ENUM,
};

// The console's whole view of a cvar. `offset` and `size` locate the
// value inside cvar_state_t, which is what lets one text-conversion
// pair serve every cvar without a switch per call site.
struct cvar_info_t
{
  const char* name;
  const char* description;
  uint32_t    flags;
  cvar_type   type;
  uint16_t    offset;
  uint16_t    size;
  uint16_t    string_capacity; // string<N>'s N, otherwise 0

  // CVAR_TYPE_ENUM only, else NOT_AN_ENUM. The same record an
  // enum-typed entity or event field carries, which is what keeps the
  // text conversion below a fixed set of cases with no per-cvar code.
  const enum_type_info_t* enum_info;
};

struct command_info_t
{
  const char* name;
  const char* description;
  // Derived from the declared signature, so it cannot drift from what the
  // argument binder actually accepts. Just the name for a no-argument command.
  const char* usage;
  uint32_t    flags;
};

// Indexed by cvar_id / command_id, in declaration order.
Span<const cvar_info_t>    cvar_infos();
Span<const command_info_t> command_infos();

const cvar_info_t&    cvar_info(cvar_id id);
const command_info_t& command_info(command_id id);

// Name lookup. The console is the only place names exist at runtime, so
// this is a linear scan and stays one -- it runs at typing speed.
// Cvars and commands share one flat namespace, so a name resolves to at
// most one of these two.
[[nodiscard]] std::optional<cvar_id>    try_find_cvar(std::string_view name);
[[nodiscard]] std::optional<command_id> try_find_command(std::string_view name);

// The @Mirrored subset, so both ends agree on the sync set by
// construction rather than by each filtering on flags and hoping.
Span<const cvar_id> mirrored_cvars();

// The ONLY place cvar bytes become characters: console echo, config
// files, and the mirrored-value payload all go through this pair.
// Floats use the shortest representation that round-trips.
//
// try_cvar_from_text returns false and leaves the value ALONE when the text
// does not parse -- the caller reports it, because only the caller knows
// whether it came from a console line, a config file or the wire. It keeps
// a bool rather than an optional because it has no value to hand back, but
// it still carries the try_ prefix: the prefix tracks FALLIBILITY, and a
// bare name has to keep meaning "this cannot quietly fail".
[[nodiscard]] std::optional<std::string> try_cvar_to_text(const cvar_state_t& state, cvar_id id);
[[nodiscard]] bool try_cvar_from_text(cvar_state_t& state, cvar_id id, std::string_view text);

// Handler declarations, TYPED from each command's declared signature.
// Declaring a command in the .def OBLIGATES the owning side to define the
// matching function with exactly this signature: the generated binder TU
// references the symbol directly, so a missing, misspelled or wrongly
// typed handler is a link error naming it. The handler never sees the
// token list -- its generated argument binder has already parsed,
// validated and defaulted every parameter, or replied with the usage
// string instead of calling. Bodies are handwritten -- only the parsing
// and the binding are derived.
namespace commands
{
// @Server  Spawn a bot
// usage: spawn_bot [mode: idle|chase|regular]
void spawn_bot(Bot_Mode mode, const command_context_t& context);
// @Server  Spawn a physics cube in front of the calling player
// usage: spawn_cube
void spawn_cube(const command_context_t& context);
// @Server  Spawn a physics sphere in front of the calling player
// usage: spawn_sphere
void spawn_sphere(const command_context_t& context);
// @Server  Switch the server to a new map
// usage: map <path>
void map(std::string_view path, const command_context_t& context);
// @Server  Disable movement Vector Clipping
// usage: noclip [enabled]
void noclip(bool enabled, const command_context_t& context);
// @Server  Leave spectate and spawn into the match
// usage: join_game
void join_game(const command_context_t& context);
// @Server  Leave the match and return to spectating
// usage: spectate
void spectate(const command_context_t& context);
// @Server  Print the top allocation sites by live bytes
// usage: sv_mem_report [top]
void sv_mem_report(int32_t top, const command_context_t& context);
// @Server  Print the tick time distribution and the worst ticks so far
// usage: sv_frame_report
void sv_frame_report(const command_context_t& context);
// @Server  What the worst tick allocated, by call site
// usage: sv_hitch_report [top]
void sv_hitch_report(int32_t top, const command_context_t& context);
// @Client  Bind a key (a-z) to a command line
// usage: bind <key> <command...>
void bind(std::string_view key, std::string_view command, const command_context_t& context);
// @Client  Connect to a server (ip or ip:port) and enter play
// usage: connect <address>
void connect(std::string_view address, const command_context_t& context);
// @Client  Show a banner on screen for a few seconds
// usage: announce <text...>
void announce(std::string_view text, const command_context_t& context);
// @Client  Print the top allocation sites by live bytes
// usage: mem_report [top]
void mem_report(int32_t top, const command_context_t& context);
// @Client  Print how much the last frame allocated
// usage: mem_frame
void mem_frame(const command_context_t& context);
// @Client  Capture a call stack per allocation (off = totals only)
// usage: mem_stacks [capture]
void mem_stacks(bool capture, const command_context_t& context);
// @Client  Print the frame time distribution and the worst frames so far
// usage: frame_report
void frame_report(const command_context_t& context);
// @Client  Discard the frame time distribution and start measuring again
// usage: frame_reset
void frame_reset(const command_context_t& context);
// @Client  What the worst frame allocated, by call site
// usage: hitch_report [top]
void hitch_report(int32_t top, const command_context_t& context);
} // namespace commands

// The runtime dispatch surface. Each slot holds the command's generated
// ARGUMENT BINDER, which parses the tokens against the declared signature
// and calls the typed handler above. A slot is null when its side is not
// loaded (client commands on a dedicated server), which the execute
// path treats as an error rather than a silent no-op.
struct command_table_t
{
  command_binder_t binders[COMMAND_COUNT] = {};

  // Set by a networked client. @Server cvars and commands typed into a
  // client console are forwarded whole rather than executed locally.
  forward_line_fn_t forward_to_server = nullptr;
};

// Called once per loaded module, from inside that module -- the binder
// TU is compiled into the DLL that owns the handlers, so the launcher
// reaches it through the module's existing init entry point rather than
// by exporting these symbols.
void bind_server_commands(command_table_t& table);
void bind_client_commands(command_table_t& table);

// No SCHEMA_HASH here on purpose: there is exactly one, and it lives in
// entities_generated.hpp. The cvar and command declarations are folded
// into that same value by the one generator run.

} // namespace cvars

// --- Enum_Array support ---------------------------------------------
//
// Global scope on purpose: enum_traits is declared in shared/array.hpp,
// which knows nothing about this namespace. `count` is what sizes an
// Enum_Array<cvars::Foo, T>, so adding a value to the .def resizes
// every table over that enum. It does not fill the new row -- see
// rows_in_enum_order in array.hpp for the check that catches that.

template <> struct enum_traits<cvars::Game_Mode>
{
  static constexpr uint32_t count = cvars::Game_Mode_COUNT;
};

template <> struct enum_traits<cvars::Debug_Channel>
{
  static constexpr uint32_t count = cvars::Debug_Channel_COUNT;
};

template <> struct enum_traits<cvars::Bot_Mode>
{
  static constexpr uint32_t count = cvars::Bot_Mode_COUNT;
};

