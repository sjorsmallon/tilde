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
  ink_normals = 12,
  beam_terms = 13,
  beam_shadow = 14,
};

constexpr uint32_t Debug_Channel_COUNT = 15;

const char* to_string(Debug_Channel value);
template <> std::optional<Debug_Channel> try_from_string<Debug_Channel>(std::string_view text);

enum class Cel_Fill : uint8_t
{
  none = 0,
  hatch = 1,
  dither3d = 2,
  dither3d_original = 3,
};

constexpr uint32_t Cel_Fill_COUNT = 4;

const char* to_string(Cel_Fill value);
template <> std::optional<Cel_Fill> try_from_string<Cel_Fill>(std::string_view text);

enum class Beam_Fill : uint8_t
{
  tint = 0,
  dots = 1,
};

constexpr uint32_t Beam_Fill_COUNT = 2;

const char* to_string(Beam_Fill value);
template <> std::optional<Beam_Fill> try_from_string<Beam_Fill>(std::string_view text);

enum class Pattern_Kind : uint8_t
{
  none = 0,
  stripes = 1,
  grid = 2,
  checks = 3,
  bricks = 4,
  chevrons = 5,
  dots = 6,
};

constexpr uint32_t Pattern_Kind_COUNT = 7;

const char* to_string(Pattern_Kind value);
template <> std::optional<Pattern_Kind> try_from_string<Pattern_Kind>(std::string_view text);

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
// The defaults are the constructor, defined in cvars_generated.cpp: a
// default is tuned far more often than a cvar is added, and an in-class
// initializer would make every tuning a change to this header.
struct cvar_state_t
{
  cvar_state_t();

