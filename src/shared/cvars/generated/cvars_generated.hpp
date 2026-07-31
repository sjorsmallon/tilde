// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/cvars/cvars.def by def_gen. Do not edit.
#pragma once

#include "cvars/cvar_runtime.hpp"
#include "span.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace cvars
{

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

enum class Bot_Mode : uint8_t
{
  idle = 0,
  chase = 1,
  regular = 2,
};

const char* to_string(Bot_Mode value);
bool from_string(std::string_view text, Bot_Mode* out_value);

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
  float sv_tickrate = 60.0f;
  float r_fov = 90.0f;
  float cl_maxfps = 1000.0f;
  float editor_speed = 1600.0f;
  float cl_timescale = 1.0f;
  float sound_reference_distance = 150.0f;
  float sound_max_distance_cutoff = 4000.0f;
  float sound_rolloff_factor = 1.0f;
  bool debug_show_collisions = false;
  bool debug_show_hitboxes = false;
  bool debug_show_navmesh = false;
  bool debug_show_box_volumes = false;
  bool net_snapshot_debug = false;
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
  sv_tickrate = 11,
  r_fov = 12,
  cl_maxfps = 13,
  editor_speed = 14,
  cl_timescale = 15,
  sound_reference_distance = 16,
  sound_max_distance_cutoff = 17,
  sound_rolloff_factor = 18,
  debug_show_collisions = 19,
  debug_show_hitboxes = 20,
  debug_show_navmesh = 21,
  debug_show_box_volumes = 22,
  net_snapshot_debug = 23,
};

// Not a member of the enum above, so `switch` over a cvar_id still
// warns on an unhandled case.
constexpr uint32_t CVAR_COUNT = 24;

enum class command_id : uint16_t
{
  spawn_bot = 0,
  spawn_cube = 1,
  spawn_sphere = 2,
  map = 3,
  noclip = 4,
  bind = 5,
};

constexpr uint32_t COMMAND_COUNT = 6;

enum cvar_type : uint8_t
{
  CVAR_TYPE_F32 = 0,
  CVAR_TYPE_I32,
  CVAR_TYPE_U32,
  CVAR_TYPE_BOOL,
  CVAR_TYPE_STRING,
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
bool find_cvar(std::string_view name, cvar_id* out_id);
bool find_command(std::string_view name, command_id* out_id);

// The @Mirrored subset, so both ends agree on the sync set by
// construction rather than by each filtering on flags and hoping.
Span<const cvar_id> mirrored_cvars();

// The ONLY place cvar bytes become characters: console echo, config
// files, and the mirrored-value payload all go through this pair.
// Floats use the shortest representation that round-trips.
//
// cvar_from_text returns false and leaves the value ALONE when the text
// does not parse -- the caller reports it, because only the caller knows
// whether it came from a console line, a config file or the wire.
bool cvar_to_text(const cvar_state_t& state, cvar_id id, std::string& out_text);
bool cvar_from_text(cvar_state_t& state, cvar_id id, std::string_view text);

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
// @Client  Bind a key (a-z) to a command line
// usage: bind <key> <command...>
void bind(std::string_view key, std::string_view command, const command_context_t& context);
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
