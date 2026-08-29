// A player's weapons are entities, and each one recovers on its OWN clock.
//
// The bug this guards against: one fire deadline per player, compared against
// whatever weapon happened to be in hand. That measured the incoming weapon's
// interval from the outgoing weapon's shot, so firing a Scout (1.25s) delayed a
// Knife swing, and swinging a Knife (0.5s) delayed a Scout that had been
// holstered and idle for a minute. Both directions are checked below.
#include "entities/entity_reflection.hpp"
#include "game_session.hpp"
#include "log.hpp"
#include "physics.hpp"
#include "server_context.hpp"
#include "subtick.hpp"
#include "systems/inventory_system.hpp"
#include "weapons.hpp"

#include <cstdio>

static int32_t failure_count = 0;

static void check(bool condition, const char* description)
{
  printf("%s %s\n", condition ? "  ok  " : "FAILED", description);
  if (!condition)
    ++failure_count;
}

// What resolve_player_shot's gate does, in the two clocks it is: the weapon's
// own interval, which runs while holstered, and the PLAYER's deploy deadline,
// which blocks every weapon at once. Both, because the whole point is that they
// are separate and neither one alone is the gate.
static bool weapon_may_fire_at(shared::game_session_t&         session,
                               const entities::Player_Entity& player,
                               shared::subtick_time_t         moment)
{
  const entities::Weapon_Entity* active = server::try_find_active_weapon(session, player);
  if (active == nullptr)
    return false;

  return moment >= active->next_fire_time && moment >= player.inventory.deploy_complete_time;
}

// Put a named weapon in hand by selecting the slot its definition declares.
// The inventory is keyed by SLOT and this test is about per-weapon clocks, so
// this is the one place the two vocabularies meet -- the default loadout puts
// exactly one weapon in each of the three slots it uses, which is what makes
// naming a weapon here unambiguous.
static void equip(entities::Player_Entity& player, entities::Weapon weapon)
{
  player.inventory.active_slot = shared::get_weapon_definition(weapon).slot;
}

// What the switch in server_impl.cpp's step loop does: equip, and charge the
// INCOMING weapon's deploy time against the player. No magazine changes hands.
static void switch_to(entities::Player_Entity& player, entities::Weapon weapon,
                      shared::subtick_time_t moment, float tick_dt)
{
  equip(player, weapon);
  player.inventory.deploy_complete_time = shared::subtick_time_after(
      moment, shared::get_weapon_definition(weapon).deploy_duration_seconds, tick_dt);
}

static void fire_at(shared::game_session_t& session, const entities::Player_Entity& player,
                    shared::subtick_time_t moment, float tick_dt)
{
  entities::Weapon_Entity* active = server::try_find_active_weapon(session, player);
  if (active == nullptr)
    return;
  active->next_fire_time = shared::subtick_time_after(
      moment, shared::get_weapon_definition(active->weapon_id).fire_interval_seconds, tick_dt);
}

