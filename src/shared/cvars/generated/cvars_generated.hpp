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

enum class Bunnyhop_Mode : uint8_t
{
  none = 0,
  hl2 = 1,
  cs = 2,
};

constexpr uint32_t Bunnyhop_Mode_COUNT = 3;

const char* to_string(Bunnyhop_Mode value);
template <> std::optional<Bunnyhop_Mode> try_from_string<Bunnyhop_Mode>(std::string_view text);

enum class Locomotion_Model : uint8_t
{
  quake = 0,
  instant = 1,
  instant_momentum = 2,
  instant_redirect = 3,
};

constexpr uint32_t Locomotion_Model_COUNT = 4;

const char* to_string(Locomotion_Model value);
template <> std::optional<Locomotion_Model> try_from_string<Locomotion_Model>(std::string_view text);

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
  reflection = 10,
  reflection_capture = 11,
};

constexpr uint32_t Debug_Channel_COUNT = 12;

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
  Locomotion_Model pm_model = Locomotion_Model::quake;
  float pm_maxspeed = 320.0f;
  float pm_overbounce = 1.001f;
  float pm_jumpspeed = 270.0f;
  float g_gravity = 800.0f;
  float pm_speed_threshold = 1.0f;
  float pm_step_height = 18.0f;
  float pm_minimum_land_impact_speed = 150.0f;
  int32_t pm_air_jump_count = 0;
  float pm_air_jump_speed = 270.0f;
  float pm_quake_friction = 6.0f;
  float pm_quake_stop_speed = 100.0f;
  float pm_quake_ground_acceleration = 10.0f;
  float pm_quake_air_acceleration = 5.0f;
  Bunnyhop_Mode pm_quake_bunnyhop = Bunnyhop_Mode::none;
  float pm_quake_jump_boost = 32.0f;
  float pm_quake_jump_boost_max_speed = 480.0f;
  float pm_quake_air_speed_cap = 30.0f;
  float pm_instant_speed_return_seconds = 0.5f;
  float pm_instant_momentum_ground_drag = 10.0f;
  float pm_instant_momentum_air_drag = 0.0f;
  float pm_instant_redirect_turn_degrees_per_second = 360.0f;
  float pm_instant_redirect_ground_drag = 10.0f;
  float pm_instant_redirect_air_drag = 0.0f;
  float sv_hook_pull_speed = 900.0f;
  float sv_hook_max_pull_seconds = 1.5f;
  float sv_hook_arrive_radius = 48.0f;
  float mp_warmup_seconds = 0.0f;
  float mp_countdown_seconds = 5.0f;
  float mp_freeze_seconds = 3.0f;
  float mp_round_seconds = 180.0f;
  float mp_round_end_seconds = 5.0f;
  float mp_next_map_seconds = 5.0f;
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
  float sv_ping_range = 8192.0f;
  float sv_ping_lifetime_seconds = 10.0f;
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
  bool r_lightmap_gpu = true;
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
  bool cl_replay_player_view = true;
  bool cl_replay_panel = true;
  bool cl_ghost_show = true;
  bool cl_noclip = false;
  bool cl_player_unlit = false;
  bool cl_blob_shadow = true;
  float cl_blob_shadow_radius = 20.0f;
  float cl_blob_shadow_opacity = 0.6f;
  float cl_blob_shadow_max_distance = 1024.0f;
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
  network::pascal_string_t<128> next_map = "";
  bool pin_main_thread = true;
  Debug_Channel r_debug_channel = Debug_Channel::off;
  float r_exposure = 1.4f;
  network::pascal_string_t<64> sv_skybox = "";
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
  bool sv_io_debug = false;
  float replay_keyframe_seconds = 2.0f;
  bool sv_replay_auto = false;
  bool sv_ghost_record = true;
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
  pm_model = 0,
  pm_maxspeed = 1,
  pm_overbounce = 2,
  pm_jumpspeed = 3,
  g_gravity = 4,
  pm_speed_threshold = 5,
  pm_step_height = 6,
  pm_minimum_land_impact_speed = 7,
  pm_air_jump_count = 8,
  pm_air_jump_speed = 9,
  pm_quake_friction = 10,
  pm_quake_stop_speed = 11,
  pm_quake_ground_acceleration = 12,
  pm_quake_air_acceleration = 13,
  pm_quake_bunnyhop = 14,
  pm_quake_jump_boost = 15,
  pm_quake_jump_boost_max_speed = 16,
  pm_quake_air_speed_cap = 17,
  pm_instant_speed_return_seconds = 18,
  pm_instant_momentum_ground_drag = 19,
  pm_instant_momentum_air_drag = 20,
  pm_instant_redirect_turn_degrees_per_second = 21,
  pm_instant_redirect_ground_drag = 22,
  pm_instant_redirect_air_drag = 23,
  sv_hook_pull_speed = 24,
  sv_hook_max_pull_seconds = 25,
  sv_hook_arrive_radius = 26,
  mp_warmup_seconds = 27,
  mp_countdown_seconds = 28,
  mp_freeze_seconds = 29,
  mp_round_seconds = 30,
  mp_round_end_seconds = 31,
  mp_next_map_seconds = 32,
  mp_game_over_seconds = 33,
  mp_players_to_start = 34,
  mp_frag_limit = 35,
  sv_aim_max_pitch = 36,
  sv_aim_max_yaw = 37,
  sv_aim_body_turn_rate = 38,
  sv_lag_compensation = 39,
  sv_max_rewind_ticks = 40,
  sv_lag_compensation_debug = 41,
  sv_shot_debug = 42,
  sv_ping_range = 43,
  sv_ping_lifetime_seconds = 44,
  sv_tickrate = 45,
  sv_timeout = 46,
  sv_max_move_backlog = 47,
  sv_map_transfer_fragments_per_tick = 48,
  name = 49,
  cl_max_unacked_inputs = 50,
  r_fov = 51,
  r_zoom_fov = 52,
  r_zoom_easing_time_between_fovs = 53,
  r_shadow_map_size = 54,
  r_shadow_layer_count = 55,
  r_shadow_light_offset = 56,
  r_shadow_bias_slope = 57,
  r_shadow_normal_offset = 58,
  r_shadow_pcf_radius = 59,
  r_shadow_pcss = 60,
  r_shadow_pcss_max_radius = 61,
  r_shadow_debug_light = 62,
  r_lightmap_gpu = 63,
  r_shadow_cascade_count = 64,
  r_shadow_cascade_lambda = 65,
  r_shadow_cascade_distance = 66,
  r_shadow_cascade_blend = 67,
  r_shadow_cascade_caster_extent = 68,
  r_shadow_freeze = 69,
  m_sensitivity = 70,
  m_zoom_sensitivity_ratio = 71,
  cl_maxfps = 72,
  cl_interpolation_delay_ticks = 73,
  cl_interpolation_debug = 74,
  cl_display_latency_ms = 75,
  cl_draw_player_hull = 76,
  cl_spectate_slot = 77,
  cl_replay_player_view = 78,
  cl_replay_panel = 79,
  cl_ghost_show = 80,
  cl_noclip = 81,
  cl_player_unlit = 82,
  cl_blob_shadow = 83,
  cl_blob_shadow_radius = 84,
  cl_blob_shadow_opacity = 85,
  cl_blob_shadow_max_distance = 86,
  cl_aim_debug = 87,
  cl_aim_debug_pitch = 88,
  cl_aim_debug_yaw = 89,
  cl_show_deploy_timer = 90,
  cl_crosshair = 91,
  cl_crosshair_dot = 92,
  cl_crosshair_size = 93,
  cl_crosshair_gap = 94,
  cl_crosshair_thickness = 95,
  cl_crosshair_r = 96,
  cl_crosshair_g = 97,
  cl_crosshair_b = 98,
  cl_crosshair_a = 99,
  editor_speed = 100,
  cl_timescale = 101,
  sound_reference_distance = 102,
  sound_max_distance_cutoff = 103,
  sound_rolloff_factor = 104,
  map_respawn_delay_seconds = 105,
  map_kill_limit = 106,
  map_round_time_limit_seconds = 107,
  next_map = 108,
  pin_main_thread = 109,
  r_debug_channel = 110,
  r_exposure = 111,
  sv_skybox = 112,
  debug_show_collisions = 113,
  debug_show_hitboxes = 114,
  debug_show_navmesh = 115,
  debug_show_box_volumes = 116,
  debug_hide_geometry = 117,
  cl_shot_debug_seconds = 118,
  debug_show_entity_counts = 119,
  debug_show_physics_bodies = 120,
  net_snapshot_debug = 121,
  sv_event_debug = 122,
  cl_event_debug = 123,
  sv_reliable_debug = 124,
  sv_io_debug = 125,
  replay_keyframe_seconds = 126,
  sv_replay_auto = 127,
  sv_ghost_record = 128,
};

