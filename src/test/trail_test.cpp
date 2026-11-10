// The trail a player leaves (shared/collision/trail.hpp, server/systems/trail_system.cpp): points laid from the
// owner's feet by the flight path rule, a wall per edge that hurts whoever stands in it but the owner on their
// newest two edges, begun and ended through Mobile's verbs, ended by a death or a full list. Compiles the server
// sources it drives rather than linking game_server, like camera_swap_test.
#include "assets/asset_state.hpp"
#include "entities/generated/entities_generated.hpp"
#include "server/server_context.hpp"
#include "server/systems/game_rules_system.hpp"
#include "server/systems/trail_system.hpp"
#include "shared/aabb.hpp"
#include "shared/collision/trail.hpp"
#include "shared/map/game_session.hpp"
#include "shared/map/map.hpp"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace server
{
uint32_t get_tick_number() { return 0; }
float    get_tick_interval_seconds() { return 1.f / 60.f; }
}

using namespace server;
using linalg::vec3f;

namespace
{

int failure_count = 0;

void check(bool condition, const std::string& what)
{
  std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
  if (!condition)
    ++failure_count;
}

bool near(float a, float b, float tolerance = 1e-3f)
{
  return std::fabs(a - b) <= tolerance;
}

bool near(const vec3f& a, const vec3f& b, float tolerance = 1e-3f)
{
  return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
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

int32_t health(test_world_t& world, shared::entity_uid_t uid)
{
  return player(world, uid).health.current_health;
}

std::vector<entities::Trail_Entity*> trails(test_world_t& world)
{
  std::vector<entities::Trail_Entity*> found;
  for (entities::Trail_Entity& trail :
       world.context.world.session.entity_system.entities_of_type<entities::Trail_Entity>())
    found.push_back(&trail);
  return found;
}

// The one trail `owner` has live.
entities::Trail_Entity* live_trail_of(test_world_t& world, shared::entity_uid_t owner)
{
  for (entities::Trail_Entity* trail : trails(world))
    if (trail->owner_uid == owner)
      return trail;
  return nullptr;
}

const vec3f& last_point(const entities::Trail_Entity& trail)
{
  return trail.points[trail.points.size() - 1];
}

// Two players, both alive and far apart, in a match whose phase lets them move. A wall hurts one point a tick,
// so nobody dies on a long walk and every hit is countable.
void stand_up(test_world_t& world)
{
  world.context.cvars         = &world.cvars;
  world.context.tick_number   = 1000;
  world.cvars.sv_tickrate     = tickrate;
  world.cvars.mp_trail_damage = 1.f;

  shared::map_t map;
  map.name           = "trail_test";
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
    entities::Player_Entity& spawned = *world.context.world.session.entity_system.get<entities::Player_Entity>(uid);
    spawned.health.current_health                        = 100;
    spawned.position                                     = {0.f, 0.f, slot * 5000.f};
    world.context.transport_layer.clients[slot].occupied = true;
    world.context.clients[slot].player_uid               = uid;
    (slot == 0 ? world.a : world.b)                      = uid;
  }
}

void tick(test_world_t& world)
{
  ++world.context.tick_number;
  update_trails(world.context);
}

// Moves `uid` by `step` each tick, with the velocity that movement implies, for `ticks` ticks.
void walk(test_world_t& world, shared::entity_uid_t uid, const vec3f& step, int ticks)
{
  for (int index = 0; index < ticks; ++index)
  {
    entities::Player_Entity& walker = player(world, uid);
    walker.velocity                 = step * static_cast<float>(tickrate);
    walker.position                 = walker.position + step;
    tick(world);
  }
}

// A player who is not trailing can be put anywhere: nothing records their feet.
void stand(test_world_t& world, shared::entity_uid_t uid, const vec3f& feet)
{
  player(world, uid).velocity = {0.f, 0.f, 0.f};
  player(world, uid).position = feet;
}

} // namespace