int main()
{
  shared::game_session_t session;

  const shared::entity_uid_t player_uid =
      session.entity_system.spawn<entities::Player_Entity>();
  server::grant_default_inventory(session, player_uid);

  entities::Player_Entity* player =
      session.entity_system.get<entities::Player_Entity>(player_uid);
  if (player == nullptr)
  {
    printf("FAILED could not resolve the spawned player\n");
    return 1;
  }

  // --- the inventory itself ---
  {
    bool every_weapon_carried = true;
    bool every_entity_agrees  = true;
    for (uint32_t index = 0; index < enum_traits<entities::Weapon>::count; ++index)
    {
      const entities::Weapon     weapon = (entities::Weapon)index;
      const shared::entity_uid_t uid =
          player->inventory.weapons[shared::get_weapon_definition(weapon).slot];
      if (uid == shared::null_entity_uid)
      {
        every_weapon_carried = false;
        continue;
      }

      const entities::Weapon_Entity* entity =
          session.entity_system.get<entities::Weapon_Entity>(uid);
      if (entity == nullptr || entity->weapon_id != weapon || entity->owner_uid != player_uid ||
          entity->ammo != shared::get_weapon_definition(weapon).magazine_size)
        every_entity_agrees = false;
    }
    check(every_weapon_carried,
          "the default inventory puts each weapon in the slot its definition names");
    check(every_entity_agrees,
          "each weapon entity knows its own type, owner and magazine");
  }

  // --- the active weapon resolves through the SLOT, not a second handle ---
  {
    equip(*player, entities::Weapon::Scout);
    const entities::Weapon_Entity* as_scout = server::try_find_active_weapon(session, *player);
    equip(*player, entities::Weapon::Knife);
    const entities::Weapon_Entity* as_knife = server::try_find_active_weapon(session, *player);

    check(as_scout != nullptr && as_scout->weapon_id == entities::Weapon::Scout &&
              as_knife != nullptr && as_knife->weapon_id == entities::Weapon::Knife,
          "active_slot selects which carried weapon is in hand");

    // An empty slot is a legal hand, not a decode failure: nothing is granted
    // into Utility_2, and selecting it resolves to no weapon rather than to
    // whatever sits at that index. This is what a spent card leaves behind.
    player->inventory.active_slot = entities::Inventory_Slot::Utility_2;
    check(server::try_find_active_weapon(session, *player) == nullptr,
          "an empty slot resolves to no weapon rather than to a neighbour's");
  }

  // --- the fix: the two clocks are independent ---
  const float tick_dt = 1.0f / 60.0f;

  {
    // Fire the Scout at tick 100, then switch to the Knife and swing in the very
    // next slot. The Scout's 1.25s interval is still running; the Knife's is not,
    // and the Knife is what is in hand.
    equip(*player, entities::Weapon::Scout);
    const shared::subtick_time_t scout_shot = shared::subtick_time(100, 0);
    check(weapon_may_fire_at(session, *player, scout_shot), "a fresh Scout may fire");
    fire_at(session, *player, scout_shot, tick_dt);

    check(!weapon_may_fire_at(session, *player, shared::subtick_time(100, 1)),
          "the Scout is still recovering one slot after its own shot");

    equip(*player, entities::Weapon::Knife);
    check(weapon_may_fire_at(session, *player, shared::subtick_time(100, 1)),
          "switching to the Knife one slot later may swing -- the Scout's recovery is the "
          "Scout's, not the player's");
  }

  {
    // The other direction, which the old model got wrong more visibly: a weapon
    // recovers WHILE HOLSTERED, so coming back to it after its interval has
    // elapsed finds it ready rather than gated on whatever fired since.
    equip(*player, entities::Weapon::Knife);
    const shared::subtick_time_t knife_swing = shared::subtick_time(200, 0);
    fire_at(session, *player, knife_swing, tick_dt);

    // The Scout fired at tick 100 and its 1.25s interval (75 ticks) is long
    // past, though it has been holstered for all of it.
    equip(*player, entities::Weapon::Scout);
    check(weapon_may_fire_at(session, *player, shared::subtick_time(200, 1)),
          "a holstered weapon recovers -- the Scout is ready despite the Knife just swinging");

    // And the Knife is not, because that one really did just fire.
    equip(*player, entities::Weapon::Knife);
    check(!weapon_may_fire_at(session, *player, shared::subtick_time(200, 1)),
          "the weapon that actually fired is the one still recovering");
  }

  // --- the deploy gate: the OTHER clock, and it is the player's ---
  {
    // Both weapons are long since recovered -- tick 400 is minutes past
    // anything fired above -- so the only thing that can refuse a shot here is
    // the switch itself. That is the point: this gate is invisible to the
    // per-weapon clock and vice versa.
    server::refill_inventory(session, *player);

    const shared::subtick_time_t press = shared::subtick_time(400, 0);
    switch_to(*player, entities::Weapon::Scout, press, tick_dt);

    const float deploy_seconds =
        shared::get_weapon_definition(entities::Weapon::Scout).deploy_duration_seconds;
    const shared::subtick_time_t ready =
        shared::subtick_time_after(press, deploy_seconds, tick_dt);

    check(!weapon_may_fire_at(session, *player, press),
          "the weapon being raised may not fire in the slot it was selected in");
    check(!weapon_may_fire_at(session, *player, ready - 1),
          "still deploying one slot before the deadline");
    check(weapon_may_fire_at(session, *player, ready),
          "the deploy deadline is when the weapon is up");

    // It blocks EVERY weapon, which is what makes it the player's rather than
    // the weapon's: switching away mid-deploy does not dodge it, it recharges
    // it with the new weapon's number.
    equip(*player, entities::Weapon::Knife);
    check(!weapon_may_fire_at(session, *player, press + 1),
          "a deploy in flight blocks a weapon that is not the one being raised");

    // A second switch REPLACES the deadline rather than extending it: the
    // duration belongs to the weapon being raised at the moment the key went
    // down, and nothing about the switch it interrupted survives. The Knife is
    // quicker than the Scout, so switching to it mid-deploy is genuinely ready
    // sooner -- which is what Source does too, and is a consequence of storing
    // a deadline rather than accumulating one.
    const shared::subtick_time_t second_press = shared::subtick_time(400, 5);
    switch_to(*player, entities::Weapon::Knife, second_press, tick_dt);

    const shared::subtick_time_t knife_ready = shared::subtick_time_after(
        second_press, shared::get_weapon_definition(entities::Weapon::Knife).deploy_duration_seconds,
        tick_dt);

    check(knife_ready < ready && !weapon_may_fire_at(session, *player, knife_ready - 1) &&
              weapon_may_fire_at(session, *player, knife_ready),
          "a second switch replaces the deploy deadline with its own weapon's, rather than "
          "extending the one it interrupted");
  }

  // --- the magazine belongs to the weapon, so a switch is not a reload ---
  {
    server::refill_inventory(session, *player);
    equip(*player, entities::Weapon::Scout);

    entities::Weapon_Entity* scout = server::try_find_active_weapon(session, *player);
    const int32_t full = shared::get_weapon_definition(entities::Weapon::Scout).magazine_size;
    if (scout != nullptr)
      scout->ammo = full - 3;

    // The cheese that used to live in the switch handler: `ammo` was one field
    // per player, so equipping had to hand out a fresh magazine to keep "ammo
    // is the held weapon's count" true -- which made every keypress a free
    // instant reload.
    equip(*player, entities::Weapon::Knife);
    equip(*player, entities::Weapon::Scout);

    const entities::Weapon_Entity* after = server::try_find_active_weapon(session, *player);
    check(after != nullptr && after->ammo == full - 3,
          "switching away and back does not refill the magazine");

    // And it really is per weapon: the Rocket Launcher's count is untouched by
    // any of the above.
    equip(*player, entities::Weapon::Rocket_Launcher);
    const entities::Weapon_Entity* rocket = server::try_find_active_weapon(session, *player);
    check(rocket != nullptr &&
              rocket->ammo ==
                  shared::get_weapon_definition(entities::Weapon::Rocket_Launcher).magazine_size,
          "one weapon's spent rounds are not another's");
  }

  // --- a respawn is what clears all of it ---
  {
    equip(*player, entities::Weapon::Scout);
    entities::Weapon_Entity* scout = server::try_find_active_weapon(session, *player);
    if (scout != nullptr)
    {
      scout->ammo           = 1;
      scout->next_fire_time = shared::subtick_time(9999, 0);
    }
    switch_to(*player, entities::Weapon::Scout, shared::subtick_time(500, 0), tick_dt);

    server::refill_inventory(session, *player);

    const entities::Weapon_Entity* fresh = server::try_find_active_weapon(session, *player);
    check(fresh != nullptr &&
              fresh->ammo == shared::get_weapon_definition(entities::Weapon::Scout).magazine_size &&
              fresh->next_fire_time == 0 && player->inventory.deploy_complete_time == 0,
          "a refill restores every magazine and clears both clocks -- the deadlines are "
          "absolute, so a corpse's would otherwise gate the new body");
  }

  // --- a self-impulse is gated by MOVEMENT state, and by nothing else -------
  //
  // The property the whole prediction rests on (generalization_def.md §4): the
  // client can replay this because Movement is the only per-player state a
  // replay restarts from. If any of it started reading a weapon clock or a
  // magazine, the dash the client takes and the dash the server takes would
  // stop being the same dash.
  {
    const shared::weapon_definition_t& dash =
        shared::get_weapon_definition(entities::Weapon::Dash);

    entities::Movement movement{};
    vec3f              velocity{0.f, -400.f, 0.f};
    const vec3f        aim{1.f, 0.f, 0.f};

    const bool fired = shared::try_apply_self_impulse(dash, aim, movement, velocity);
    check(fired && velocity.x == dash.self_impulse_along_aim_speed,
          "a self-impulse pushes the shooter along the aim it was fired on");

    // Like a jump, not like a sum: the fall in progress is cancelled rather
    // than subtracted from the launch, so how long the player had been falling
    // does not decide how much of their dash survives.
    check(velocity.y == dash.self_impulse_upward_speed,
          "the upward half cancels a fall rather than being eaten by it");
    check(movement.seconds_until_impulse_ready == dash.self_impulse_cooldown_seconds,
          "firing charges the cooldown, which is the only gate there is");

    vec3f      second_velocity{0.f, 0.f, 0.f};
    const bool fired_again =
        shared::try_apply_self_impulse(dash, aim, movement, second_velocity);
    check(!fired_again && second_velocity.x == 0.f,
          "a second press while the cooldown runs does nothing at all");

    // Nothing else in the table is one, and asking is the arm's own job rather
    // than the caller's -- the client calls this straight off whatever is in
    // the hand, with no switch of its own to have got right.
    entities::Movement scout_movement{};
    vec3f              scout_velocity{0.f, 0.f, 0.f};
    check(!shared::try_apply_self_impulse(
              shared::get_weapon_definition(entities::Weapon::Scout), aim, scout_movement,
              scout_velocity),
          "a weapon that is not a self-impulse is refused by the function, not by its caller");
  }

  // --- an empty inventory is a refusal, not a crash ---
  {
    const shared::entity_uid_t bare_uid =
        session.entity_system.spawn<entities::Player_Entity>();
    const entities::Player_Entity* bare =
        session.entity_system.get<entities::Player_Entity>(bare_uid);
    check(bare != nullptr && server::try_find_active_weapon(session, *bare) == nullptr,
          "a player with no inventory resolves no active weapon");
  }

  // --- granting into an occupied slot ---
  //
  // The pickup / card-draw door. Needs a context rather than a session because
  // the weapon it displaces has to be DESTROYED: a slot overwritten in place
  // leaves a Weapon_Entity nothing holds a handle to.
  {
    jolt_init();
    server::server_context_t context;
    context.world.physics = make_physics_state();

    const shared::entity_uid_t owner_uid =
        context.world.session.entity_system.spawn<entities::Player_Entity>();
    server::grant_default_inventory(context.world.session, owner_uid);

    entities::Player_Entity* owner =
        context.world.session.entity_system.get<entities::Player_Entity>(owner_uid);
    const entities::Inventory_Slot slot =
        shared::get_weapon_definition(entities::Weapon::Scout).slot;
    const shared::entity_uid_t displaced_uid = owner->inventory.weapons[slot];

    const shared::entity_uid_t granted_uid =
        server::try_grant_weapon(context, *owner, entities::Weapon::Scout);

    owner = context.world.session.entity_system.get<entities::Player_Entity>(owner_uid);
    check(granted_uid != shared::null_entity_uid && granted_uid != displaced_uid,
          "granting into an occupied slot spawns a second weapon entity");
    check(owner->inventory.weapons[slot] == granted_uid,
          "...and the slot names the one just granted");
    check(context.world.session.entity_system.get<entities::Weapon_Entity>(displaced_uid) ==
              nullptr,
          "...and the weapon it displaced is destroyed rather than leaked");
  }

  printf("%s (%d failure%s)\n", failure_count == 0 ? "PASSED" : "FAILED", failure_count,
         failure_count == 1 ? "" : "s");
  return failure_count == 0 ? 0 : 1;
}
