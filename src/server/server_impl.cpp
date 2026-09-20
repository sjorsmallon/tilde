// The module: its one context, its lifetime (init / shutdown / the map load),
// and the @Server command handlers the generated binder calls. The TICK is in
// tick.cpp -- tick_def.md is the design of record for what runs in what order.

#include "server_impl.hpp"

#include "../shared/collision_detection.hpp"
#include "../shared/entities/entity_reflection.hpp"
#include "../shared/frame_timing.hpp"
#include "../shared/movement_settings.hpp"
#include "../shared/memory_audit.hpp"
#include "../shared/network/packet.hpp"
#include "../shared/player_animator.hpp"
#include "../shared/player_constants.hpp"
#include "../shared/player_rig.hpp"
#include "../shared/round_phase_rules.hpp"
#include "../shared/weapons.hpp"
#include "damage.hpp"
#include "entity_io_console.hpp"
#include "entity_io_queue.hpp"
#include "entity_lifecycle.hpp"
#include "send_protobuf_message.hpp"
#include "server_api.hpp"
#include "server_messages.hpp"
#include "systems/bot_system.hpp"
#include "systems/game_rules_system.hpp"
#include "systems/inventory_system.hpp"
#include "systems/mover_system.hpp"
#include "systems/physics_body_system.hpp"
#include "systems/respawn_system.hpp"
#include "weapon_fire.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include "cvars/cvar_console.hpp"
#include "game_session.hpp"
#include "log.hpp"
#include "map.hpp"
#include "network/bitstream.hpp"
#include "network/map_transfer.hpp"
#include "network/server_transport_layer.hpp"
#include "server_context.hpp"
#include "timed_function.hpp"

