#include "server_messages.hpp"
#include "../shared/frame_timing.hpp"
#include "../shared/memory_audit.hpp"
#include "../shared/player_constants.hpp"
#include "../shared/hitscan.hpp"
#include "../shared/player_animator.hpp"
#include "../shared/player_rig.hpp"
#include "../shared/weapons.hpp"
#include "../shared/round_phase_rules.hpp"
#include "../shared/collision_detection.hpp"
#include "damage.hpp"
#include "../shared/entities/entity_reflection.hpp"
#include "entity_lifecycle.hpp"
#include "server_api.hpp"
#include "systems/ping_system.hpp"
#include "systems/timer_system.hpp"
#include "systems/mover_system.hpp"
#include "systems/trigger_system.hpp"
#include "systems/bot_system.hpp"
#include "systems/game_rules_system.hpp"
#include "systems/physics_body_system.hpp"
#include "systems/inventory_system.hpp"
#include "systems/respawn_system.hpp"
#include "systems/rocket_system.hpp"
#include "systems/bubble_system.hpp"
#include "send_protobuf_message.hpp"
#include "weapon_fire.hpp"
#include "entity_io_console.hpp"
#include "entity_io_queue.hpp"
#include "../shared/hitscan.hpp"
#include "../shared/weapons.hpp"
#include "../shared/array.hpp"
#include "../shared/network/subtick_codec.hpp"
#include "../shared/network/packet.hpp"
#include "../shared/subtick.hpp"
#include "../shared/disabled_geometry.hpp"
#include "../shared/movement_volumes.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include "game_session.hpp"
#include "cvars/cvar_console.hpp"
#include "debug_collision.hpp"
#include "log.hpp"
#include "network/bitstream.hpp"
#include "network/cvar_mirror.hpp"
#include "network/map_transfer.hpp"
#include "network/entity_serialization.hpp"
#include "network/entity_snapshot.hpp"
#include "network/quantization.hpp"
#include "network/server_transport_layer.hpp"
#include "network/snapshot_history.hpp"

#include "move_budget.hpp"
#include "server_context.hpp"
#include "timed_function.hpp"
#include "map.hpp"
#include "player_move.hpp"

#include <fstream>

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