  Locomotion_Model pm_model;
  float pm_maxspeed;
  float pm_overbounce;
  float pm_jumpspeed;
  float g_gravity;
  float pm_speed_threshold;
  float pm_step_height;
  float pm_minimum_land_impact_speed;
  int32_t pm_air_jump_count;
  float pm_air_jump_speed;
  float pm_quake_friction;
  float pm_quake_stop_speed;
  float pm_quake_ground_acceleration;
  float pm_quake_air_acceleration;
  Bunnyhop_Mode pm_quake_bunnyhop;
  float pm_quake_jump_boost;
  float pm_quake_jump_boost_max_speed;
  float pm_quake_air_speed_cap;
  float pm_instant_speed_return_seconds;
  float pm_instant_momentum_ground_drag;
  float pm_instant_momentum_air_drag;
  float pm_instant_redirect_turn_degrees_per_second;
  float pm_instant_redirect_ground_drag;
  float pm_instant_redirect_air_drag;
  float sv_hook_pull_speed;
  float sv_hook_arrive_radius;
  float mp_warmup_seconds;
  float mp_countdown_seconds;
  float mp_freeze_seconds;
  float mp_round_seconds;
  float mp_round_end_seconds;
  float mp_next_map_seconds;
  float mp_game_over_seconds;
  int32_t mp_players_to_start;
  int32_t mp_frag_limit;
  float sv_aim_max_pitch;
  float sv_aim_max_yaw;
  float sv_aim_body_turn_rate;
  float sv_reveal_light_range;
  float sv_reveal_light_half_angle;
  float sv_reveal_light_overhead_height;
  bool sv_lag_compensation;
  int32_t sv_max_rewind_ticks;
  bool sv_lag_compensation_debug;
  bool sv_shot_debug;
  float sv_ping_range;
  float sv_ping_lifetime_seconds;
  float sv_tickrate;
  float sv_timeout;
  int32_t sv_max_move_backlog;
  int32_t sv_map_transfer_fragments_per_tick;
  network::pascal_string_t<32> name;
  int32_t cl_max_unacked_inputs;
  float r_fov;
  float r_zoom_fov;
  float r_zoom_easing_time_between_fovs;
  int32_t r_shadow_map_size;
  int32_t r_shadow_layer_count;
  float r_shadow_light_offset;
  float r_shadow_bias_slope;
  float r_shadow_normal_offset;
  int32_t r_shadow_pcf_radius;
  bool r_shadow_pcss;
  float r_shadow_pcss_max_radius;
  int32_t r_shadow_debug_light;
  bool r_lightmap_gpu;
  int32_t r_shadow_cascade_count;
  float r_shadow_cascade_lambda;
  float r_shadow_cascade_distance;
  float r_shadow_cascade_blend;
  float r_shadow_cascade_caster_extent;
  bool r_shadow_freeze;
  float m_sensitivity;
  float m_zoom_sensitivity_ratio;
  float cl_maxfps;
  float cl_interpolation_delay_ticks;
  bool cl_smooth_drawn_tick;
  bool cl_interpolation_debug;
  float cl_display_latency_ms;
  bool cl_draw_player_hull;
  int32_t cl_spectate_slot;
  bool cl_replay_player_view;
  bool cl_replay_panel;
  bool cl_ghost_show;
  bool cl_noclip;
  bool cl_player_unlit;
  bool cl_blob_shadow;
  float cl_blob_shadow_radius;
  float cl_blob_shadow_opacity;
  float cl_blob_shadow_max_distance;
  bool cl_aim_debug;
  float cl_aim_debug_pitch;
  float cl_aim_debug_yaw;
  bool cl_show_deploy_timer;
  bool cl_auto_equip_weapon_on_pickup;
  bool cl_crosshair;
  bool cl_crosshair_dot;
  float cl_crosshair_size;
  float cl_crosshair_gap;
  float cl_crosshair_thickness;
  uint32_t cl_crosshair_r;
  uint32_t cl_crosshair_g;
  uint32_t cl_crosshair_b;
  uint32_t cl_crosshair_a;
  float editor_speed;
  float cl_timescale;
  float sound_reference_distance;
  float sound_max_distance_cutoff;
  float sound_rolloff_factor;
  float map_respawn_delay_seconds;
  int32_t map_kill_limit;
  float map_round_time_limit_seconds;
  network::pascal_string_t<128> next_map;
  bool pin_main_thread;
  Debug_Channel r_debug_channel;
  float r_exposure;
  float r_ambient_floor;
  bool r_stylized;
  bool r_cel;
  float r_cel_terminator;
  float r_cel_shadow_edge;
  float r_cel_softness;
  float r_cel_shadow_red;
  float r_cel_shadow_green;
  float r_cel_shadow_blue;
  float r_cel_bands;
  float r_cel_flat_albedo;
  float r_cel_black;
  float r_cel_halftone;
  float r_cel_halftone_paper;
  float r_cel_halftone_ink;
  float r_cel_halftone_gamma;
  Cel_Fill r_cel_fill;
  float r_cel_fill_strength;
  float r_cel_fill_spacing;
  float r_cel_fill_edge;
  float r_cel_fill_shadow_tone_dark;
  float r_cel_fill_shadow_tone_light;
  float r_cel_fill_ambient_dark;
  float r_cel_fill_ambient_light;
  float r_cel_fill_tone_lit;
  float r_cel_fill_material;
  float r_cel_hatch_width;
  float r_cel_dither3d_size_variability;
  float r_cel_dither3d_contrast;
  float r_cel_dither3d_stretch_smoothness;
  float r_cel_speckle;
  float r_cel_speckle_spacing;
  float r_cel_speckle_density;
  float r_cel_speckle_radius;
  float r_cel_pebble;
  float r_cel_pebble_spacing;
  float r_cel_pebble_density;
  float r_cel_pebble_size;
  float r_cel_pebble_irregularity;
  float r_cel_pebble_width;
  Pattern_Kind r_pattern_preview;
  float r_pattern_preview_spacing_along;
  float r_pattern_preview_spacing_across;
  float r_pattern_preview_angle;
  float r_pattern_preview_scroll;
  float r_pattern_preview_coverage;
  float r_pattern_preview_shape;
  float r_pattern_preview_strength;
  float r_pattern_preview_red;
  float r_pattern_preview_green;
  float r_pattern_preview_blue;
  bool r_ink;
  float r_ink_threshold;
  float r_ink_crease_degrees;
  int32_t r_ink_width;
  float r_ink_tint;
  float r_ink_on_black;
  float r_ink_wobble;
  float r_ink_wobble_scale;
  float r_ink_boil;
  float r_ink_weight_near;
  float r_ink_weight_distance;
  float r_rim;
  int32_t r_rim_width;
  float r_misprint;
  float r_misprint_distance;
  float r_flashlight_intensity;
  float r_flashlight_red;
  float r_flashlight_green;
  float r_flashlight_blue;
  float r_flashlight_inner;
  bool r_fxaa;
  float r_fxaa_subpixel;
  bool r_fog;
  float r_fog_distance;
  float r_fog_anisotropy;
  bool r_beam;
  float r_beam_alpha;
  Beam_Fill r_beam_fill;
  float r_beam_dot_spacing;
  float r_beam_edge_pixels;
  bool r_beam_surface_bias;
  bool r_shadow_volume;
  float r_shadow_volume_alpha;
  bool r_look_panel;
  network::pascal_string_t<64> sv_skybox;
  bool debug_show_collisions;
  bool debug_show_hitboxes;
  bool debug_show_navmesh;
  bool debug_show_box_volumes;
  bool debug_hide_geometry;
  float cl_shot_debug_seconds;
  bool cl_shadow_volume_debug;
  bool cl_solid_beam_debug;
  bool debug_show_entity_counts;
  bool net_snapshot_debug;
  bool sv_event_debug;
  bool cl_event_debug;
  bool sv_reliable_debug;
  bool sv_io_debug;
  float replay_keyframe_seconds;
  bool sv_replay_auto;
  bool sv_ghost_record;
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
  sv_hook_arrive_radius = 25,
  mp_warmup_seconds = 26,
  mp_countdown_seconds = 27,
  mp_freeze_seconds = 28,
  mp_round_seconds = 29,
  mp_round_end_seconds = 30,
  mp_next_map_seconds = 31,
  mp_game_over_seconds = 32,
  mp_players_to_start = 33,
  mp_frag_limit = 34,
  sv_aim_max_pitch = 35,
  sv_aim_max_yaw = 36,
  sv_aim_body_turn_rate = 37,
  sv_reveal_light_range = 38,
  sv_reveal_light_half_angle = 39,
  sv_reveal_light_overhead_height = 40,
  sv_lag_compensation = 41,
  sv_max_rewind_ticks = 42,
  sv_lag_compensation_debug = 43,
  sv_shot_debug = 44,
  sv_ping_range = 45,
  sv_ping_lifetime_seconds = 46,
  sv_tickrate = 47,
  sv_timeout = 48,
  sv_max_move_backlog = 49,
  sv_map_transfer_fragments_per_tick = 50,
  name = 51,
  cl_max_unacked_inputs = 52,
  r_fov = 53,
  r_zoom_fov = 54,
  r_zoom_easing_time_between_fovs = 55,
  r_shadow_map_size = 56,
  r_shadow_layer_count = 57,
  r_shadow_light_offset = 58,
  r_shadow_bias_slope = 59,
  r_shadow_normal_offset = 60,
  r_shadow_pcf_radius = 61,
  r_shadow_pcss = 62,
  r_shadow_pcss_max_radius = 63,
  r_shadow_debug_light = 64,
  r_lightmap_gpu = 65,
  r_shadow_cascade_count = 66,
  r_shadow_cascade_lambda = 67,
  r_shadow_cascade_distance = 68,
  r_shadow_cascade_blend = 69,
  r_shadow_cascade_caster_extent = 70,
  r_shadow_freeze = 71,
  m_sensitivity = 72,
  m_zoom_sensitivity_ratio = 73,
  cl_maxfps = 74,
  cl_interpolation_delay_ticks = 75,
  cl_smooth_drawn_tick = 76,
  cl_interpolation_debug = 77,
  cl_display_latency_ms = 78,
  cl_draw_player_hull = 79,
  cl_spectate_slot = 80,
  cl_replay_player_view = 81,
  cl_replay_panel = 82,
  cl_ghost_show = 83,
  cl_noclip = 84,
  cl_player_unlit = 85,
  cl_blob_shadow = 86,
  cl_blob_shadow_radius = 87,
  cl_blob_shadow_opacity = 88,
  cl_blob_shadow_max_distance = 89,
  cl_aim_debug = 90,
  cl_aim_debug_pitch = 91,
  cl_aim_debug_yaw = 92,
  cl_show_deploy_timer = 93,
  cl_auto_equip_weapon_on_pickup = 94,
  cl_crosshair = 95,
  cl_crosshair_dot = 96,
  cl_crosshair_size = 97,
  cl_crosshair_gap = 98,
  cl_crosshair_thickness = 99,
  cl_crosshair_r = 100,
  cl_crosshair_g = 101,
  cl_crosshair_b = 102,
  cl_crosshair_a = 103,
  editor_speed = 104,
  cl_timescale = 105,
  sound_reference_distance = 106,
  sound_max_distance_cutoff = 107,
  sound_rolloff_factor = 108,
  map_respawn_delay_seconds = 109,
  map_kill_limit = 110,
  map_round_time_limit_seconds = 111,
  next_map = 112,
  pin_main_thread = 113,
  r_debug_channel = 114,
  r_exposure = 115,
  r_ambient_floor = 116,
  r_stylized = 117,
  r_cel = 118,
  r_cel_terminator = 119,
  r_cel_shadow_edge = 120,
  r_cel_softness = 121,
  r_cel_shadow_red = 122,
  r_cel_shadow_green = 123,
  r_cel_shadow_blue = 124,
  r_cel_bands = 125,
  r_cel_flat_albedo = 126,
  r_cel_black = 127,
  r_cel_halftone = 128,
  r_cel_halftone_paper = 129,
  r_cel_halftone_ink = 130,
  r_cel_halftone_gamma = 131,
  r_cel_fill = 132,
  r_cel_fill_strength = 133,
  r_cel_fill_spacing = 134,
  r_cel_fill_edge = 135,
  r_cel_fill_shadow_tone_dark = 136,
  r_cel_fill_shadow_tone_light = 137,
  r_cel_fill_ambient_dark = 138,
  r_cel_fill_ambient_light = 139,
  r_cel_fill_tone_lit = 140,
  r_cel_fill_material = 141,
  r_cel_hatch_width = 142,
  r_cel_dither3d_size_variability = 143,
  r_cel_dither3d_contrast = 144,
  r_cel_dither3d_stretch_smoothness = 145,
  r_cel_speckle = 146,
  r_cel_speckle_spacing = 147,
  r_cel_speckle_density = 148,
  r_cel_speckle_radius = 149,
  r_cel_pebble = 150,
  r_cel_pebble_spacing = 151,
  r_cel_pebble_density = 152,
  r_cel_pebble_size = 153,
  r_cel_pebble_irregularity = 154,
  r_cel_pebble_width = 155,
  r_pattern_preview = 156,
  r_pattern_preview_spacing_along = 157,
  r_pattern_preview_spacing_across = 158,
  r_pattern_preview_angle = 159,
  r_pattern_preview_scroll = 160,
  r_pattern_preview_coverage = 161,
  r_pattern_preview_shape = 162,
  r_pattern_preview_strength = 163,
  r_pattern_preview_red = 164,
  r_pattern_preview_green = 165,
  r_pattern_preview_blue = 166,
  r_ink = 167,
  r_ink_threshold = 168,
  r_ink_crease_degrees = 169,
  r_ink_width = 170,
  r_ink_tint = 171,
  r_ink_on_black = 172,
  r_ink_wobble = 173,
  r_ink_wobble_scale = 174,
  r_ink_boil = 175,
  r_ink_weight_near = 176,
  r_ink_weight_distance = 177,
  r_rim = 178,
  r_rim_width = 179,
  r_misprint = 180,
  r_misprint_distance = 181,
  r_flashlight_intensity = 182,
  r_flashlight_red = 183,
  r_flashlight_green = 184,
  r_flashlight_blue = 185,
  r_flashlight_inner = 186,
  r_fxaa = 187,
  r_fxaa_subpixel = 188,
  r_fog = 189,
  r_fog_distance = 190,
  r_fog_anisotropy = 191,
  r_beam = 192,
  r_beam_alpha = 193,
  r_beam_fill = 194,
  r_beam_dot_spacing = 195,
  r_beam_edge_pixels = 196,
  r_beam_surface_bias = 197,
  r_shadow_volume = 198,
  r_shadow_volume_alpha = 199,
  r_look_panel = 200,
  sv_skybox = 201,
  debug_show_collisions = 202,
  debug_show_hitboxes = 203,
  debug_show_navmesh = 204,
  debug_show_box_volumes = 205,
  debug_hide_geometry = 206,
  cl_shot_debug_seconds = 207,
  cl_shadow_volume_debug = 208,
  cl_solid_beam_debug = 209,
  debug_show_entity_counts = 210,
  net_snapshot_debug = 211,
  sv_event_debug = 212,
  cl_event_debug = 213,
  sv_reliable_debug = 214,
  sv_io_debug = 215,
  replay_keyframe_seconds = 216,
  sv_replay_auto = 217,
  sv_ghost_record = 218,
};