namespace server
{

server_context_t g_server_context;
// little helper function to get a good location to spawn physics objects.
static std::optional<vec3f>
get_position_in_front_of(server_context_t &context, int32_t caller_slot)
{
  if (!is_valid_client_slot(caller_slot)) return std::nullopt;

  const entities::Player_Entity* player =
      context.world.session.entity_system.get<entities::Player_Entity>(
          context.clients[caller_slot].player_uid);

  if (!player) return std::nullopt;

  float yaw_rad   = linalg::to_radians(player->view_angle_yaw);
  float pitch_rad = linalg::to_radians(player->view_angle_pitch);
  vec3f forward = {std::cos(yaw_rad) * std::cos(pitch_rad),
                   std::sin(pitch_rad),
                   std::sin(yaw_rad) * std::cos(pitch_rad)};

  constexpr float forward_offset = 80.f;
  constexpr float eye_height = 40.f;
  return player->position + vec3f{0, eye_height, 0} + forward * forward_offset;
}


static void return_client_to_spectate(server_context_t &context, int32_t slot)
{
  const shared::entity_uid_t player_uid = context.clients[slot].player_uid;
  if (player_uid == shared::null_entity_uid)
    return;

  destroy_inventory(context, player_uid);
  destroy_entity(context, player_uid);
  context.clients[slot].player_uid = shared::null_entity_uid;
}


void disconnect_client(server_context_t &context, int32_t slot,
                       std::string_view reason)
{
  const network::Address address = context.transport_layer.clients[slot].address;

  return_client_to_spectate(context, slot);
  reset_client_slot(context, slot);

  broadcast_server_text_message(
      context, std::format("Player {} (slot {})", reason, slot));
  log_terminal("Player {} slot {}: {}", reason, slot, address.to_string());
}

void connect_client(server_context_t &context, int32_t slot,
                    const network::Address &address,
                    const network::pascal_string_t<32> &player_name)
{
  reset_client_slot(context, slot);
  network::occupy_client_slot(context.transport_layer, slot, address,
                              context.tick_number);
  context.clients[slot].player_name = player_name;

  log_terminal("Player {} joined at slot {} (spectating)", player_name.c_str(),
               slot);
}

void process_client_leave_message(server_context_t &context,
                         const network::Address &sender)
{
  const std::optional<int32_t> sender_slot =
      network::try_find_client_slot(context.transport_layer, sender);
  if (!sender_slot)
  {
    log_error("tried to handle a client leave from {}, which occupies no slot",
              sender.to_string());
    return;
  }

  disconnect_client(context, *sender_slot, "Left.");
}

// apply all the stored map cvars that are set in the editor.
// this more or less just executes console lines.
static void apply_map_cvars_that_were_supplied_from_the_editor(server_context_t &context, const shared::map_t &map)
{
  for (const std::string &line : map.attached_cvars)
  {
    std::string reply;
    const auto command_context = cvars::command_context_t{};
    const cvars::console_result_t result = cvars::execute_console_line(
        *context.cvars, *context.commands, line, command_context, &reply);

    if (result != cvars::console_result_t::ok)
    {
      log_error("Map '{}': cvar line '{}' was not applied: {}", map.name, line,
                reply.empty() ? "unknown name" : reply);
      continue;
    }

    log_terminal("Map cvar: {}", line);

    const shared::cvar_line_t split = shared::split_cvar_line(line);
    if (const std::optional<cvars::cvar_id> id = cvars::try_find_cvar(split.name))
      context.world.cvars_applied_by_map.push_back(*id);
  }

  // A model's group is read only when pm_model names it, so a map setting one
  // of another model's numbers is setting a value nothing will read. Checked
  // after the whole list, because pm_model may be set by any line in it.
  const cvars::Locomotion_Model model = context.cvars->pm_model;
  for (const cvars::cvar_id id : context.world.cvars_applied_by_map)
  {
    const std::string_view name = cvars::cvar_info(id).name;
    const std::optional<cvars::Locomotion_Model> belongs_to =
        shared::locomotion_model_a_cvar_belongs_to(name);
    if (belongs_to && *belongs_to != model)
      log_error("Map '{}': '{}' is read only under pm_model {}, and this map sets pm_model {}",
                map.name, name, to_string(*belongs_to), to_string(model));
  }
}

static std::optional<std::string> start_server_replay_recording(server_context_t& context,
                                                                const std::string& name)
{
  world_t& world = context.world;
  std::optional<std::string> path = shared::try_start_replay_recording_of_map(
      world.replay_recorder, world.current_map, world.session.map_name,
      static_cast<uint32_t>(context.cvars->sv_tickrate), context.cvars->replay_keyframe_seconds, name);
  if (path)
    log_terminal("replay: recording '{}' to '{}'", world.session.map_name, *path);
  return path;
}

static bool load_map_file_into_context(server_context_t &context,
                                const std::string &map_path)
{
  auto loaded_map = std::optional<shared::map_t>{};

  if (map_path.empty())
    log_terminal("empty map path. leaving session empty.");
  else
  {
    log_terminal("Loading map '{}'...", map_path);
    loaded_map = shared::try_load_map(map_path);
    if (!loaded_map)
      log_error("Failed to load map '{}'. The map currently loaded stays.", map_path);
  }

  // check if all the connections specified in the map are valid.
  // @FIXME(SJM): why do this at map load and not at save? because we can save
  // starved prefabs?
  if (loaded_map)
  {
    const std::vector<shared::connection_refusal_t> refusals =
        shared::validate_map_connections(*loaded_map);
    if (!refusals.empty())
    {
      for (const shared::connection_refusal_t& refusal : refusals)
        log_error("Map '{}' connection {}: {}", map_path, refusal.index, refusal.reason);
      log_error("Refusing map '{}': {} ill-typed connection(s).\n The map currently loaded stays.",
                map_path, refusals.size());
      loaded_map.reset();
    }
  }

  if (loaded_map)
  {
    const std::vector<shared::path_refusal_t> refusals = shared::validate_map_paths(*loaded_map);
    if (!refusals.empty())
    {
      for (const shared::path_refusal_t& refusal : refusals)
        log_error("Map '{}': {}", map_path, refusal.reason);
      log_error("Refusing map '{}': {} broken path link(s). The map currently loaded stays.",
                map_path, refusals.size());
      loaded_map.reset();
    }
  }

  if (loaded_map && count_rules_entities(*loaded_map) > 1)
  {
    log_error("Refusing map '{}': {} Game_Rules_Entity, and a map runs one match. "
              "The map currently loaded stays.",
              map_path, count_rules_entities(*loaded_map));
    loaded_map.reset();
  }

  if (!loaded_map)
  {
    if (!context.world.physics)
    {
      reset_state_in_preparation_for_new_map_load(context);
      context.world.physics = make_physics_state();
      install_match(context, context.tick_number, static_cast<uint32_t>(context.cvars->sv_tickrate));
    }
    return false;
  }

  reset_state_in_preparation_for_new_map_load(context);
  context.world.physics = make_physics_state();

  world_t& world = context.world;

  // steal the map from this function temporary.
  world.current_map = std::move(*loaded_map);
  shared::map_t& server_map = world.current_map;

  apply_map_cvars_that_were_supplied_from_the_editor(context, server_map);

  world.session = shared::build_session(server_map);
  world.current_map_path  = map_path;
  world.map_content_hash = shared::compute_map_content_hash(server_map);
  install_match(context, context.tick_number, static_cast<uint32_t>(context.cvars->sv_tickrate));
  install_movers(context);

  shared::populate_static_physics_bodies(*world.physics, server_map);
  

  // spawn players.
  int human_spawn_count = 0;
  int bot_spawn_count = 0;
  {
    Span<entities::Player_Spawn_Entity> spawn_pool =
        world.session.entity_system.entities_of<entities::Player_Spawn_Entity>();

    for (const entities::Player_Spawn_Entity& player_spawn : spawn_pool)
    {
      if (player_spawn.spawn_type == entities::Spawn_Type::Bot)
      {
        world.bots.push_back(spawn_bot(world.session, *world.physics, player_spawn,
                                       world.next_bot_slot, bot_behavior_t::Regular));
        world.next_bot_slot += 1;
        bot_spawn_count += 1;
      }
      else
        human_spawn_count += 1;
    }
  }

  log_terminal("Loaded map='{}', {} human spawns, {} bot spawns",
               world.session.map_name, human_spawn_count, bot_spawn_count);

  if (context.cvars->sv_replay_auto)
    start_server_replay_recording(context, "");

  return true;
}

void install_memory_audit(memory_audit::memory_audit_state_t *state)
{
  memory_audit::set_state(state);
}

void install_frame_timing(frame_timing::frame_timing_state_t *state)
{
  frame_timing::set_state(state);
}

bool init(cvars::cvar_state_t* cvar_state, cvars::command_table_t* cvar_command_table,
          assets::asset_state_t* asset_state)
{
  log_terminal("--- Initializing Server ---");
  log_terminal("Server port: {}", network::server_port_number);

  if (!cvar_state || !cvar_command_table || !asset_state)
  {
    log_error("cvar_state_t, command_table_t and asset_state_t need to be provided by the launcher.");
    return false;
  }

  assets::set_state(asset_state);
  g_server_context.cvars = cvar_state;
  g_server_context.commands = cvar_command_table;
  cvars::bind_server_commands(*cvar_command_table);

  // initialize to defaults.
  g_server_context.last_broadcast_cvars = *cvar_state;

  // this is kind of a shit way to load a static upfront and I don't like it.
  shared::player_rig();

  static bool jolt_initialized = false;
  if (!jolt_initialized)
  {
    jolt_init();
    jolt_initialized = true;
  }

  if (!g_server_context.socket.open(network::server_port_number,
                                    network::server_receive_buffer_size_in_bytes))
  {
    log_error("Failed to open server socket on port {}. Port may be in use or insufficient permissions.",
                 network::server_port_number);
    return false;
  }
  log_terminal("Successfully bound server socket to port {}", network::server_port_number);

  auto map_name = std::string{};

  std::ifstream f("last_map.txt");
  if (f.is_open())
  {
    if (std::getline(f, map_name))
      log_terminal("Boot map from last_map.txt: '{}'", map_name);
    f.close();
  }

  load_map_file_into_context(g_server_context, map_name);

  log_terminal("--- Server initialization complete ---");
  return true;
}


static void send_change_map_message(server_context_t &context, int32_t slot)
{
  shared::change_map_message_t msg;
  msg.map_path     = current_map_wire_id(context);
  msg.map_name     = context.world.session.map_name;
  msg.content_hash = context.world.map_content_hash;

  auto writer = network::Bit_Writer{};
  shared::serialize_change_map(writer, msg);

  network::queue_reliable_message(
      context.transport_layer.clients[slot].reliable_stream,
      static_cast<network::uint8>(network::Message_Type::CmdChangeMap),
      writer.buffer);
}

static std::optional<std::string> try_resolve_map_path(const std::string &name)
{
  const std::string candidates[] = {
      name,
      "maps/" + name,
      "maps/" + name + ".source",
  };
  for (const std::string &candidate : candidates)
  {
    if (std::filesystem::is_regular_file(candidate))
      return candidate;
  }
  return std::nullopt;
}

bool change_map_to(const std::string &map_name)
{
  server_context_t &context = g_server_context;

  const std::optional<std::string> map_path = try_resolve_map_path(map_name);
  if (!map_path)
  {
    log_error("change_map_to: '{}' not found (also tried 'maps/{}' and "
              "'maps/{}.source'). Not switching.",
              map_name, map_name, map_name);
    return false;
  }

  log_terminal("--- Changing server map to: '{}' ---", *map_path);

  // wipe the session.
  if (!load_map_file_into_context(context, *map_path)) return false;

  // Keep connected players connected across the switch
  for (connected_client_t row : connected_clients(context))
  {
    // e.g. joined mid-round.
    if (row.client.wants_to_play)
      try_admit_player(context, row.slot);

    send_change_map_message(context, row.slot);
  }
  return true;
}

void service_pending_map_change(server_context_t &context)
{
  if (context.pending_map_change.empty())
    return;

  const std::string requested_map = std::move(context.pending_map_change);
  context.pending_map_change.clear();
  change_map_to(requested_map);
}

void shutdown()
{
  timed_function();
  log_terminal("--- Shutting down Server ---");
}

double get_tick_interval()
{
  const float tickrate = g_server_context.cvars
                             ? g_server_context.cvars->sv_tickrate
                             : cvars::cvar_state_t{}.sv_tickrate;
  return 1.0 / static_cast<double>(tickrate);
}

uint32_t get_tick_number() { return g_server_context.tick_number; }

const shared::game_session_t *get_session_for_integrated_client()
{
  return &g_server_context.world.session;
}

} // namespace server