int main()
{
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::number_asset_ids_from_tree(asset_state, "resources");

  std::printf("[the box test]\n");
  {
    const shared::oriented_box_t axis_box = {.center = {0.f, 0.f, 0.f}, .half_extents = {10.f, 10.f, 10.f}};
    const shared::oriented_box_t turned_apart = {.center       = {20.f, 20.f, 0.f},
                                                 .half_extents = {10.f, 10.f, 10.f},
                                                 .orientation  = linalg::from_axis_angle({0.f, 0.f, 1.f}, 45.f)};
    const shared::oriented_box_t turned_into = {.center       = {15.f, 15.f, 0.f},
                                                .half_extents = {10.f, 10.f, 10.f},
                                                .orientation  = linalg::from_axis_angle({0.f, 0.f, 1.f}, 45.f)};
    check(shared::aabbs_intersect(shared::get_bounds(axis_box), shared::get_bounds(turned_apart)) &&
              !shared::oriented_boxes_intersect(axis_box, turned_apart),
          "a turned box whose bounds overlap but whose body does not is apart");
    check(shared::oriented_boxes_intersect(axis_box, turned_into), "a turned box reaching into the other intersects");
    check(shared::oriented_boxes_intersect(turned_into, axis_box), "...from either side");
    const shared::oriented_box_t beside = {.center = {20.f, 0.f, 0.f}, .half_extents = {10.f, 10.f, 10.f}};
    check(shared::oriented_boxes_intersect(axis_box, beside), "two boxes touching face to face intersect");
  }

  std::printf("[the segment box]\n");
  {
    const shared::trail_settings_t settings = {.half_width = 8.f, .height = 64.f};
    const std::optional<shared::oriented_box_t> box =
        shared::try_compute_trail_segment_box({0.f, 0.f, 0.f}, {100.f, 0.f, 0.f}, settings);
    check(box.has_value() && near(box->center, {50.f, 32.f, 0.f}) && near(box->half_extents, {50.f, 32.f, 8.f}),
          "an edge's box stands on the line, half its height up, as long as the edge");
    check(box.has_value() && near(linalg::forward(box->orientation), {1.f, 0.f, 0.f}),
          "the box's length runs along the edge");
    check(!shared::try_compute_trail_segment_box({0.f, 0.f, 0.f}, {0.5f, 0.f, 0.f}, settings).has_value(),
          "an edge shorter than SHORTEST_TRAIL_EDGE is no wall");
  }

  std::printf("[the trail]\n");
  test_world_t world;
  stand_up(world);

  begin_trail_for_player(world.context, player(world, world.a), world.context.tick_number);
  entities::Trail_Entity* trail = live_trail_of(world, world.a);
  check(trails(world).size() == 1 && trail != nullptr, "Begin_Trail spawns one trail owned by the player");
  check(trail != nullptr && trail->born_tick == world.context.tick_number && trail->ended_tick == 0,
        "...born this tick and not ended");
  check(trail != nullptr && trail->points.size() == 1 && near(trail->points[0], {0.f, 0.f, 0.f}),
        "...with its first point at the owner's feet");
  if (trail == nullptr)
    return 1;

  // --- a straight walk lays a point every longest_segment ---
  walk(world, world.a, {20.f, 0.f, 0.f}, 15);
  check(trail->points.size() == 3 && near(trail->points[1], {140.f, 0.f, 0.f}) &&
            near(trail->points[2], {280.f, 0.f, 0.f}),
        "a 300 unit walk lays a point where the walker first stood 128 past the last, twice");
  check(health(world, world.a) == 100, "the owner on their live edge and newest laid edge is unhurt");

  // --- a turn lays a point at once, the last point being past the shortest segment ---
  walk(world, world.a, {0.f, 0.f, 20.f}, 1);
  check(trail->points.size() == 4 && near(last_point(*trail), {300.f, 0.f, 20.f}),
        "a right-angle turn lays a point on the first step past the shortest segment");

  // --- the wall hurts whoever stands in it ---
  stand(world, world.b, {64.f, 0.f, 0.f});
  tick(world);
  check(health(world, world.b) == 99, "another player standing in an edge takes mp_trail_damage a tick");
  stand(world, world.b, {64.f, 0.f, 40.f});
  tick(world);
  check(health(world, world.b) == 99, "...and beside the wall, past its half width, is unhurt");
  stand(world, world.b, {64.f, 70.f, 0.f});
  tick(world);
  check(health(world, world.b) == 99, "...and above the wall's height is unhurt");
  stand(world, world.b, {0.f, 0.f, 5000.f});

  // --- the owner walks a square: unhurt on fresh legs and at their corners, where the two points of a sharp turn
  // lie 20 apart and the 32 wide hull reaches the edge before them; hurt where the last leg crosses the first edge.
  walk(world, world.a, {0.f, 0.f, 20.f}, 14);
  walk(world, world.a, {-20.f, 0.f, 0.f}, 15);
  check(near(player(world, world.a).position, {0.f, 0.f, 300.f}) && health(world, world.a) == 100,
        "the owner is unhurt along two fresh legs and their corners: the lag distance behind their feet is theirs");
  walk(world, world.a, {0.f, 0.f, -20.f}, 13);
  check(near(player(world, world.a).position, {0.f, 0.f, 40.f}) && health(world, world.a) == 100,
        "...and down the last leg until the first edge is within reach");
  walk(world, world.a, {0.f, 0.f, -20.f}, 1);
  check(health(world, world.a) == 99, "closing the square, the owner is hurt by their own first edge");
  walk(world, world.a, {0.f, 0.f, -20.f}, 2);
  check(health(world, world.a) == 97 && near(last_point(*trail), {0.f, 0.f, -20.f}),
        "...twice more crossing it, and stands at the end of their newest edge");

  // --- End_Trail: the owner is dropped, the trail spares nobody and stops growing ---
  const int32_t health_before_owned_tick = health(world, world.a);
  tick(world);
  const int32_t hits_while_owned = health_before_owned_tick - health(world, world.a);
  end_trails_of_player(world.context, world.a, world.context.tick_number);
  check(trail->owner_uid == shared::null_entity_uid && trail->ended_tick == world.context.tick_number,
        "End_Trail nulls the owner and stamps the end tick");
  const int32_t health_before_unowned_tick = health(world, world.a);
  tick(world);
  check(health_before_unowned_tick - health(world, world.a) > hits_while_owned,
        "a trail with no owner spares nobody: its former owner at its end is hit by its last edge too");
  const uint32_t points_at_end = trail->points.size();
  walk(world, world.a, {-20.f, 0.f, 0.f}, 10);
  check(trail->points.size() == points_at_end, "...and it lays no more points");

  // --- a second Begin ends the first ---
  begin_trail_for_player(world.context, player(world, world.a), world.context.tick_number);
  begin_trail_for_player(world.context, player(world, world.a), world.context.tick_number);
  {
    uint32_t owned = 0;
    for (const entities::Trail_Entity* each : trails(world))
      owned += each->owner_uid == world.a ? 1u : 0u;
    check(trails(world).size() == 3 && owned == 1, "a second Begin_Trail ends the live trail and starts another");
  }

  // --- a death ends the trail ---
  begin_trail_for_player(world.context, player(world, world.b), world.context.tick_number);
  player(world, world.b).health.current_health = 0;
  tick(world);
  check(live_trail_of(world, world.b) == nullptr, "a dead owner's trail ends");
  player(world, world.b).health.current_health = 100;

  // --- a full list ends the trail ---
  {
    entities::Trail_Entity* full = live_trail_of(world, world.a);
    while (full != nullptr && !full->points.full())
      if (!full->points.try_push_back(player(world, world.a).position))
        return 1;
    walk(world, world.a, {20.f, 0.f, 0.f}, 10);
    check(full != nullptr && full->owner_uid == shared::null_entity_uid && full->points.full(),
          "a trail whose points are full ends with them all kept");
  }

  if (failure_count != 0)
  {
    std::printf("FAILED: %d check(s)\n", failure_count);
    return 1;
  }
  std::printf("trail_test passed\n");
  return 0;
}
