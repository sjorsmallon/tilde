// The Camera Swapper (shared/camera_swap.hpp): a steered body's steps run under its hand's mouse, its
// head is written from it, and the pair comes apart on a second shot or a death. Compiles the server
// systems it needs rather than linking game_server, like game_rules_test.
#include "assets/asset_state.hpp"
#include "entities/generated/entities_generated.hpp"
#include "server/server_context.hpp"
#include "server/systems/contact_system.hpp"
#include "server/systems/game_rules_system.hpp"
#include "server/move_budget.hpp"
#include "server/systems/player_input_system.hpp"
#include "shared/camera_swap.hpp"
#include "shared/collision/predicted_world.hpp"
#include "shared/map/game_session.hpp"
#include "shared/map/map.hpp"
#include "shared/network/generated/messages_generated.hpp"
#include "shared/movement/player_move.hpp"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace server
{
uint32_t get_tick_number() { return 0; }
float    get_tick_interval_seconds() { return 1.f / 60.f; }
}

using namespace server;

namespace
{

int failure_count = 0;

void check(bool condition, const std::string& what)
{
  std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
  if (!condition)
    ++failure_count;
}

constexpr uint32_t tickrate = 60;

struct test_world_t
{
  server_context_t     context;
  cvars::cvar_state_t  cvars;
  shared::entity_uid_t a = shared::null_entity_uid;
  shared::entity_uid_t b = shared::null_entity_uid;
};

entities::Player_Entity& player(test_world_t& world, shared::entity_uid_t uid)
{
  return *world.context.world.session.entity_system.get<entities::Player_Entity>(uid);
}

// Two connected slots, each with a live player, in a match whose phase lets them move.
void stand_up(test_world_t& world)
{
  world.context.cvars       = &world.cvars;
  world.context.tick_number = 1000;
  world.cvars.sv_tickrate   = tickrate;

  shared::map_t map;
  map.name           = "camera_swap_test";
  auto marker        = std::make_shared<entities::Player_Spawn_Entity>();
  marker->spawn_type = entities::Spawn_Type::Human;
  map.add_entity(marker);
  auto rules        = std::make_shared<entities::Game_Rules_Entity>();
  rules->match.mode = entities::Game_Mode::deathmatch;
  map.add_entity(rules);

  world.context.world.current_map = map;
  world.context.world.session     = shared::build_session(map);
  install_match(world.context, world.context.tick_number, tickrate, false);

  for (int32_t slot : {0, 1})
  {
    const shared::entity_uid_t uid =
        world.context.world.session.entity_system.spawn<entities::Player_Entity>();
    entities::Player_Entity& spawned =
        *world.context.world.session.entity_system.get<entities::Player_Entity>(uid);
    spawned.health.current_health                        = 100;
    spawned.position                                     = {slot * 500.f, 0.f, 0.f};
    world.context.transport_layer.clients[slot].occupied = true;
    world.context.clients[slot].player_uid               = uid;
    (slot == 0 ? world.a : world.b)                      = uid;
  }
}

// One tick: each slot's input, then the contacts, as the server tick orders them.
void tick(test_world_t& world, float yaw_a, float yaw_b)
{
  ++world.context.tick_number;
  const int32_t input_number = static_cast<int32_t>(world.context.tick_number);
  world.context.incoming.client_inputs.clear();
  for (int32_t slot : {0, 1})
  {
    client_slot_t& client = world.context.clients[slot];
    client.move_credits   = grant_move_credit(client.move_credits, world.cvars.sv_max_move_backlog);

    shared::Client_Input input{};
    input.input_number           = input_number;
    input.input.buttons_at_start = Button::Forward;
    input.input.view_at_start    = {slot == 0 ? yaw_a : yaw_b, 0.f};
    input.input.view_at_end      = input.input.view_at_start;
    world.context.incoming.client_inputs.push_back({slot, input});
  }

  shared::predicted_world_storage_t storage;
  shared::compute_disabled_geometry_per_team(world.context.world.session, storage);
  update_player_inputs(world.context, storage);
  update_contacts(world.context, storage);
}

void shoot_camera_swap(test_world_t& world, shared::entity_uid_t shooter, shared::entity_uid_t target)
{
  pending_contact_t contact;
  contact.shooter_uid = shooter;
  contact.target_uid  = target;
  contact.weapon      = entities::Weapon::Camera_Swapper;
  if (target != shared::null_entity_uid)
    contact.point = player(world, target).position;
  world.context.outgoing.pending_contacts.push_back(contact);
}

bool heads_toward(const linalg::vec3f& change, float yaw_degrees)
{
  const linalg::vec3f forward = linalg::direction_from_angles(yaw_degrees, 0.f);
  return change.x * forward.x + change.z * forward.z > 0.f && std::abs(change.x) + std::abs(change.z) > 1e-3f;
}

} // namespace

int main()
{
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::number_asset_ids_from_tree(asset_state, "resources");

  std::printf("[camera swap]\n");
  test_world_t world;
  stand_up(world);

  // --- unswapped: each head is its own mouse ---
  tick(world, 10.f, 200.f);
  tick(world, 10.f, 200.f);
  check(player(world, world.a).view_angle_yaw == 10.f && player(world, world.b).view_angle_yaw == 200.f,
        "with no swap each body's head is its own mouse");

  // --- the shot swaps both: A's head is B's mouse and A walks along it ---
  shoot_camera_swap(world, world.a, world.b);
  tick(world, 10.f, 200.f);
  check(player(world, world.a).view_steered_by_uid == world.b &&
            player(world, world.b).view_steered_by_uid == world.a,
        "a Camera_Swap contact names each body's hand as the other");
  const linalg::vec3f a_velocity_before = player(world, world.a).velocity;
  tick(world, 10.f, 200.f);
  check(player(world, world.a).view_angle_yaw == 200.f && player(world, world.b).view_angle_yaw == 10.f,
        "swapped, each head is the OTHER's mouse");
  check(heads_toward(player(world, world.a).velocity - a_velocity_before, 200.f),
        "A's forward key accelerates A along B's mouse");

  // --- a second shot by a swapped shooter releases both, whoever it hits ---
  shoot_camera_swap(world, world.a, shared::null_entity_uid);
  tick(world, 10.f, 200.f);
  check(!shared::player_view_is_steered_by_another(player(world, world.a)) &&
            !shared::player_view_is_steered_by_another(player(world, world.b)),
        "a swapped shooter's next shot ends the swap for both");
  tick(world, 10.f, 200.f);
  check(player(world, world.a).view_angle_yaw == 10.f, "...and A's head is A's mouse again");

  // --- a death comes apart on its own ---
  shoot_camera_swap(world, world.b, world.a);
  tick(world, 10.f, 200.f);
  check(player(world, world.a).view_steered_by_uid == world.b, "B's shot swaps the pair again");
  player(world, world.b).health.current_health = 0;
  tick(world, 10.f, 200.f);
  check(!shared::player_view_is_steered_by_another(player(world, world.a)) &&
            !shared::player_view_is_steered_by_another(player(world, world.b)),
        "a dead hand releases both bodies");

  if (failure_count != 0)
  {
    std::printf("FAILED: %d check(s)\n", failure_count);
    return 1;
  }
  std::printf("camera_swap_test passed\n");
  return 0;
}