// these are from the cvars.def. anything defined there requires us to have a handler on the server side. the functions are declared in a generated header
// and this is just the  simplest way I could imagine listrening to it.


namespace cvars::commands
{

void spawn_bot(Bot_Mode mode, const command_context_t &)
{
  using namespace server;

  bot_behavior_t type = bot_behavior_t::Idle;
  switch (mode)
  {
    case Bot_Mode::idle:    type = bot_behavior_t::Idle;    break;
    case Bot_Mode::chase:   type = bot_behavior_t::Chase;   break;
    case Bot_Mode::regular: type = bot_behavior_t::Regular; break;
  }

  server_context_t &server_context = g_server_context;
  world_t          &world          = server_context.world;

 
  const entities::Player_Spawn_Entity* marker =
      try_pick_human_spawn(world.session, Spawn_Policy::Rotate_Markers,
                           entities::Team_Allegiance::Free_For_All,
                           static_cast<uint32_t>(world.bots.size()));
  if (marker == nullptr)
    log_error("spawn_bot: map '{}' declares no Spawn_Type::Human marker — "
              "spawning the bot at origin",
              world.session.map_name);

  world.bots.push_back(server::spawn_bot(world.session, *world.physics,
                                         marker ? *marker : origin_fallback_spawn(),
                                         world.next_bot_slot++, type));

  log_terminal("spawn_bot: spawned {} bot at slot {}", cvars::to_string(mode),
               world.next_bot_slot - 1);
}

void join_game(const command_context_t &command_context)
{
  using namespace server;

  server_context_t &server_context = g_server_context;
  const int32_t slot = command_context.caller_slot;

  // caller_slot is -1 when the server itself typed the line. A dedicated
  // server's console has no body to spawn, so there is nothing to do but say
  // why -- unlike spawn_bot, this command is meaningless without a caller.
  if (!is_valid_client_slot(slot))
  {
    log_error("join_game: no calling player (caller_slot {}) — this command "
              "only means something from a connected client",
              slot);
    return;
  }

  if (server_context.clients[slot].player_uid != shared::null_entity_uid)
  {
    log_terminal("join_game: slot {} already has a player entity — ignoring",
                 slot);
    return;
  }

  try_admit_player(server_context, slot);
}

void spectate(const command_context_t &command_context)
{
  using namespace server;

  server_context_t &server_context = g_server_context;
  const int32_t     slot           = command_context.caller_slot;

  // Same reason join_game above needs a caller: a dedicated server's console has
  // no body to give up.
  if (!is_valid_client_slot(slot))
  {
    log_error("spectate: no calling player (caller_slot {}) — this command "
              "only means something from a connected client",
              slot);
    return;
  }

  // Cleared FIRST, and whether or not there is a body to give up: a player who
  // asked to join mid-round is a spectator with a pending request, and one who
  // then changes their mind must not be spawned by the next round boundary.
  const bool was_waiting = server_context.clients[slot].wants_to_play &&
                           server_context.clients[slot].player_uid == shared::null_entity_uid;
  server_context.clients[slot].wants_to_play = false;

  if (server_context.clients[slot].player_uid == shared::null_entity_uid)
  {
    if (was_waiting)
      log_terminal("spectate: slot {} cancelled its pending join", slot);
    else
      log_terminal("spectate: slot {} is already spectating — ignoring", slot);
    return;
  }

  return_client_to_spectate(server_context, slot);
  log_terminal("spectate: slot {} left the match", slot);
}

void spawn_cube(const command_context_t &command_context)
{
  using namespace server;

  server_context_t &server_context = g_server_context;

  if (!server_context.world.physics)
  {
    log_error("spawn_cube: physics state not initialized");
    return;
  }
  auto drop_position =
      get_position_in_front_of(server_context, command_context.caller_slot);
  if (!drop_position)
  {
    log_error("spawn_cube: no Player_Entity for caller_slot {}",
              command_context.caller_slot);
    return;
  }

  vec3f full_extents = {16.f, 16.f, 16.f};
  const shared::entity_uid_t body_uid =
      spawn_physics_body(server_context, entities::Shape_Kind::Box,
                         full_extents, *drop_position);
  if (body_uid != shared::null_entity_uid)
    log_terminal("spawn_cube: spawned entity_id {} at ({:.1f}, {:.1f}, {:.1f})",
                 body_uid, drop_position->x, drop_position->y,
                 drop_position->z);
}

void setpos(float x, float y, float z, const command_context_t &command_context)
{
  using namespace server;
  server_context_t &server_context = g_server_context;

  entities::Player_Entity *player = nullptr;
  if (is_valid_client_slot(command_context.caller_slot))
    player = server_context.world.session.entity_system.get<entities::Player_Entity>(
        server_context.clients[command_context.caller_slot].player_uid);
  if (!player)
  {
    log_error("setpos: no Player_Entity for caller_slot {}", command_context.caller_slot);
    return;
  }

  player->position = {x, y, z};
  player->velocity = {};
}

void spawn_sphere(const command_context_t &command_context)
{
  using namespace server;

  server_context_t &server_context = g_server_context;

  if (!server_context.world.physics)
  {
    log_error("spawn_sphere: physics state not initialized");
    return;
  }
  auto drop_position =
      get_position_in_front_of(server_context, command_context.caller_slot);
  if (!drop_position)
  {
    log_error("spawn_sphere: no Player_Entity for caller_slot {}",
              command_context.caller_slot);
    return;
  }

  vec3f full_extents = {16.f, 16.f, 16.f}; // x = diameter
  const shared::entity_uid_t body_uid =
      spawn_physics_body(server_context, entities::Shape_Kind::Sphere,
                         full_extents, *drop_position);
  if (body_uid != shared::null_entity_uid)
    log_terminal("spawn_sphere: spawned entity_id {} at ({:.1f}, {:.1f}, {:.1f})",
                 body_uid, drop_position->x, drop_position->y,
                 drop_position->z);
}

void map(std::string_view requested_name, const command_context_t &)
{
  using namespace server;

  // Resolved here so a typo is refused in the reply the author is looking at,
  // rather than a tick later in the log.
  const std::optional<std::string> map_path =
      try_resolve_map_path(std::string(requested_name));
  if (!map_path)
  {
    log_error("map: '{}' not found (also tried 'maps/{}' and 'maps/{}.source'). "
              "Not switching.",
              requested_name, requested_name, requested_name);
    return;
  }

  g_server_context.pending_map_change = *map_path;
}


void sv_replay_record(std::string_view name, const command_context_t &)
{
  using namespace server;
  if (g_server_context.world.current_map_path.empty())
  {
    log_error("sv_replay_record: no map is running, so there is nothing to record");
    return;
  }
  (void)start_server_replay_recording(g_server_context, std::string(name));
}

void sv_replay_stop(const command_context_t &)
{
  using namespace server;
  if (!g_server_context.world.replay_recorder.active)
  {
    log_terminal("sv_replay_stop: not recording");
    return;
  }
  shared::finish_replay_recording(g_server_context.world.replay_recorder);
}

void sv_mem_report(int32_t top, const command_context_t &)
{
  memory_audit::report(top <= 0 ? 20u : static_cast<uint32_t>(top));
}

void sv_frame_report(const command_context_t &)
{
  frame_timing::report();
}

void sv_hitch_report(int32_t top, const command_context_t &)
{
  frame_timing::report_worst_frame_zones();
  memory_audit::report_captured_frame(top <= 0 ? 15u : static_cast<uint32_t>(top));
}

// The caller's own body is the activator, which is what lets a console line
// stand in for a trigger. A dedicated server's own console names nobody.
static shared::entity_uid_t caller_body(const server::server_context_t &context,
                                        const command_context_t &command_context)
{
  if (command_context.caller_slot >= 0 &&
      command_context.caller_slot < (int)network::sv_max_client_count)
    return context.clients[command_context.caller_slot].player_uid;
  return shared::null_entity_uid;
}

static void send_to_rules_entity(entities::entity_action action,
                                 const command_context_t &command_context)
{
  using namespace server;

  server_context_t &context = g_server_context;
  entities::Game_Rules_Entity *rules = try_find_rules_entity(context);
  if (rules == nullptr)
  {
    log_error("{}: no map is loaded", entities::to_string(action));
    return;
  }

  entities::action_data_t data;
  data.tag = action;
  input_context_t handler_context{context, caller_body(context, command_context),
                                  context.tick_number};
  entities::send_action(*rules, data, handler_context);
}

void restart_round(const command_context_t &command_context)
{
  send_to_rules_entity(entities::entity_action::Restart_Round, command_context);
}

void end_match(const command_context_t &command_context)
{
  send_to_rules_entity(entities::entity_action::End_Match, command_context);
}

void ready(const command_context_t &command_context)
{
  using namespace server;

  server_context_t &context = g_server_context;
  if (try_find_rules_entity(context) == nullptr)
  {
    log_error("ready: no map is loaded");
    return;
  }

  const entities::Round_Phase phase = match_of(context).phase;
  if (!shared::is_before_match(phase))
  {
    log_warning("ready: refused during {}, the match has started", entities::to_string(phase));
    return;
  }

  entities::Player_Entity *player = context.world.session.entity_system.get<entities::Player_Entity>(
      caller_body(context, command_context));
  if (player == nullptr)
  {
    log_warning("ready: caller_slot {} has no body to vote with", command_context.caller_slot);
    return;
  }

  player->ready = !player->ready;

  const warmup_vote_t vote = count_warmup_vote(context);
  broadcast_server_text_message(
      context, std::format("{} is {} ({} of {} ready)", player->display_name.c_str(),
                           player->ready ? "ready" : "not ready", vote.ready, vote.joined));
}

// fire a specific action on a target.
void ent_fire(uint32_t target, std::string_view action_name, std::string_view parameters,
              const command_context_t &command_context)
{
  using namespace server;

  server_context_t& context = g_server_context;

  entities::Entity* target_entity = context.world.session.entity_system.try_find(target);
  if (target_entity == nullptr)
  {
    log_error("ent_fire: no entity with uid {}", target);
    return;
  }

  const std::optional<entities::entity_action> action =
      entities::try_from_string<entities::entity_action>(action_name);
  if (!action)
  {
    log_error("ent_fire: '{}' is not an action", action_name);
    return;
  }

  entities::action_data_t data;
  data.tag = *action;

  // Reported and then CONTINUED: a refused parameter leaves its field at the
  // default, which is exactly what a map row with a bad override does, and the
  // author asked for the action to be sent.
  for (const std::string &refusal : parse_action_parameters(data, parameters))
    log_error("ent_fire: {}", refusal);

  input_context_t handler_context{context, caller_body(context, command_context),
                                  context.tick_number};

  // SYNCHRONOUS: everything from code is, and the console is code. A connection
  // queues because the action it delivers must not run under the system that
  // emitted the signal; there is no such system here.
  if (!entities::try_send_action(*target_entity, data, handler_context))
  {
    log_error("ent_fire: {} does not accept {}", entity_io_label(context, target),
              entities::to_string(data.tag));
    return;
  }

  log_terminal("ent_fire: sent {} to {}", entities::to_string(data.tag),
               entity_io_label(context, target));
}

} // namespace cvars::commands