// Not a member of the enum above, so `switch` over a cvar_id still
// warns on an unhandled case.
constexpr uint32_t CVAR_COUNT = 129;

enum class command_id : uint16_t
{
  spawn_bot = 0,
  spawn_cube = 1,
  spawn_sphere = 2,
  map = 3,
  setpos = 4,
  join_game = 5,
  spectate = 6,
  sv_mem_report = 7,
  sv_frame_report = 8,
  sv_hitch_report = 9,
  ent_fire = 10,
  restart_round = 11,
  end_match = 12,
  ready = 13,
  sv_replay_record = 14,
  sv_replay_stop = 15,
  bind = 16,
  connect = 17,
  announce = 18,
  noclip = 19,
  mem_report = 20,
  mem_frame = 21,
  mem_stacks = 22,
  frame_report = 23,
  frame_reset = 24,
  hitch_report = 25,
  replay_record = 26,
  replay_play = 27,
  replay_stop = 28,
  replay_pause = 29,
  replay_speed = 30,
  replay_seek = 31,
  replay_skip = 32,
};

constexpr uint32_t COMMAND_COUNT = 33;

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
// @Server  Move the calling player's body to a position
// usage: setpos <x> <y> <z>
void setpos(float x, float y, float z, const command_context_t& context);
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
// @Server  Send an action to one entity, as field=value pairs
// usage: ent_fire <target> <action> [parameters...]
void ent_fire(uint32_t target, std::string_view action, std::string_view parameters, const command_context_t& context);
// @Server  Restart the current round at the start line
// usage: restart_round
void restart_round(const command_context_t& context);
// @Server  End the match and go to next_map
// usage: end_match
void end_match(const command_context_t& context);
// @Server  Toggle your ready vote during warmup
// usage: ready
void ready(const command_context_t& context);
// @Server  Record the running map to replays/<name>.replay
// usage: sv_replay_record [name]
void sv_replay_record(std::string_view name, const command_context_t& context);
// @Server  Finish the server's replay recording
// usage: sv_replay_stop
void sv_replay_stop(const command_context_t& context);
// @Client  Bind a key (a-z) to a command line
// usage: bind <key> <command...>
void bind(std::string_view key, std::string_view command, const command_context_t& context);
// @Client  Connect to a server (ip or ip:port) and enter play
// usage: connect <address>
void connect(std::string_view address, const command_context_t& context);
// @Client  Show a banner on screen for a few seconds
// usage: announce <text...>
void announce(std::string_view text, const command_context_t& context);
// @Client  Toggle the free camera (cl_noclip)
// usage: noclip
void noclip(const command_context_t& context);
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
// @Client  Record what this client receives to replays/<name>.replay
// usage: replay_record [name]
void replay_record(std::string_view name, const command_context_t& context);
// @Client  Play a .replay file in place of a server
// usage: replay_play <path>
void replay_play(std::string_view path, const command_context_t& context);
// @Client  Stop playing a replay, or finish this client's recording
// usage: replay_stop
void replay_stop(const command_context_t& context);
// @Client  Pause or resume the replay
// usage: replay_pause
void replay_pause(const command_context_t& context);
// @Client  Play the replay at a speed (audio is muted away from 1)
// usage: replay_speed <factor>
void replay_speed(float factor, const command_context_t& context);
// @Client  Jump to a time, in seconds from the start of the replay
// usage: replay_seek <seconds>
void replay_seek(float seconds, const command_context_t& context);
// @Client  Jump forward, or back with a negative number of seconds
// usage: replay_skip <seconds>
void replay_skip(float seconds, const command_context_t& context);
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

template <> struct enum_traits<cvars::Bunnyhop_Mode>
{
  static constexpr uint32_t count = cvars::Bunnyhop_Mode_COUNT;
};

template <> struct enum_traits<cvars::Locomotion_Model>
{
  static constexpr uint32_t count = cvars::Locomotion_Model_COUNT;
};

template <> struct enum_traits<cvars::Debug_Channel>
{
  static constexpr uint32_t count = cvars::Debug_Channel_COUNT;
};

template <> struct enum_traits<cvars::Bot_Mode>
{
  static constexpr uint32_t count = cvars::Bot_Mode_COUNT;
};

