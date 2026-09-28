#include "entities/generated/entities/extending_platform_entity_generated.hpp"
#include "entities/generated/entities/guided_rocket_entity_generated.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "entities/generated/entities_tables_generated.hpp"
#include "guided_rocket_system.hpp"

#include "../../shared/flight_path.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"
#include "../../shared/player_constants.hpp"
#include "../../shared/spawned_platforms.hpp"
#include "../../shared/weapons.hpp"
#include "../entity_lifecycle.hpp"

#include <unordered_map>
#include <vector>

namespace server
{

namespace
{

struct ended_flight_t
{
  shared::entity_uid_t  pilot_uid;
  entities::Weapon      weapon_id;
  shared::flight_path_t path;
};

bool player_is_piloting(const entities::Player_Entity& player)
{
  return player.movement.active_override == entities::Movement_Override::Pilot;
}

vec3f heading_of(const entities::Player_Entity& pilot)
{
  return linalg::direction_from_angles(pilot.view_angle_yaw, pilot.view_angle_pitch);
}

void write_rocket_pose(entities::Guided_Rocket_Entity& rocket, const entities::Player_Entity& pilot)
{
  rocket.position    = pilot.movement.override_target_position;
  rocket.orientation = linalg::from_view_angles(pilot.view_angle_yaw, pilot.view_angle_pitch);
}

const shared::pilot_t* try_find_path_laying_pilot_of(entities::Weapon weapon_id)
{
  const shared::pilot_t* pilot = shared::try_find_pilot_of(shared::get_weapon_definition(weapon_id));
  if (pilot == nullptr || pilot->leaves != shared::pilot_leaves_t::Path)
    return nullptr;
  return pilot;
}

// One path per pilot and weapon: the older one goes, then each edge is an extending platform that starts
// growing the tick the one before it is grown, so the path draws itself outward from the pilot.
void solidify_flight_path(server_context_t& context, const ended_flight_t& flight,
                          float tick_interval_seconds)
{
  const shared::pilot_t* pilot = try_find_path_laying_pilot_of(flight.weapon_id);
  if (pilot == nullptr)
    return;

  shared::Entity_System& system = context.world.session.entity_system;

  std::vector<shared::entity_uid_t> replaced;
  for (const entities::Extending_Platform_Entity& platform :
       system.entities_of<entities::Extending_Platform_Entity>())
  {
    if (platform.projectile.owner_uid == flight.pilot_uid &&
        platform.projectile.weapon_id == flight.weapon_id)
      replaced.push_back(platform.entity_id);
  }
  for (const shared::entity_uid_t uid : replaced)
    destroy_entity(context, uid);

  uint32_t spawned_tick = context.tick_number;

  for (const shared::flight_path_segment_t& segment :
       shared::segments_of_flight_path(flight.path.vertices, pilot->path))
  {
    const shared::entity_uid_t uid = system.spawn(entities::entity_type::Extending_Platform_Entity);
    entities::Extending_Platform_Entity* platform = system.get<entities::Extending_Platform_Entity>(uid);
    if (platform == nullptr)
    {
      log_error("solidify_flight_path: no room to spawn a path segment for player {}; the path stops short",
                flight.pilot_uid);
      return;
    }

    platform->position             = segment.start;
    platform->orientation          = segment.orientation;
    platform->length               = segment.length;
    platform->half_width           = segment.half_width;
    platform->spawned_tick         = spawned_tick;
    platform->projectile.owner_uid = flight.pilot_uid;
    platform->projectile.weapon_id = flight.weapon_id;

    spawned_tick += shared::extending_platform_extend_ticks(*platform, tick_interval_seconds);
  }
}

} // namespace

void update_guided_rockets(server_context_t& context, float tick_interval_seconds)
{
  shared::Entity_System& system = context.world.session.entity_system;
  std::unordered_map<shared::entity_uid_t, shared::flight_path_t>& paths =
      context.world.flight_path_by_rocket_uid;

  for (entities::Player_Entity& player : system.entities_of<entities::Player_Entity>())
  {
    if (player_is_piloting(player) && player.health.current_health <= 0)
      (void)shared::try_end_pilot_flight(player.movement);
  }

  std::vector<shared::entity_uid_t> piloted;
  std::vector<shared::entity_uid_t> spent;
  std::vector<ended_flight_t>       ended;

  for (entities::Guided_Rocket_Entity& rocket : system.entities_of<entities::Guided_Rocket_Entity>())
  {
    const entities::Player_Entity* pilot = system.get<entities::Player_Entity>(rocket.pilot_uid);
    const bool pilot_is_alive = pilot != nullptr && pilot->health.current_health > 0;
    const auto path           = paths.find(rocket.entity_id);

    if (pilot_is_alive && player_is_piloting(*pilot))
    {
      write_rocket_pose(rocket, *pilot);
      piloted.push_back(pilot->entity_id);

      const shared::pilot_t* row = try_find_path_laying_pilot_of(rocket.weapon_id);
      if (row != nullptr && path != paths.end())
        shared::record_flight_path(path->second, row->path, rocket.position, heading_of(*pilot));
      continue;
    }

    spent.push_back(rocket.entity_id);

    // A pilot who died, left or was rebuilt leaves nothing. An override that took over since the flight
    // ended has written the target, so the rocket's last pose is the end instead.
    if (!pilot_is_alive || path == paths.end())
      continue;

    const vec3f end = pilot->movement.active_override == entities::Movement_Override::None
                          ? pilot->movement.override_target_position
                          : rocket.position;
    shared::end_flight_path(path->second, end);
    ended.push_back({.pilot_uid = pilot->entity_id,
                     .weapon_id = rocket.weapon_id,
                     .path      = std::move(path->second)});
  }

  std::vector<shared::entity_uid_t> launched;
  for (const entities::Player_Entity& player : system.entities_of<entities::Player_Entity>())
  {
    if (!player_is_piloting(player) || player.health.current_health <= 0)
      continue;

    bool has_a_rocket = false;
    for (const shared::entity_uid_t uid : piloted)
      has_a_rocket = has_a_rocket || uid == player.entity_id;
    if (!has_a_rocket)
      launched.push_back(player.entity_id);
  }

  for (const shared::entity_uid_t pilot_uid : launched)
  {
    const shared::entity_uid_t uid = system.spawn(entities::entity_type::Guided_Rocket_Entity);
    entities::Guided_Rocket_Entity* rocket = system.get<entities::Guided_Rocket_Entity>(uid);
    const entities::Player_Entity*  pilot  = system.get<entities::Player_Entity>(pilot_uid);
    if (rocket == nullptr || pilot == nullptr)
    {
      log_error("update_guided_rockets: no room to spawn a guided rocket for player {}", pilot_uid);
      continue;
    }

    rocket->pilot_uid = pilot_uid;
    rocket->weapon_id = pilot->last_fire_weapon;
    write_rocket_pose(*rocket, *pilot);

    // The body has held since the press, so the eye is still where the flight began.
    if (const shared::pilot_t* row = try_find_path_laying_pilot_of(rocket->weapon_id))
    {
      shared::flight_path_t path = shared::begin_flight_path(
          pilot->position + vec3f{0.f, shared::player_eye_height, 0.f}, heading_of(*pilot));
      shared::record_flight_path(path, row->path, rocket->position, heading_of(*pilot));
      paths[uid] = std::move(path);
    }
  }

  for (const ended_flight_t& flight : ended)
    solidify_flight_path(context, flight, tick_interval_seconds);

  for (const shared::entity_uid_t uid : spent)
    destroy_entity(context, uid);

  std::erase_if(paths, [&system](const auto& entry) {
    return system.get<entities::Guided_Rocket_Entity>(entry.first) == nullptr;
  });
}

} // namespace server