static void send_message_to_reject_incoming_connection(
  server_context_t &context,
  const network::Address& sender,
  std::string_view reason,
  uint32_t server_schema_hash)
{
  game::S2C_Connection reply;
  auto *reject = reply.mutable_reject();
  reject->set_reason(std::string(reason));
  reject->set_server_schema_hash(server_schema_hash);

  ::send_protobuf_message(context, sender, reply);
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

static void drop_timed_out_clients(server_context_t &context)
{
  const float timeout_seconds = context.cvars->sv_timeout;

  // there's no timeout.
  if (timeout_seconds <= 0.0f) return;

  const uint32_t timeout_in_ticks = std::max(
      1u, static_cast<uint32_t>(timeout_seconds * context.cvars->sv_tickrate));

  for (connected_client_t row : connected_clients(context))
  {
    const uint32_t silent_ticks =
        context.tick_number - row.transport.latest_packet_tick;
    if (silent_ticks < timeout_in_ticks)
      continue;

    log_warning("Slot {} ({}) has been silent for {:.1f}s (sv_timeout {:.1f}s)",
                row.slot, row.transport.address.to_string(),
                static_cast<float>(silent_ticks) / context.cvars->sv_tickrate,
                timeout_seconds);
    disconnect_client(context, row.slot, "timed out.");
  }
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

static network::pascal_string_t<32> sanitized_player_name(const std::string& requested_name, int32_t slot)
{
  auto filtered_name = std::string{};
  for (const char character : requested_name)
  {
    if (filtered_name.size() >= 32)
    {
      log_warning("slot {}: player name '{}' exceeds 32 characters — truncated",
                  slot, requested_name);
      break;
    }
    if (static_cast<unsigned char>(character) >= 0x20 &&
        static_cast<unsigned char>(character) != 0x7f)
      filtered_name.push_back(character);
  }

  if (filtered_name.empty())
    filtered_name = std::format("Player {}", slot);

  network::pascal_string_t<32> name;
  name.set(filtered_name.c_str());
  return name;
}

static std::string current_map_wire_id(const server_context_t &context)
{
  return std::filesystem::path(context.world.current_map_path).filename().generic_string();
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

static void send_cvar_values(server_context_t &context, int32_t slot,
                             const shared::cvar_values_message_t &msg)
{
  auto writer  = network::Bit_Writer{};
  shared::serialize_cvar_values(writer, msg);
  network::queue_reliable_message(
      context.transport_layer.clients[slot].reliable_stream,
      static_cast<network::uint8>(network::Message_Type::S2C_CvarValues),
      writer.buffer);
}

static void broadcast_changed_cvar_values(server_context_t &context)
{
  shared::cvar_values_message_t changed =
      shared::collect_changed_mirrored_cvars(*context.cvars,
                                             context.last_broadcast_cvars);
  if (changed.values.empty())
    return;

  for (connected_client_t row : connected_clients(context))
    send_cvar_values(context, row.slot, changed);

  // just copy the whole struct.
  context.last_broadcast_cvars = *context.cvars;

  for (const shared::cvar_value_t &value : changed.values)
    log_terminal("Mirroring '{}' = {} to connected clients",
                 cvars::cvar_info(value.id).name, value.text);
}

static void send_reliable_blocks_if_there_are_any(server_context_t &context)
{
  for (connected_client_t row : connected_clients(context))
  {
    network::Reliable_Stream& stream = row.transport.reliable_stream;

    if (network::reliable_outbound_has_overflowed(stream))
    {
      log_error("slot {} has {} bytes of unconfirmed reliable data (cap {}); it "
                "has stopped acking while we kept queueing",
                row.slot, network::reliable_pending_bytes(stream),
                network::RELIABLE_OUTBOUND_CAP_IN_BYTES);
      disconnect_client(context, row.slot, "overflowed its reliable stream.");
      continue;
    }

    if (context.cvars->sv_reliable_debug && stream.block_length == 0 &&
        network::reliable_pending_bytes(stream) != 0)
    {
      network::visit_pending_reliable_records(
          stream, [&](network::uint8 message_type, network::uint32 length,
                      size_t offset) {
            log_terminal("[reliable] slot {}: record type {} at +{} ({} bytes)",
                         row.slot, static_cast<int>(message_type), offset, length);
          });
    }

    network::send_reliable_block(context.transport_layer, context.socket, row.slot);
  }
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

struct target_shape_t
{
  uint32_t volume_count = 0;
  bool     has_pose = false; // so we know if we have to reconstruct.
};

static target_shape_t target_shape_of(entities::entity_type type,
                                      const shared::player_rig_t &rig)
{
  switch (type)
  {
  case entities::entity_type::Player_Entity:     return {rig.volume_count(), true};
  case entities::entity_type::Damageable_Entity: return {1, false};

  case entities::entity_type::Invalid:
  case entities::entity_type::Player_Spawn_Entity:
  case entities::entity_type::Player_Spectate_Entity:
  case entities::entity_type::Weapon_Entity:
  case entities::entity_type::Rocket_Entity:
  case entities::entity_type::Bubble_Entity:
  case entities::entity_type::Physics_Body_Entity:
  case entities::entity_type::Particle_Emitter_Entity:
  case entities::entity_type::Sound_Emitter_Entity:
  case entities::entity_type::Point_Light_Entity:
  case entities::entity_type::Spot_Light_Entity:
  case entities::entity_type::Directional_Light_Entity:
  case entities::entity_type::Trigger_Volume_Entity:
  case entities::entity_type::Jump_Pad_Entity:
  case entities::entity_type::Reflection_Volume_Entity:
  case entities::entity_type::Game_Rules_Entity:
  case entities::entity_type::Logic_Counter_Entity:
  case entities::entity_type::Geometry_Owner_Entity:
  case entities::entity_type::Ping_Marker_Entity:
  case entities::entity_type::Logic_Timer_Entity:
  case entities::entity_type::Path_Node_Entity:
  case entities::entity_type::Mover_Entity:
    break;
  }

  fatal_error("target_shape_of: {} is Mortal and has no hit volumes; add an arm",
              entities::entity_info(type).classname);
}

// Write one target's volumes into `slice`, and its pose if it has one.
static void build_target_volumes(const entities::Entity &entity,
  const shared::player_rig_t &rig,
  const aim_settings_t &settings, 
  const Span<assets::posed_hitbox_t> posed_hitboxes, 
  std::vector<shared::player_pose_t> &poses)
{
  if (const entities::Player_Entity* player = entities::entity_as<entities::Player_Entity>(&entity))
  {
    const shared::player_pose_t pose{.feet_position = player->position,
                                     .body_yaw      = player->body_yaw,
                                     .view_yaw      = player->view_angle_yaw,
                                     .view_pitch    = player->view_angle_pitch};

    shared::compute_player_hitboxes(rig, pose, settings, posed_hitboxes);
    poses.push_back(pose);
    return;
  }

  if (const entities::Damageable_Entity* damageable =
          entities::entity_as<entities::Damageable_Entity>(&entity))
  {
    // this is actually malformed because orientation is just blatantly ignored.
    posed_hitboxes[0] = assets::make_box_hit_volume(damageable->position + damageable->volume.position,
                                           damageable->volume.half_extents,
                                           shared::hit_region_t::Torso);
    return;
  }

  fatal_error("build_target_volumes: {} is Mortal and target_shape_of gave it volumes, but "
              "nothing here builds them",
              entities::entity_info(entity.type).classname);
}


// Once per tick, however many inputs each client sent: mover_def.md ss12.
static void push_players_by_movers(server_context_t &context,
                                   Span<const uint8_t> disabled_geometry,
                                   Span<const shared::mover_t> movers)
{
  if (movers.empty())
    return;

  for (entities::Player_Entity &player :
       context.world.session.entity_system.entities_of<entities::Player_Entity>())
  {
    const mover_push_t push =
        push_player_by_movers(context.world.session.bvh, disabled_geometry, movers,
                              player.movement, player.position, shared::player_half_width,
                              shared::player_half_height);
    player.position = push.feet;

    if (push.crushed_by != shared::null_entity_uid && player.health.current_health > 0)
    {
      damage_info_t crush;
      crush.victim_uid   = player.entity_id;
      crush.attacker_uid = push.crushed_by;
      crush.amount       = (float)player.health.current_health;
      inflict_damage(context, crush);
    }
  }
}

static void pose_all_targets(server_context_t &context)
{
  shared::posed_players_t &posed = context.posed_players;
  posed.targets.clear();
  posed.poses.clear();
  posed.built_for_tick = context.tick_number;

  const shared::player_rig_t &rig = shared::player_rig();
  const aim_settings_t settings   = aim_settings_from(*context.cvars);

  shared::Entity_System &system = context.world.session.entity_system;

  size_t total_volume_count = 0;
  size_t total_target_count = 0;
  size_t posed_target_count = 0;

  // query the trait, not the component.
  for (auto [entity, health] : system.entities_with_trait<entities::Mortal>())
  {
    // alraedy dead?
    if (health.current_health <= 0) continue;

    const target_shape_t shape = target_shape_of(entity.type, rig);
    total_volume_count += shape.volume_count;
    total_target_count += 1;
    posed_target_count += shape.has_pose ? 1 : 0;
  }

  posed.volumes.resize(total_volume_count);
  posed.targets.reserve(total_target_count);
  posed.poses.reserve(posed_target_count);

  // TWO passes, posed targets first, because `poses` describes a PREFIX of
  // `targets` -- send_shot_debug walks min(targets, poses) and
  // append_static_targets keys off poses.size() to tell a rewindable target from
  // a static one. Splitting the passes is what makes that prefix true BY
  // CONSTRUCTION; one pass got it right only because Player_Entity happens to be
  // declared before Damageable_Entity, which is not something to rest an
  // invariant on.
  size_t next_volume = 0;

  for (const bool should_be_posed : {true, false})
  {
    for (auto [entity, health] : system.entities_with_trait<entities::Mortal>())
    {
      if (health.current_health <= 0)
        continue;

      const target_shape_t shape = target_shape_of(entity.type, rig);
      if (shape.has_pose != should_be_posed)
        continue;

      const Span<assets::posed_hitbox_t> slice{posed.volumes.data() + next_volume, shape.volume_count};
      next_volume += shape.volume_count;

      build_target_volumes(entity, rig, settings, slice, posed.poses);
      posed.targets.push_back(shared::make_hitscan_target(
          entity.entity_id, Span<const assets::posed_hitbox_t>{slice}));
    }
  }
}

static void check_if_there_is_a_pending_map_change(server_context_t &context)
{
  if (context.pending_map_change.empty())
    return;

  const std::string requested_map = std::move(context.pending_map_change);
  context.pending_map_change.clear();
  change_map_to(requested_map);
}

// actual entrypoint.
bool Tick()
{
  timed_function();

  server_context_t& context = g_server_context;

  // if there is a pending map change, that's leading. set everything up.
  check_if_there_is_a_pending_map_change(context);
  
  clear_incoming(context);
  network::Server_Inbox& inbox = context.incoming;

  network::poll_network(context.transport_layer, context.socket,
                        network::server_receive_drain_cap_in_datagrams,
                        context.tick_number, inbox);

  // if we lost someone, no use processing them.
  drop_timed_out_clients(context);

  // Handle connection messages (connect / disconnect)
  for (const auto& [sender, cmd] : inbox.connection_messages)
  {
    if (cmd.has_connect())
    {
      // duplicate connect, no meaningful work to do.
      if (network::try_find_client_slot(context.transport_layer, sender))
      {
        log_warning("duplicate connect received from sender: <not sure if i should log ip addresses.>");
        continue;
      }

      // are we talking the same version of the game?
      const uint32_t client_schema_hash = cmd.connect().schema_hash();
      if (client_schema_hash != entities::SCHEMA_HASH)
      {
        log_error("Refusing connection from {}: schema hash mismatch "
                  "(client {:#010x}, server {:#010x}). Both sides must be "
                  "built from the same entities.def and asset set.",
                  cmd.connect().player_name(), client_schema_hash,
                  entities::SCHEMA_HASH);
        send_message_to_reject_incoming_connection(context, sender,
                    std::format("Schema mismatch: client {:#010x}, server "
                                "{:#010x} -- rebuild against the same "
                                "entities.def",
                                client_schema_hash, entities::SCHEMA_HASH),
                    entities::SCHEMA_HASH);
        continue;
      }

      // find a free slot. if there's a free slot, connect them.
      int32_t slot = invalid_slot_idx;
      for (int32_t candidate = 0; candidate < network::sv_max_client_count; ++candidate)
      {
        if (!context.transport_layer.clients[candidate].occupied)
        {
          slot = candidate;
          break;
        }
      }

      if (slot != invalid_slot_idx)
      {
        connect_client(context, slot, sender,
                       sanitized_player_name(cmd.connect().player_name(), slot));

        // actually handshake back to the client.
        {
          game::S2C_Connection reply;
          auto *accept = reply.mutable_accept();
          accept->set_client_slot(slot);
          accept->set_map_name(context.world.session.map_name.empty()
                                  ? "start.map"
                                  : context.world.session.map_name);
          accept->set_server_tickrate(
              static_cast<int>(context.cvars->sv_tickrate));
          accept->set_map_path(current_map_wire_id(context));
          accept->set_content_hash(context.world.map_content_hash);

          ::send_protobuf_message(context, sender, reply);
        }

        send_cvar_values(context, slot,
                         shared::collect_mirrored_cvars(*context.cvars));

        // Announce join to all clients (including the new one)
        broadcast_server_text_message(
            context, std::format("{} joined the server (slot {})",
                                 cmd.connect().player_name(), slot));
      }
      else
      {
        send_message_to_reject_incoming_connection(context, sender, "Server is Full. please try again later.", 0);
      }
    }
    else if (cmd.has_disconnect())
    {
      process_client_leave_message(context, sender);
    }
  }

  // dispatch developer console entries from clients
  for (const auto& [client_slot, line] : inbox.developer_console_entries)
  {
    log_terminal("Command from slot {}: {}", client_slot, line);
    const network::Address &client_address =
        context.transport_layer.clients[client_slot].address;

    cvars::command_context_t command_context{.caller_slot = client_slot};
    auto reply = std::string{};
    cvars::console_result_t result = cvars::execute_console_line(
        *context.cvars, *context.commands, line, command_context, &reply);

    if (result == cvars::console_result_t::unknown_name)
      log_terminal("Unknown command from slot {}: {}", client_slot, line);

    // echo something back, if it succeeded or not.
    send_text_message_to_a_specific_client(
        context, client_address, reply.empty() ? ("OK: " + line) : reply);
  }


  // clients want a map: set up a paced transfer.
  for (const auto &[client_slot, payload] : inbox.map_data_requests)
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    shared::request_map_data_message_t request =
        shared::deserialize_request_map_data(reader);

    shared::map_package_t package =
        shared::build_map_package(context.world.current_map);
    std::vector<network::uint8> blob = shared::serialize_map_package(package);

    shared::map_data_message_t msg;
    msg.map_name     = context.world.session.map_name;
    msg.package_hash = shared::compute_map_package_hash(blob);
    msg.compressed   = false; 
    msg.bytes        = std::move(blob);

    auto writer = network::Bit_Writer{};
    shared::serialize_map_data(writer, msg);

    network::begin_paced_transfer(
        context.transport_layer, client_slot, writer.buffer,
        static_cast<network::uint8>(network::Message_Type::S2C_MapData));

    log_terminal("Queued map package '{}' ({} bytes, {} fragments, hash {:#x}) "
                 "for slot {} (requested '{}').",
                 msg.map_name, msg.bytes.size(),
                 context.transport_layer.clients[client_slot].outbound_transfer.fragments.size(),
                 msg.package_hash, client_slot, request.map_name);
  }

  // send a block of map fragments so not to swamp the connection.
  network::service_paced_transfers(
      context.transport_layer, context.socket,
      static_cast<size_t>(std::max(1, context.cvars->sv_map_transfer_fragments_per_tick)));


  // this used to sort by timestamp which was broken regardless.
  // now ordered monotonically by command number so that commands in the same tick
  // will at least be processed later. :~)
  std::sort(inbox.client_inputs.begin(), inbox.client_inputs.end(),
    [](const auto& a, const auto& b)
    {
      if (a.first != b.first) return a.first < b.first;

      return a.second.input_number() < b.second.input_number();
    });

  // update (on the servers internal data structure)
  // each client's held snapshot, based on the held_snapshot tick from the move,
  // which (in theory?) should be the latest snapshot.
  for (size_t index = 0; index < inbox.client_inputs.size(); ++index)
  {
    const auto& [client_slot, input] = inbox.client_inputs[index];
    if (!is_valid_client_slot(client_slot))
      continue; // the input loop below logs it; one complaint per input is enough

    client_slot_t& client = context.clients[client_slot];
    client.held_snapshot_tick = std::max(client.held_snapshot_tick, input.held_snapshot_tick());

    // does this mean that it's a redundant input?
    const bool this_is_the_slots_newest_input =
        index + 1 == inbox.client_inputs.size() ||
        inbox.client_inputs[index + 1].first != client_slot;

    if (!this_is_the_slots_newest_input) continue;

    const bool map_ready_now =
        input.map_content_hash() == context.world.map_content_hash;

    if (map_ready_now != client.map_ready)
      log_terminal("Slot {} {} map '{}' (hash {:#x}); {} snapshots.", client_slot,
                   map_ready_now ? "now holds" : "no longer holds",
                   context.world.session.map_name, context.world.map_content_hash,
                   map_ready_now ? "resuming" : "withholding");

    client.map_ready = map_ready_now;
  }

  // gate the amouint of moves that clients can execute in a single tick.
  // this is just for sanity.
  for (connected_client_t row : connected_clients(context))
    row.client.move_credits = grant_move_credit(
        row.client.move_credits, context.cvars->sv_max_move_backlog);


  auto movement_volumes = std::vector<shared::movement_volume_t>{};
  shared::collect_movement_volumes(
      context.world.session.entity_system,
      {.tick                  = context.tick_number,
       .tick_interval_seconds = static_cast<float>(get_tick_interval()),
       .gravity               = context.cvars->g_gravity},
      movement_volumes);
  const Span<const shared::movement_volume_t> movement_volume_span{movement_volumes};

  shared::disabled_geometry_t disabled_geometry;
  shared::collect_disabled_geometry(context.world.session.entity_system,
                                    context.world.session.owner_of, disabled_geometry);
  const Span<const uint8_t> disabled_geometry_span{disabled_geometry};

  std::vector<shared::mover_t> movers;
  shared::collect_movers(context.world.session.entity_system, context.world.session.path_links,
                         context.world.session.mover_rests, context.tick_number,
                         context.cvars->sv_tickrate, movers);
  const Span<const shared::mover_t> mover_span{movers};
  push_players_by_movers(context, disabled_geometry_span, mover_span);

  // After the cut: collect_movers read T-1 and T from the follow as it stood, so a
  // segment boundary costs a rider no travel (mover_def.md ss13).
  advance_movers(context);

  // since the server is in lockstep, pose all players once before handling moves:
  // internalize:
  // posing after the move would test a world no client has ever been shown,
  // and would make the fallback arm disagree with the rewind arm by one tick.
  pose_all_targets(context);

  // actually move players.
  for (const auto &[client_slot, input] : inbox.client_inputs)
  {
    if (!is_valid_client_slot(client_slot))
    {
      log_error("Tick: a move arrived tagged with slot {}, which is out of range "
                "— dropped",
                client_slot);
      continue;
    }

    client_slot_t& client = context.clients[client_slot];

    // valid during this tick.
    entities::Player_Entity* player =
        context.world.session.entity_system.get<entities::Player_Entity>(
            client.player_uid);

    // latest processed is the high watermark so anything earlier can be discarded.
    if (input.input_number() <= client.latest_processed_input_number)
      continue;

    // A spectator has no body to move, but the command was still received and
    // consumed: the pass above drained its held_snapshot_tick, which is the only
    // part of a move that applies to a client with no body. So ACK it. The mark
    // means "I have processed through N", not "I moved you", and leaving it
    // parked was invisible until the client started resending unacked inputs --
    // at which point a spectator resent the same ones forever.
    //
    // No credit is spent. Nothing moved, so there is no rate to bound.
    const bool spectating = !player;
    if (spectating)
    {
      client.latest_processed_input_number = input.input_number();
      continue;
    }

    // filtering so we don't process input people that could probably not move.
    const bool is_dead = player->health.current_health <= 0;

    if (!try_spend_move_credit(client.move_credits))
    {
      const uint32_t warning_interval =
          static_cast<uint32_t>(std::max(1.f, context.cvars->sv_tickrate));
      if (context.tick_number - client.last_move_throttle_warning_tick >=
          warning_interval)
      {
        log_warning("slot {} is over its move budget ({} banked max) — dropping "
                    "moves. A stall this long costs the client the input it "
                    "cannot catch up on; a client that never stops being over "
                    "budget is sending faster than the server ticks",
                    client_slot, context.cvars->sv_max_move_backlog);
        client.last_move_throttle_warning_tick = context.tick_number;
      }
      continue;
    }

    client.latest_processed_input_number = input.input_number();

    const std::optional<shared::subtick_input_t> decoded_input =
        network::try_read_subtick_input(input);
    if (!decoded_input)
    {
      log_error("slot {}: input {} carries sub-tick edges that break the grammar "
                "(slots must be 1..{}, strictly ascending, at most {} of them) — "
                "command dropped",
                client_slot, input.input_number(), shared::SUBTICK_SLOT_COUNT - 1,
                shared::MAX_SUBTICK_EDGES);
      continue;
    }
    const shared::subtick_input_t& subtick_input = *decoded_input;

    shared::queue_replay_player_view(
        context.world.replay_recorder,
        shared::replay_player_view_from_input(static_cast<uint8_t>(client_slot),
                                              {.from_tick    = input.interpolated_from_tick(),
                                               .towards_tick = input.interpolated_towards_tick(),
                                               .fraction     = input.interpolation_fraction()},
                                              subtick_input));

    const uint64_t buttons_before_tick = client.latest_buttons_bitmap;
    client.latest_buttons_bitmap = subtick_input.buttons_at_end();

    // allowed to move is the moire logical one because we can pile more conditions on here.
    bool allowed_to_move = !is_dead;

    // where the aim ended up at the end so interpolation is proper.
    const float yaw = subtick_input.view_at_end.yaw;
    const float pitch = subtick_input.view_at_end.pitch;

    float tick_dt = static_cast<float>(get_tick_interval());

    const bool world_is_frozen = !is_movement_allowed(context);
    if (world_is_frozen)
    {
      // zero out velocity so nothing builds up.
      player->velocity = {0.f, 0.f, 0.f};
    }

    const shared::subtick_steps_t steps =
        shared::split_input_per_tick_into_subtick_steps(subtick_input, tick_dt);

    auto move_events = Move_Events{};
    uint64_t buttons_entering_step = buttons_before_tick;

    for (const shared::subtick_step_t& step : steps)
    {
      const uint64_t pressed_in_this_step = step.buttons & ~buttons_entering_step;
      buttons_entering_step = step.buttons;

      const shared::subtick_time_t step_time =
          shared::subtick_time(context.tick_number, step.start_slot);

      const entities::Inventory_Slot slot_before_switch = player->inventory.active_slot;
      if (const std::optional<entities::Inventory_Slot> selected =
              shared::try_slot_selected_by(pressed_in_this_step))
        player->inventory.active_slot = *selected;

      if (player->inventory.active_slot != slot_before_switch)
      {
        cancel_reload(*player);

        const entities::Weapon_Entity* raised =
            try_find_active_weapon(context.world.session, *player);
        const float deploy_seconds =
            raised != nullptr
                ? shared::get_weapon_definition(raised->weapon_id).deploy_duration_seconds
                : 0.f;

        player->inventory.deploy_complete_time =
            shared::subtick_time_after(step_time, deploy_seconds, tick_dt);

        // debug

        {
          broadcast_server_text_message(
            context,
            std::format("Slot {} equipped {} ({})", client_slot,
                          raised != nullptr
                              ? shared::get_weapon_definition(raised->weapon_id).display_name
                              : "an empty hand",
                          to_string(player->inventory.active_slot)));  
        }
        
      }

      // did we press reload?
      if (pressed_in_this_step & Button::Reload)
      {
        const entities::Weapon_Entity *held_entity =
            try_find_active_weapon(context.world.session, *player);
        if (held_entity != nullptr)
        {
          const shared::weapon_definition_t &held =
              shared::get_weapon_definition(held_entity->weapon_id);
          if (held.magazine_size > 0 && !is_reloading(*player) &&
              held_entity->ammo < held.magazine_size)
          {
            player->reload_complete_time = shared::subtick_time_after(
                step_time, held.reload_duration_seconds, tick_dt);
          }
        }
      }

      const bool fire_pressed_in_this_step = (pressed_in_this_step & Button::Fire) != 0;
      const bool secondary_fire_pressed_in_this_step =
          (pressed_in_this_step & Button::Secondary_Fire) != 0;

      const float step_yaw_rad   = linalg::to_radians(step.view.yaw);
      const float step_pitch_rad = linalg::to_radians(step.view.pitch);
      const float cos_yaw        = std::cos(step_yaw_rad);
      const float sin_yaw        = std::sin(step_yaw_rad);
      const float cos_pitch      = std::cos(step_pitch_rad);
      const float sin_pitch      = std::sin(step_pitch_rad);

      vec3 front = {cos_yaw * cos_pitch, sin_pitch, sin_yaw * cos_pitch};
      //@FIXME(SJM): up vector global?
      const vec3 up = vec3{0, 1, 0};
      vec3       right = linalg::cross(front, up);
      const float right_length = linalg::length(right);
      if (right_length > 0.001f)
        right = right * (1.0f / right_length);
      else
      {
        log_warning("arbitrarily deciding that right is {{1, 0, 0}} because the vector length was too small.");
        right = {1, 0, 0};
      }

      // process movement and fire stuff only if the world is not frozen.
      if (!world_is_frozen)
      {
        auto step_events = Move_Events{};

        // canonical move.
        auto [new_pos, new_vel] = player_move(
            *context.cvars,
            allowed_to_move ? move_input_from_buttons(step.buttons) : Move_Input{},
            player->movement,
            context.world.session.bvh,
            disabled_geometry_span,
            movement_volume_span,
            mover_span,
            player->position,
            player->velocity,
            front,
            right,
            aim_sweep_of(step),
            16.f,
            36.f,
            step.dt,
            &step_events);

        player->position = new_pos;
        player->velocity = new_vel;

        // check what movement events happened so we can fire events and track some state.
        move_events.jumped |= step_events.jumped;
        if (step_events.landed &&
            step_events.land_impact_speed > move_events.land_impact_speed)
        {
          move_events.landed            = true;
          move_events.land_impact_speed = step_events.land_impact_speed;
        }
        if (step_events.launched_by_pad)
        {
          move_events.launched_by_pad = true;
          move_events.pad_uid         = step_events.pad_uid;
        }
      }

      // if we tried to throw the weapon: repair the player weapon state.
      if ((pressed_in_this_step & Button::Throw) && !is_dead &&
          try_throw_active_weapon(context, *player,
                                  linalg::direction_from_angles(step.view.yaw, step.view.pitch),
                                  tick_dt))
        cancel_reload(*player);

      // if we tried to ping and we're not frozen.
      if ((pressed_in_this_step & Button::Ping) && !is_dead && allowed_to_move &&
          !world_is_frozen)
        (void)try_place_ping(context, *player,
                             player->position + vec3f{0.f, shared::player_eye_height, 0.f},
                             linalg::direction_from_angles(step.view.yaw, step.view.pitch),
                             disabled_geometry_span);
      // if we tried to fire.
      if (fire_pressed_in_this_step && allowed_to_move && !world_is_frozen)
        resolve_player_shot(context, client_slot, input, disabled_geometry_span, player,
                            step.view.yaw, step.view.pitch,
                            shared::subtick_time(context.tick_number, step.start_slot));

      // The right mouse button never passes through the shot clocks: Zoom is
      // the client's (it arrives as Button::Zoom state), and an impulse is gated
      // by the movement cooldown alone, the same call the client predicts. One
      // that fired is still a shot for the fire mark, or it sounds on nobody's
      // screen.
      if (secondary_fire_pressed_in_this_step && allowed_to_move && !world_is_frozen)
      {
        const entities::Weapon_Entity* held_entity =
            try_find_active_weapon(context.world.session, *player);
        if (held_entity != nullptr &&
            shared::try_apply_self_impulse(
                shared::get_weapon_definition(held_entity->weapon_id),
                shared::fire_trigger_t::Secondary,
                linalg::direction_from_angles(step.view.yaw, step.view.pitch),
                player->movement, player->velocity))
          mark_shot_fired(context, *player, held_entity->weapon_id);
      }
    }

    if (allowed_to_move && !world_is_frozen)
    {
      player->view_angle_yaw = yaw;
      player->view_angle_pitch = pitch;
    }

    // play some sounds.
    if (move_events.jumped)
    {
      shared::Jump fx{};
      fx.origin          = player->position;
      fx.attached_entity = player->entity_id;
      shared::fire_jump(context.outgoing.effects, fx);
    }
    if (move_events.landed && move_events.land_impact_speed >
                                  context.cvars->pm_minimum_land_impact_speed)
    {
      shared::Land fx{};
      fx.origin = player->position;
      fx.scale = move_events.land_impact_speed; // for volume scaling
      fx.attached_entity = player->entity_id;
      shared::fire_land(context.outgoing.effects, fx);
    }

    // launch pad is predicted locally too so it feels good.
    if (move_events.launched_by_pad)
    {
      shared::Jump_Pad_Launch fx{};
      fx.origin = shared::movement_volume_origin(movement_volume_span, move_events.pad_uid,
                                                 player->position);
      fx.normal          = linalg::normalize(player->velocity);
      fx.attached_entity = player->entity_id;
      shared::fire_jump_pad_launch(context.outgoing.effects, fx);
      pop_bubble(context, move_events.pad_uid);
    }

    // jolt nonsense. 
    if (!world_is_frozen)
      set_kinematic_pose(
        *context.world.physics,
        player->entity_id,
        player->position + vec3f{0.f, shared::player_capsule_center_offset, 0.f},player->velocity);
  }

  // Before the damage pass, so a victim's knockback lands where the swap put them.
  for (const pending_swap_t& swap : context.outgoing.pending_swaps)
  {
    entities::Player_Entity* shooter =
        context.world.session.entity_system.get<entities::Player_Entity>(swap.shooter_uid);
    entities::Player_Entity* target =
        context.world.session.entity_system.get<entities::Player_Entity>(swap.target_uid);
    if (shooter == nullptr || target == nullptr)
    {
      log_error("swap between uid {} and uid {} dropped: one of them is no longer a player",
                swap.shooter_uid, swap.target_uid);
      continue;
    }

    if (target->health.current_health <= 0)
      continue;

    std::swap(shooter->position, target->position);
    std::swap(shooter->velocity, target->velocity);

    for (entities::Player_Entity* swapped : {shooter, target})
      set_kinematic_pose(*context.world.physics, swapped->entity_id,
                         swapped->position + vec3f{0.f, shared::player_capsule_center_offset, 0.f},
                         swapped->velocity);
  }
  context.outgoing.pending_swaps.clear();


  // resolve all the pending hits and damage events.
  for (const pending_hit_t &pending : context.outgoing.pending_hits)
  {
    auto impact_fx = shared::Shot_Impact{};
    impact_fx.origin          = pending.impact_point;
    impact_fx.normal          = pending.impact_normal;
    impact_fx.attached_entity = pending.info.victim_uid;
    impact_fx.region          = static_cast<uint16_t>(pending.region);
    impact_fx.weapon          = pending.info.weapon_id;
    shared::fire_shot_impact(context.outgoing.effects, impact_fx);

    // The hitmarker, for the shooter only, as replicated state. Their own client
    // plays it off this stamp advancing -- see Player_Entity::last_hit_tick in
    // entities.def for why it is not an effect. Every contributor gets one, not
    // just the one credited with the kill: you hit them, so you saw it land.
    //
    // Gated on the same query the health write is: a hitmarker is a claim that
    // damage landed, so outside the round it would be feedback for a hit that
    // did nothing. The Shot_Impact above is NOT gated -- it says where the
    // bullet went, which is true either way.
    entities::Player_Entity* attacker =
        can_take_damage(context)
            ? context.world.session.entity_system.get<entities::Player_Entity>(
                  pending.info.attacker_uid)
            : nullptr;
    if (attacker)
    {
      attacker->last_hit_tick         = context.tick_number;
      attacker->last_hit_was_headshot = pending.info.was_headshot;
    }
  }

  inflict_damage_batch(context, context.outgoing.pending_hits);
  context.outgoing.pending_hits.clear();

  const shared::subtick_time_t end_of_tick =
      shared::subtick_time(context.tick_number + 1, 0);
  for (entities::Player_Entity& player :
       context.world.session.entity_system.entities_of<entities::Player_Entity>())
  {
    if (is_reloading(player) && player.reload_complete_time <= end_of_tick)
      finish_reload(context.world.session, player);
  }

  // --- Simulate server-side entities ---
  float tick_dt = static_cast<float>(get_tick_interval());
  if (!context.world.physics)
  {
    log_error("Server tick with no physics state — init() must have failed");
    return false;
  }


  // bots need an update because they are a complex system, I guess.
  update_bots(context, movement_volume_span, disabled_geometry_span, mover_span,
              context.tick_number, tick_dt);

  //@NOTE(SJM): why does this happen? repoint the orientation?
  {
    const aim_settings_t settings = aim_settings_from(*context.cvars);
    for (entities::Player_Entity& player :
         context.world.session.entity_system.entities_of<entities::Player_Entity>())
    {
      if (player.health.current_health <= 0) continue;
      advance_body_yaw(player.body_yaw, player.view_angle_yaw, tick_dt, settings);
    }
  }

  update_rockets(context, tick_dt);
  update_bubbles(context, disabled_geometry_span);
  update_ping_markers(context, tick_dt);

  // respawn runs after death so we can correctly set next ticks etc.
  update_respawns(context, context.tick_number,
                  static_cast<uint32_t>(context.cvars->sv_tickrate),
                  context.cvars->map_respawn_delay_seconds);

  // simulation over, just do the rule stuff.
  // ----- rule stuff.


  update_match(context, context.tick_number, static_cast<uint32_t>(context.cvars->sv_tickrate));


  step_physics(*context.world.physics, tick_dt);
  update_physics_bodies(context.world.session, *context.world.physics);
  update_dropped_weapons(context);


  update_triggers(context);
  update_timers(context);

  // Before the drain, so the tick a goal volume fires Complete_Level has its pose.
  {
    const entities::Match& match = match_of(context);
    if (context.cvars->sv_ghost_record && match.phase == entities::Round_Phase::Live &&
        current_mode(context).win_condition == Win_Condition::Objective_Reached)
      shared::capture_ghost_poses(
          context.world.ghost_capture, match.phase_start_tick, context.tick_number,
          context.world.session.entity_system.entities_of<entities::Player_Entity>());
  }

  // Every connection this tick emitted, delivered before the snapshot is built,
  // looping until the chain settles. Here rather than at the top of the tick so
  // a zero-delay chain completes inside the tick that started it: the door a
  // button opened is open in the snapshot of the tick it was pressed in, not
  // one hop per tick later. Handlers still never run under an emitting system
  // -- the reentrancy guard is the hop, not the tick (entity_io_queue.hpp).
  drain_pending_entity_actions(context);
  latch_mover_switches(context);

  // debug
  {
    if (!context.world.bots.empty())
    {
      game::S2C_BotDebug bot_debug_message;
      for (const auto &bot : context.world.bots)
      {
        auto *entry = bot_debug_message.add_bots();
        entry->set_slot(bot.player_slot);
        entry->set_goal(static_cast<int>(bot.goal));
        entry->set_type(static_cast<int>(bot.type));
        entry->set_path_index(bot.path_index);
        for (const auto &wp : bot.path)
        {
          auto *v = entry->add_path();
          v->set_x(wp.x);
          v->set_y(wp.y);
          v->set_z(wp.z);
        }
      }

      for (connected_client_t row : connected_clients(context))
      {
        ::send_protobuf_message(context, row.transport.address, bot_debug_message);
      }
    }
  
  }


  network::snapshot_frame_t& frame = context.replication.snapshot_history.slot_for(context.tick_number);
  frame.clear();
  frame.tick = context.tick_number;

  frame.copy_replicated_entities_from(context.world.session.entity_system);

  // Serialize and send to each client with per-client delta compression
  for (connected_client_t row : connected_clients(context))
  {
    const int32_t slot = row.slot;

    // if the client doesn't have the map ready, they don't need deltas or full updates.
    if (!row.client.map_ready) continue;

    auto writer = network::Bit_Writer{};

    // what's the diff against the snapshot the client holds?
    const network::snapshot_frame_t* baseline =
        context.replication.snapshot_history.find(context.clients[slot].held_snapshot_tick);

    network::serialize_snapshot(writer, frame, baseline);

    // create and send package
    game::S2C_EntityPackage package;
    package.set_server_tick(context.tick_number);
    package.set_latest_processed_input_number(context.clients[slot].latest_processed_input_number);
    network::set_snapshot_baseline(package, baseline);

    package.set_entity_data(writer.buffer.data(), writer.buffer.size());
    ::send_protobuf_message(context, row.transport.address, package);
  }

  // send the effects in a batch.
  std::vector<network::uint8> effect_batch_bytes;
  if (!context.outgoing.effects.empty())
  {
    //@NOTE(SJM): why is this finish necessary?
    context.outgoing.effects.finish();

    game::S2C_EffectBatch batch;
    batch.set_effect_data(context.outgoing.effects.writer.buffer.data(),
                          context.outgoing.effects.writer.buffer.size());
    batch.set_server_tick(context.tick_number);

    for (connected_client_t row : connected_clients(context))
    {
      if (!row.client.map_ready) continue;
      ::send_protobuf_message(context, row.transport.address, batch);
    }

    effect_batch_bytes.resize(batch.ByteSizeLong());
    batch.SerializeToArray(effect_batch_bytes.data(), static_cast<int>(effect_batch_bytes.size()));
  }

  // send gameplay events in a batch. note that this is _reliable_ transfer
  // because events cause gameplay state to change. cosmetic events nobody 
  // cares about. entity updates need neither: each is a delta against a tick the client says it holds.
  std::vector<network::uint8> event_batch_bytes;
  if (!context.outgoing.events.empty())
  {
  
    context.outgoing.events.finish();

    game::S2C_GameEventBatch batch;
    batch.set_event_data(context.outgoing.events.writer.buffer.data(),
                         context.outgoing.events.writer.buffer.size());
    batch.set_server_tick(context.tick_number);

    event_batch_bytes.resize(batch.ByteSizeLong());
    batch.SerializeToArray(event_batch_bytes.data(),
                           static_cast<int>(event_batch_bytes.size()));

    for (connected_client_t row : connected_clients(context))
      network::queue_reliable_message(
          row.transport.reliable_stream,
          static_cast<network::uint8>(network::Message_Type::S2C_GameEventBatch),
          event_batch_bytes);
  }

  shared::record_replay_tick(context.world.replay_recorder, frame,
                             Span<const uint8_t>(effect_batch_bytes),
                             Span<const uint8_t>(event_batch_bytes), *context.cvars);

  // in theory redundant but just so we don't have stale shit to send.
  clear_outgoing(context);

  broadcast_changed_cvar_values(context);

  // if we had reliable data to send, do so.
  send_reliable_blocks_if_there_are_any(context);

  context.tick_number++;
  return true;
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
