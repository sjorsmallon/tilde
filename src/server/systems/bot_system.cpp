#include "../../shared/player_constants.hpp"
#include "../../shared/entities/entity_reflection.hpp"
#include "bot_system.hpp"
#include "inventory_system.hpp"

#include "../entity_lifecycle.hpp"
#include "respawn_system.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"
#include "../../shared/pathfinding.hpp"
#include "../../shared/player_move.hpp"

#include <limits>

namespace server
{

Bot_State spawn_bot(shared::game_session_t &session, physics_state_t &physics,
                    const entities::Player_Spawn_Entity &marker,
                    int32_t slot, bot_behavior_t type, bot_personality_t personality)
{
  const shared::entity_uid_t bot_uid =
      session.entity_system.spawn<entities::Player_Entity>();

  entities::Player_Entity *bot =
      session.entity_system.get<entities::Player_Entity>(bot_uid);

  if (!bot)
    log_error("spawn_bot: could not spawn a Player_Entity for bot slot {} — the bot "
              "will have no entity and will do nothing",
              slot);

  if (bot)
  {
    bot->client_slot_index = slot;
    bot->team_allegiance = marker.team_allegiance;
    bot->name.set(std::format("Bot {}", slot).c_str());
    grant_default_inventory(session, bot_uid);
    place_player_at_spawn(session, *bot, marker);

    register_kinematic_capsule(physics,
                               bot_uid,
                               bot->position +
                                   vec3f{0.f, shared::player_capsule_center_offset, 0.f},
                               shared::player_capsule_radius,
                               shared::player_capsule_cylinder_half_height);
  }

  Bot_State state;
  state.entity_uid = bot_uid; // null_entity_uid if the spawn failed
  state.player_slot = slot;
  state.type = type;
  state.personality = personality;
  if (type == bot_behavior_t::Chase)
    state.goal = bot_goal_t::Chase;
  return state;
}

// Advance along the path. Returns the horizontal direction toward the next
// waypoint, or {1,0,0} if no valid waypoint exists.
static vec3f advance_path(Bot_State &bot, const vec3f &bot_pos)
{
  if (bot.path.empty() || bot.path_index >= static_cast<int>(bot.path.size()))
    return bot.last_facing;

  // Advance past waypoints we've already reached.
  while (bot.path_index < static_cast<int>(bot.path.size()))
  {
    const vec3f &wp = bot.path[bot.path_index];
    float dx = wp.x - bot_pos.x;
    float dz = wp.z - bot_pos.z;
    float d2 = dx * dx + dz * dz;
    if (d2 < 32.f * 32.f)
      ++bot.path_index;
    else
      break;
  }

  if (bot.path_index >= static_cast<int>(bot.path.size()))
    return bot.last_facing;

  const vec3f &wp = bot.path[bot.path_index];
  vec3f dir = {wp.x - bot_pos.x, 0.f, wp.z - bot_pos.z};
  float len = linalg::length(dir);
  if (len < 0.001f) return bot.last_facing;
  vec3f facing = dir * (1.f / len);
  bot.last_facing = facing;
  return facing;
}

// The half of a bot's tick that is SIMULATION rather than decision: the move,
// the cosmetics it produces, and the physics pose. Split out because a corpse
// runs this and nothing else -- death takes the AI away, not the simulation,
// which is the same split the human path makes in server_impl.cpp. Sharing the
// one body is the point: a second player_move call for the dead case is how the
// two drift apart.
static void apply_bot_movement(server_context_t &context, physics_state_t &physics,
                               const shared::game_session_t &session,
                               entities::Player_Entity &bot_ent, const vec3f &front,
                               const Move_Input &input, float half_width, float dt)
{
  vec3f right = linalg::cross(front, vec3f{0.f, 1.f, 0.f});
  float rlen  = linalg::length(right);
  if (rlen > 0.001f) right = right * (1.f / rlen);

  Move_Events move_events{};
  auto [new_pos, new_vel] =
      player_move(*context.cvars, input, session.bvh, bot_ent.position, bot_ent.velocity, front,
                  right, half_width, shared::player_half_height, dt, &move_events);

  bot_ent.position = new_pos;
  bot_ent.velocity = new_vel;

  // Movement cosmetics, same as real players. Bots are never the local player
  // on any client, so no originator-suppression is needed — every client hears
  // these spatialized at the bot's position.
  if (move_events.jumped)
  {
    shared::Jump fx{};
    fx.origin          = new_pos;
    fx.attached_entity = bot_ent.entity_id;
    shared::fire_jump(context.outgoing.effects, fx);
  }
  if (move_events.landed &&
      move_events.land_impact_speed > context.cvars->pm_minimum_land_impact_speed)
  {
    shared::Land fx{};
    fx.origin          = new_pos;
    fx.scale           = move_events.land_impact_speed;
    fx.attached_entity = bot_ent.entity_id;
    shared::fire_land(context.outgoing.effects, fx);
  }

  set_kinematic_pose(physics, bot_ent.entity_id,
                     new_pos + vec3f{0.f, shared::player_capsule_center_offset, 0.f}, new_vel);
}

void update_bots(server_context_t &context,
                 uint32_t          current_tick,
                 float             dt)
{
  shared::game_session_t &session = context.world.session;
  physics_state_t        &physics = *context.world.physics;
  std::vector<Bot_State> &bots    = context.world.bots;

  // Nothing fills a shared debug list here, and there is no longer one to fill:
  // the old bot_debug::g_entries lived in game_shared, a STATIC lib, so this
  // DLL's copy was never the one the client read -- and nothing serialized it
  // either. The client's bot visualisation is fed over the wire from
  // context.world.bots directly, in server_impl.cpp's S2C_BotDebug broadcast,
  // which is the real bridge; it lands in client replication_t::bot_debug_entries.

  Span<entities::Player_Entity> players =
      session.entity_system.entities_of<entities::Player_Entity>();

  for (auto &bot : bots)
  {
    // ---- find this bot's entity ----
    // One uid-index lookup. Safe to hold for the rest of the body: the only
    // spawn below is a Rocket_Entity, which is a different pool, and nothing
    // here destroys a player.
    entities::Player_Entity *bot_ent =
        session.entity_system.get<entities::Player_Entity>(bot.entity_uid);
    if (!bot_ent) continue;

    // ---- a corpse decides nothing ----
    // The target scan below already skips dead TARGETS; nothing skipped a dead
    // HUNTER, so a killed bot kept pathing, kept chasing and kept launching
    // rockets with the death clip playing on top of it.
    //
    // Still simulated, though, and on a zeroed input rather than by skipping the
    // move: gravity, friction and the rocket knockback that did the killing all
    // have to play out, or the body hangs where it died. Facing is left frozen
    // for the same reason it is for humans (server_impl.cpp) -- body_yaw places
    // the hit volumes and orients the model, so writing it would spin the corpse
    // under an animation that is supposed to be settling.
    if (bot_ent->health <= 0)
    {
      apply_bot_movement(context, physics, session, *bot_ent,
                         linalg::direction_from_angles(bot_ent->view_angle_yaw, 0.f),
                         Move_Input{}, bot.personality.move_speed, dt);
      continue;
    }

    // ---- find nearest human target ----
    entities::Player_Entity *target    = nullptr;
    float                   best_dist = std::numeric_limits<float>::max();
    for (entities::Player_Entity &p : players)
    {
      if (p.client_slot_index >= BOT_SLOT_BASE) continue;
      // Corpses are invisible to hitscan, so a bot that kept aiming at one
      // would stand there emptying a magazine into a body it cannot hit until
      // the respawn moved it.
      if (p.health <= 0) continue;
      float d = linalg::distance_between(bot_ent->position, p.position);
      if (d < best_dist) { best_dist = d; target = &p; }
    }

    bot.time_spent_in_current_state  += dt;
    bot.path_refresh -= dt;
    bot.fire_cooldown -= dt;

    // ---- state transitions ----
    {
      bot_goal_t next = bot.goal;

      if (bot.type == bot_behavior_t::Idle)
      {
        // Idle bots never leave the Idle state.
        next = bot_goal_t::Idle;
      }
      else if (bot.type == bot_behavior_t::Chase)
      {
        // Chase bots follow players but never attack or retreat.
        next = target ? bot_goal_t::Chase : bot_goal_t::Idle;
      }
      else // bot_behavior_t::Regular — full state machine
      {
        // Retreat takes priority if health is low and personality calls for it.
        if (bot.personality.retreat_health > 0.f &&
            bot_ent->health > 0 &&
            static_cast<float>(bot_ent->health) < bot.personality.retreat_health)
        {
          next = bot_goal_t::Retreat;
        }
        else if (bot.goal == bot_goal_t::Retreat)
        {
          // Exit retreat after timer or if health recovered.
          if (bot.time_spent_in_current_state > 4.f ||
              static_cast<float>(bot_ent->health) >= bot.personality.retreat_health * 2.f)
            next = bot_goal_t::Idle;
        }
        else if (!target)
        {
          next = bot_goal_t::Idle;
        }
        else if (best_dist <= bot.personality.engage_range)
        {
          next = bot_goal_t::Attack;
        }
        else if (bot.goal == bot_goal_t::Attack &&
                 best_dist <= bot.personality.engage_range * 1.5f)
        {
          next = bot_goal_t::Attack;
        }
        else
        {
          next = bot_goal_t::Chase;
        }
      }

      if (next != bot.goal)
      {
        bot.goal        = next;
        bot.time_spent_in_current_state = 0.f;
        bot.path.clear();
        bot.path_index   = 0;
        bot.path_refresh = 0.f;
      }
    }

    // ---- per-state update ----
    // Facing defaults to whatever the bot is already looking at, so a state
    // that never sets `front` leaves the yaw where it was instead of snapping
    // to a fixed world direction.
    vec3f front = linalg::direction_from_angles(bot_ent->view_angle_yaw, 0.f);
    Move_Input input;

    switch (bot.goal)
    {
      case bot_goal_t::Idle:
      {
        // Stand still, keep facing where we already face.
        break;
      }

      case bot_goal_t::Chase:
      {
        if (!target) break;

        // Refresh navmesh path periodically.
        if (bot.path_refresh <= 0.f || bot.path.empty())
        {
          bot.path         = find_path(session.navmesh, bot_ent->position, target->position);
          bot.path_index   = 0;
          bot.path_refresh = bot.personality.path_refresh_rate;
        }

        front         = advance_path(bot, bot_ent->position);
        input.forward_pressed = true;
        break;
      }

      case bot_goal_t::Attack:
      {
        if (!target) break;

        // Face directly at target (horizontal).
        vec3f to = {target->position.x - bot_ent->position.x,
                    0.f,
                    target->position.z - bot_ent->position.z};
        float len = linalg::length(to);
        if (len > 0.001f) front = to * (1.f / len);

        input.forward_pressed = true;

        // Fire.
        if (bot.fire_cooldown <= 0.f)
        {
          bot.fire_cooldown = bot.personality.fire_rate;

          // Bots never reach the human fire path in server_impl.cpp, so they
          // have to stamp the shot themselves or a bot shoots silently on
          // every client. The weapon is written out rather than read from
          // inventory.active_weapon because a bot's is whatever it spawned with while
          // this path always launches a rocket -- latch what actually fired.
          bot_ent->last_fire_tick   = current_tick;
          bot_ent->last_fire_weapon = entities::Weapon::Rocket_Launcher;

          vec3f eye = {bot_ent->position.x,
                       bot_ent->position.y + shared::player_eye_height,
                       bot_ent->position.z};
          vec3f target_center = {target->position.x,
                                 target->position.y + 36.f,
                                 target->position.z};
          vec3f aim_dir = linalg::normalize(target_center - eye);

          // bot_ent and target stay valid across this spawn: it lands in the
          // Rocket_Entity pool, not the Player_Entity one they point into.
          const shared::entity_uid_t rocket_uid =
              session.entity_system.spawn<entities::Rocket_Entity>();
          entities::Rocket_Entity *rocket =
              session.entity_system.get<entities::Rocket_Entity>(rocket_uid);
          if (rocket)
          {
            // A bot's rocket is a player's rocket. It used to restate lifetime,
            // damage, both radii and the hitbox here, which is how bot rockets
            // came to live 5 seconds while player rockets lived 20.
            rocket->position = eye;
            rocket->velocity = aim_dir * 600.f;
            rocket->owner_id = bot_ent->entity_id;
          }
        }
        break;
      }

      case bot_goal_t::Retreat:
      {
        // Pathfind to a point directly away from the threat.
        if (bot.path_refresh <= 0.f || bot.path.empty())
        {
          vec3f flee_point = bot_ent->position;
          if (target)
          {
            vec3f away = {bot_ent->position.x - target->position.x,
                          0.f,
                          bot_ent->position.z - target->position.z};
            float len  = linalg::length(away);
            if (len > 0.001f)
              away = away * (200.f / len);
            flee_point = {bot_ent->position.x + away.x,
                          bot_ent->position.y,
                          bot_ent->position.z + away.z};
          }
          bot.path         = find_path(session.navmesh, bot_ent->position, flee_point);
          bot.path_index   = 0;
          bot.path_refresh = bot.personality.path_refresh_rate;
        }

        front                 = advance_path(bot, bot_ent->position);
        input.forward_pressed = true;
        break;
      }
    }

    // ---- apply movement ----
    apply_bot_movement(context, physics, session, *bot_ent, front, input,
                       bot.personality.move_speed, dt);

    // Update facing direction so the client can visualise it. DEGREES, in
    // direction_from_angles' convention (yaw sweeps +X toward +Z) -- this is
    // the same field the remote-player renderer and the aim poses read, and it
    // used to be written as a raw atan2(x, z) radian value.
    bot_ent->view_angle_yaw =
        linalg::wrap_degrees(linalg::to_degrees(std::atan2(front.z, front.x)));
  }
}

} // namespace server