// Not a member of the enum above, so `switch` over a cvar_id still
// warns on an unhandled case.
constexpr uint32_t CVAR_COUNT = 219;

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
  sv_shadow_volume_report = 9,
  sv_hitch_report = 10,
  ent_fire = 11,
  restart_round = 12,
  end_match = 13,
  ready = 14,
  sv_replay_record = 15,
  sv_replay_stop = 16,
  shadow_volume_report = 17,
  bind = 18,
  connect = 19,
  announce = 20,
  noclip = 21,
  mem_report = 22,
  mem_frame = 23,
  mem_stacks = 24,
  frame_report = 25,
  frame_reset = 26,
  gpu_report = 27,
  gpu_reset = 28,
  hitch_report = 29,
  replay_record = 30,
  replay_play = 31,
  replay_stop = 32,
  replay_pause = 33,
  replay_speed = 34,
  replay_seek = 35,
  replay_skip = 36,
};

constexpr uint32_t COMMAND_COUNT = 37;

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
// @Server  Print how the shadow volumes cut this tick: lights, casters, receivers and every refusal
// usage: sv_shadow_volume_report
void sv_shadow_volume_report(const command_context_t& context);
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
// @Client  Print how the shadow volumes cut for the newest snapshot: lights, casters, receivers and every refusal
// usage: shadow_volume_report
void shadow_volume_report(const command_context_t& context);
// @Client  Bind a key (a-z, 0-9, f1-f12, space, arrows, kp_0-kp_9) to a command line
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
// @Client  Print the GPU time distribution per render pass so far
// usage: gpu_report
void gpu_report(const command_context_t& context);
// @Client  Discard the GPU time distribution and start measuring again
// usage: gpu_reset
void gpu_reset(const command_context_t& context);
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

template <> struct enum_traits<cvars::Cel_Fill>
{
  static constexpr uint32_t count = cvars::Cel_Fill_COUNT;
};

template <> struct enum_traits<cvars::Beam_Fill>
{
  static constexpr uint32_t count = cvars::Beam_Fill_COUNT;
};

template <> struct enum_traits<cvars::Pattern_Kind>
{
  static constexpr uint32_t count = cvars::Pattern_Kind_COUNT;
};

template <> struct enum_traits<cvars::Bot_Mode>
{
  static constexpr uint32_t count = cvars::Bot_Mode_COUNT;
};

