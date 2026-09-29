// A player's weapons are entities, and each one recovers on its OWN clock.
//
// The bug this guards against: one fire deadline per player, compared against
// whatever weapon happened to be in hand. That measured the incoming weapon's
// interval from the outgoing weapon's shot, so firing a Scout (1.25s) delayed a
// Knife swing, and swinging a Knife (0.5s) delayed a Scout that had been
// holstered and idle for a minute. Both directions are checked below.
#include "entities/entity_reflection.hpp"
#include "entities/generated/entities/extending_platform_entity_generated.hpp"
#include "entities/generated/entities/guided_rocket_entity_generated.hpp"
#include "entities/generated/entities/modifier_shot_entity_generated.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "entities/generated/entities/weapon_entity_generated.hpp"
#include "game_session.hpp"
#include "log.hpp"
#include "player_constants.hpp"
#include "server_context.hpp"
#include "spawn_projectile.hpp"
#include "subtick.hpp"
#include "systems/guided_rocket_system.hpp"
#include "systems/inventory_system.hpp"
#include "weapon_instance.hpp"
#include "weapons.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace server
{
uint32_t get_tick_number() { return 0; }
}

static int32_t failure_count = 0;

static void check(bool condition, const char* description)
{
  printf("%s %s\n", condition ? "  ok  " : "FAILED", description);
  if (!condition)
    ++failure_count;
}

// One weapon per slot, the hand starting on the knife: a player spawns empty-handed.
static constexpr Array<entities::Weapon, 5> TEST_LOADOUT = {{
    entities::Weapon::Knife,
    entities::Weapon::Scout,
    entities::Weapon::Rocket_Launcher,
    entities::Weapon::Dash,
    entities::Weapon::Swapper,
}};

constexpr const shared::weapon_definition_t& SCOUT = shared::get_weapon_definition(entities::Weapon::Scout);
constexpr const shared::weapon_definition_t& KNIFE = shared::get_weapon_definition(entities::Weapon::Knife);

constexpr const shared::weapon_definition_t& PLATFORM =
    shared::get_weapon_definition(entities::Weapon::Platform);

static_assert(shared::reloaded_magazine(SCOUT, {.size = 10, .ammo = 3, .reserve_ammo = -1}).ammo == 10);
static_assert(shared::reloaded_magazine(SCOUT, {.size = 10, .ammo = 3, .reserve_ammo = -1}).reserve_ammo == -1);
static_assert(shared::reloaded_magazine(SCOUT, {.size = 10, .ammo = 3, .reserve_ammo = 4}).ammo == 7);
static_assert(shared::reloaded_magazine(SCOUT, {.size = 10, .ammo = 3, .reserve_ammo = 4}).reserve_ammo == 0);
static_assert(shared::reloaded_magazine(SCOUT, {.size = 10, .ammo = 3, .reserve_ammo = 30}).reserve_ammo == 23);
static_assert(shared::reloaded_magazine(SCOUT, {.size = 5, .ammo = 3, .reserve_ammo = -1}).ammo == 5);
static_assert(!shared::reload_may_start(SCOUT, {.size = 10, .ammo = 3, .reserve_ammo = 0}));
static_assert(!shared::reload_may_start(SCOUT, {.size = 10, .ammo = -1, .reserve_ammo = -1}));
static_assert(!shared::reload_may_start(SCOUT, {.size = 10, .ammo = 10, .reserve_ammo = 30}));
static_assert(!shared::reload_may_start(KNIFE, {.size = 0, .ammo = 0, .reserve_ammo = 30}));
static_assert(!shared::reload_may_start(PLATFORM, {.size = 2, .ammo = 0, .reserve_ammo = -1}));
static_assert(shared::ammo_after_ground_refill(PLATFORM, {.size = 5, .ammo = 0, .reserve_ammo = -1}) == 5);
static_assert(shared::ammo_after_ground_refill(PLATFORM, {.size = 5, .ammo = -1, .reserve_ammo = -1}) == -1);
static_assert(shared::ammo_after_ground_refill(PLATFORM, {.size = 2, .ammo = 4, .reserve_ammo = -1}) == 4);
static_assert(shared::ammo_after_ground_refill(SCOUT, {.size = 10, .ammo = 0, .reserve_ammo = -1}) == 0);
static_assert(shared::ammo_allows_a_shot(-1) && shared::ammo_allows_a_shot(1) &&
              !shared::ammo_allows_a_shot(0));

static void grant_test_loadout(shared::game_session_t& session, shared::entity_uid_t player_uid)
{
  for (const entities::Weapon weapon : TEST_LOADOUT)
  {
    const shared::weapon_definition_t& definition = shared::get_weapon_definition(weapon);
    const shared::entity_uid_t weapon_uid = session.entity_system.spawn<entities::Weapon_Entity>();

    entities::Weapon_Entity* weapon_entity =
        session.entity_system.get<entities::Weapon_Entity>(weapon_uid);
    weapon_entity->weapon_id = weapon;
    shared::write_weapon_kind_counts(*weapon_entity);
    weapon_entity->owner_uid = player_uid;

    entities::Player_Entity* player = session.entity_system.get<entities::Player_Entity>(player_uid);
    player->inventory.weapons[definition.slot] = weapon_uid;
    player->inventory.active_slot              = entities::Inventory_Slot::Melee;
  }
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
// this is the one place the two vocabularies meet -- the test loadout puts
// exactly one weapon in each slot, which is what makes
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

constexpr shared::weapon_fire_t HELD_FIRE = {.resolution = entities::Fire_Resolution::Hitscan,
                                             .fires_while_held = true};
constexpr shared::weapon_fire_t PRESSED_FIRE = {.resolution = entities::Fire_Resolution::Hitscan};

static_assert(!shared::try_find_held_fire_time(PRESSED_FIRE, 0, 0, 640, 704),
              "a fire that is not fires_while_held never repeats");
static_assert(shared::try_find_held_fire_time(HELD_FIRE, 600, 0, 640, 704) == 640,
              "a held fire already due fires where the step opens");
static_assert(shared::try_find_held_fire_time(HELD_FIRE, 650, 0, 640, 704) == 650,
              "a held fire due inside the step fires AT its interval, not at a step boundary");
static_assert(!shared::try_find_held_fire_time(HELD_FIRE, 704, 0, 640, 704),
              "a held fire due where the step closes belongs to the next step");
static_assert(shared::try_find_held_fire_time(HELD_FIRE, 600, 700, 640, 704) == 700,
              "a held fire waits out the deploy");

int main()
{
  shared::game_session_t session;

  const shared::entity_uid_t player_uid =
      session.entity_system.spawn<entities::Player_Entity>();
  grant_test_loadout(session, player_uid);

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
    for (const entities::Weapon weapon : TEST_LOADOUT)
    {
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
          entity->ammo != shared::full_magazine_of(shared::get_weapon_definition(weapon)))
        every_entity_agrees = false;
    }
    check(every_weapon_carried,
          "a granted weapon sits in the slot its definition names");
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
    const shared::entity_uid_t utility_2_uid =
        player->inventory.weapons[entities::Inventory_Slot::Utility_2];
    player->inventory.weapons[entities::Inventory_Slot::Utility_2] = shared::null_entity_uid;
    player->inventory.active_slot = entities::Inventory_Slot::Utility_2;
    check(server::try_find_active_weapon(session, *player) == nullptr,
          "an empty slot resolves to no weapon rather than to a neighbour's");
    player->inventory.weapons[entities::Inventory_Slot::Utility_2] = utility_2_uid;
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
              rocket->ammo == shared::full_magazine_of(shared::get_weapon_definition(
                                  entities::Weapon::Rocket_Launcher)),
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

    const cvars::cvar_state_t         defaults{};
    const shared::movement_settings_t settings = shared::movement_settings_from(defaults);

    entities::Movement movement{};
    vec3f              velocity{0.f, -400.f, 0.f};
    const vec3f        aim{1.f, 0.f, 0.f};

    const bool fired = shared::try_apply_self_impulse(
        settings, dash, entities::Fire_Trigger::Primary, aim, movement, velocity);
    check(fired && velocity.x == dash.primary_fire.self_impulse.along_aim_speed,
          "a self-impulse pushes the shooter along the aim it was fired on");

    // Like a jump, not like a sum: the fall in progress is cancelled rather
    // than subtracted from the launch, so how long the player had been falling
    // does not decide how much of their dash survives.
    check(velocity.y == dash.primary_fire.self_impulse.upward_speed,
          "the upward half cancels a fall rather than being eaten by it");
    check(movement.seconds_until_impulse_ready == dash.self_impulse_cooldown_seconds,
          "firing charges the cooldown, which is the only gate there is");
    // The row carries no duration any more: where an impulse lands and how
    // long it lasts is the MODEL's answer, and quake's is "in the velocity".
    check(movement.seconds_until_speed_returns_to_base_speed == 0.f,
          "the quake model keeps an impulse in the velocity, so it borrows nothing");

    cvars::cvar_state_t instant_cvars{};
    instant_cvars.pm_model = cvars::Locomotion_Model::instant;
    const shared::movement_settings_t instant_settings =
        shared::movement_settings_from(instant_cvars);

    entities::Movement instant_movement{};
    vec3f              instant_velocity{0.f, 0.f, 0.f};
    check(shared::try_apply_self_impulse(instant_settings, dash, entities::Fire_Trigger::Primary,
                                         aim, instant_movement, instant_velocity) &&
              instant_movement.seconds_until_speed_returns_to_base_speed ==
                  instant_settings.instant.speed_return_seconds,
          "the instant model borrows it instead, for a duration the row does not carry");

    vec3f      second_velocity{0.f, 0.f, 0.f};
    const bool fired_again = shared::try_apply_self_impulse(
        settings, dash, entities::Fire_Trigger::Primary, aim, movement, second_velocity);
    check(!fired_again && second_velocity.x == 0.f,
          "a second press while the cooldown runs does nothing at all");

    // The secondary spends the SAME cooldown: one countdown in Movement, so
    // one gate for both buttons.
    vec3f secondary_while_cooling{0.f, 0.f, 0.f};
    check(!shared::try_apply_self_impulse(settings, dash, entities::Fire_Trigger::Secondary, aim,
                                          movement, secondary_while_cooling),
          "the secondary impulse is refused while the primary's cooldown runs");

    // Set mode replaces the velocity outright, so what the player was doing at
    // the press does not reach the outcome -- a sideways run and a fall both
    // come out as exactly the aimed speed plus the lift.
    entities::Movement secondary_movement{};
    vec3f              secondary_velocity{0.f, -400.f, 250.f};
    check(dash.secondary_fire.resolution == entities::Fire_Resolution::Self_Impulse &&
              dash.secondary_fire.self_impulse.mode == shared::impulse_mode_t::Set,
          "the Dash's right mouse button is the Set-mode impulse this block tests");
    check(shared::try_apply_self_impulse(settings, dash, entities::Fire_Trigger::Secondary, aim,
                                         secondary_movement, secondary_velocity),
          "the secondary impulse fires off a fresh cooldown");
    check(secondary_velocity.x == dash.secondary_fire.self_impulse.along_aim_speed &&
              secondary_velocity.y == dash.secondary_fire.self_impulse.upward_speed &&
              secondary_velocity.z == 0.f,
          "a Set-mode impulse replaces the velocity rather than joining it");
    check(secondary_movement.seconds_until_impulse_ready == dash.self_impulse_cooldown_seconds,
          "the secondary charges the shared cooldown");
    check(secondary_movement.seconds_until_speed_returns_to_base_speed == 0.f,
          "and the secondary asks the same model the same question");

    // Nothing else in the table is one, and asking is the arm's own job rather
    // than the caller's -- the client calls this straight off whatever is in
    // the hand, with no switch of its own to have got right. The Scout's
    // secondary is Zoom, which is the client's FOV and no impulse at all.
    entities::Movement scout_movement{};
    vec3f              scout_velocity{0.f, 0.f, 0.f};
    check(!shared::try_apply_self_impulse(settings,
                                          shared::get_weapon_definition(entities::Weapon::Scout),
                                          entities::Fire_Trigger::Primary, aim, scout_movement,
                                          scout_velocity),
          "a weapon that is not a self-impulse is refused by the function, not by its caller");
    check(!shared::try_apply_self_impulse(settings,
                                          shared::get_weapon_definition(entities::Weapon::Scout),
                                          entities::Fire_Trigger::Secondary, aim, scout_movement,
                                          scout_velocity),
          "a Zoom secondary is refused as an impulse: the scope is the client's, not a shove");
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
    server::server_context_t context;

    const shared::entity_uid_t owner_uid =
        context.world.session.entity_system.spawn<entities::Player_Entity>();
    grant_test_loadout(context.world.session, owner_uid);

    entities::Player_Entity* owner =
        context.world.session.entity_system.get<entities::Player_Entity>(owner_uid);
    const entities::Inventory_Slot slot =
        shared::get_weapon_definition(entities::Weapon::Scout).slot;
    const shared::entity_uid_t displaced_uid = owner->inventory.weapons[slot];

    const shared::entity_uid_t granted_uid =
        server::try_grant_weapon(context, *owner, owner->inventory, entities::Weapon::Scout);

    owner = context.world.session.entity_system.get<entities::Player_Entity>(owner_uid);
    check(granted_uid != shared::null_entity_uid && granted_uid != displaced_uid,
          "granting into an occupied slot spawns a second weapon entity");
    check(owner->inventory.weapons[slot] == granted_uid,
          "...and the slot names the one just granted");
    check(context.world.session.entity_system.get<entities::Weapon_Entity>(displaced_uid) ==
              nullptr,
          "...and the weapon it displaced is destroyed rather than leaked");

    const float tick_dt = 1.f / 60.f;
    owner->inventory.active_slot = slot;
    owner->position              = {0.f, 0.f, 0.f};
    owner->velocity              = {900.f, 0.f, 0.f};

    check(server::try_throw_active_weapon(context, *owner, {1.f, 0.f, 0.f}, tick_dt),
          "throwing the held weapon succeeds");
    check(owner->inventory.weapons[slot] == shared::null_entity_uid,
          "...and empties the slot it was held in");
    entities::Weapon_Entity* thrown =
        context.world.session.entity_system.get<entities::Weapon_Entity>(granted_uid);
    check(thrown != nullptr && thrown->owner_uid == shared::null_entity_uid,
          "...and the same weapon entity survives with no owner");
    check(thrown != nullptr && !thrown->bounce.at_rest && thrown->bounce.velocity.x > 900.f,
          "...as a bounce body carrying the thrower's speed plus the throw");
    check(!server::try_throw_active_weapon(context, *owner, {1.f, 0.f, 0.f}, tick_dt),
          "throwing an empty hand does nothing");

    owner->position = thrown->position - vec3f{0.f, 36.f, 0.f};
    server::update_dropped_weapons(context);
    check(owner->inventory.weapons[slot] == shared::null_entity_uid,
          "a thrown weapon cannot be picked up before its delay runs out");

    context.tick_number = thrown->pickup_allowed_tick;
    server::update_dropped_weapons(context);
    check(owner->inventory.weapons[slot] == granted_uid && thrown->owner_uid == owner_uid,
          "touching it after the delay puts the same weapon back in its slot");

    check(server::try_throw_active_weapon(context, *owner, {1.f, 0.f, 0.f}, tick_dt),
          "a picked-up weapon can be thrown again");
    const shared::entity_uid_t replacement_uid =
        server::try_grant_weapon(context, *owner, owner->inventory, entities::Weapon::Scout);
    owner  = context.world.session.entity_system.get<entities::Player_Entity>(owner_uid);
    thrown = context.world.session.entity_system.get<entities::Weapon_Entity>(granted_uid);
    owner->position     = thrown->position - vec3f{0.f, 36.f, 0.f};
    context.tick_number = thrown->pickup_allowed_tick;
    server::update_dropped_weapons(context);
    check(owner->inventory.weapons[slot] == replacement_uid &&
              thrown->owner_uid == shared::null_entity_uid,
          "a weapon is not picked up into a slot that is already full");
  }

  // --- the weapon's alive limit replaces the owner's oldest shot off that button, and nobody else's ---
  {
    server::server_context_t context;
    shared::Entity_System& entity_system = context.world.session.entity_system;

    const shared::weapon_definition_t& modifier_gun =
        shared::get_weapon_definition(entities::Weapon::Modifier_Gun);

    entities::Weapon_Entity held;
    held.weapon_id = entities::Weapon::Modifier_Gun;
    shared::write_weapon_kind_counts(held);
    check(shared::alive_limit_of(held).max_alive == modifier_gun.limit.max_alive &&
              modifier_gun.limit.max_alive > 0,
          "a modifier gun is born with its kind's alive limit");

    held.max_alive = 4;
    const shared::alive_limit_t limit = shared::alive_limit_of(held);
    const uint32_t max_alive          = limit.max_alive;
    check(max_alive == 4, "and the limit a shot reads is the one written on the weapon");

    const shared::entity_uid_t owner_uid = entity_system.spawn<entities::Player_Entity>();
    const shared::entity_uid_t other_uid = entity_system.spawn<entities::Player_Entity>();
    const vec3f origin    = {0.f, 0.f, 0.f};
    const vec3f direction = {1.f, 0.f, 0.f};

    const shared::entity_uid_t other_shot = server::spawn_projectile(
        context, other_uid, modifier_gun, origin, direction, entities::Fire_Trigger::Primary, limit);
    const shared::entity_uid_t secondary_shot = server::spawn_projectile(
        context, owner_uid, modifier_gun, origin, direction, entities::Fire_Trigger::Secondary, limit);

    std::vector<shared::entity_uid_t> shots;
    for (uint32_t shot = 0; shot < max_alive + 1; ++shot)
      shots.push_back(server::spawn_projectile(context, owner_uid, modifier_gun, origin, direction,
                                               entities::Fire_Trigger::Primary, limit));

    uint32_t alive = 0;
    for (const entities::Modifier_Shot_Entity& shot : entity_system.entities_of<entities::Modifier_Shot_Entity>())
      if (shot.projectile.owner_uid == owner_uid)
        ++alive;
    check(alive == max_alive, "firing past the limit keeps max_alive shots alive");
    check(entity_system.try_find(shots.front()) == nullptr, "...and the one removed is the oldest");
    check(entity_system.try_find(shots.back()) != nullptr, "...and the newest is alive");
    check(entity_system.try_find(other_shot) != nullptr, "another owner's shot is not counted");
    check(entity_system.try_find(secondary_shot) != nullptr, "the other button's shot is not counted");

    held.max_alive = 0;
    for (uint32_t shot = 0; shot < 3; ++shot)
      server::spawn_projectile(context, owner_uid, modifier_gun, origin, direction,
                               entities::Fire_Trigger::Primary, shared::alive_limit_of(held));
    alive = 0;
    for (const entities::Modifier_Shot_Entity& shot : entity_system.entities_of<entities::Modifier_Shot_Entity>())
      if (shot.projectile.owner_uid == owner_uid)
        ++alive;
    check(alive == max_alive + 3, "a weapon written to 0 has no limit");
  }

  // --- a weapon from a file that never said takes its kind's counts, once ---
  {
    entities::Weapon_Entity typed;
    typed.weapon_id = entities::Weapon::Platform;
    typed.ammo      = 1;
    shared::convert_weapon_without_counts(typed);
    check(typed.magazine_size == 1 && typed.ammo == 1,
          "a ground-refill gun keeps the ammo its author typed as its magazine");
    check(typed.max_alive == static_cast<int32_t>(PLATFORM.limit.max_alive),
          "and takes its kind's alive limit");

    entities::Weapon_Entity untouched;
    untouched.weapon_id = entities::Weapon::Platform;
    shared::convert_weapon_without_counts(untouched);
    check(untouched.magazine_size == PLATFORM.magazine_size &&
              untouched.ammo == PLATFORM.magazine_size,
          "one nobody typed a count into is its kind's, and full");

    entities::Weapon_Entity scout;
    scout.weapon_id = entities::Weapon::Scout;
    scout.ammo      = 3;
    shared::convert_weapon_without_counts(scout);
    check(scout.magazine_size == SCOUT.magazine_size && scout.ammo == 3,
          "a reloading gun found half empty is still its kind's size");

    entities::Weapon_Entity authored;
    authored.weapon_id     = entities::Weapon::Platform;
    authored.ammo          = -1;
    authored.magazine_size = 5;
    authored.max_alive     = 0;
    shared::convert_weapon_without_counts(authored);
    check(authored.magazine_size == 5 && authored.ammo == -1 && authored.max_alive == 0,
          "a weapon that carries its counts is left as written");
  }

  // --- a piloted flight is followed by one rocket and leaves one path, which the next flight replaces ---
  {
    server::server_context_t context;
    shared::Entity_System& entity_system = context.world.session.entity_system;

    const float tick_interval_seconds = 1.f / 60.f;
    const shared::weapon_definition_t& guided_rocket =
        shared::get_weapon_definition(entities::Weapon::Guided_Rocket);
    const shared::pilot_t& row = guided_rocket.primary_fire.pilot;
    const vec3f eye = {0.f, shared::player_eye_height, 0.f};

    const shared::entity_uid_t pilot_uid = entity_system.spawn<entities::Player_Entity>();
    const auto pilot = [&]() { return entity_system.get<entities::Player_Entity>(pilot_uid); };
    pilot()->last_fire_weapon = entities::Weapon::Guided_Rocket;

    const auto count_segments = [&]() {
      uint32_t count = 0;
      for (const entities::Extending_Platform_Entity& platform :
           entity_system.entities_of<entities::Extending_Platform_Entity>())
        if (platform.projectile.owner_uid == pilot_uid)
          ++count;
      return count;
    };

    const auto fly = [&](uint32_t first_tick, uint32_t tick_count) {
      pilot()->movement.seconds_until_impulse_ready = 0.f;
      check(shared::try_begin_pilot_flight(guided_rocket, entities::Fire_Trigger::Primary, eye,
                                           pilot()->movement),
            "the press launches");
      for (uint32_t tick = 0; tick < tick_count; ++tick)
      {
        pilot()->movement.override_target_position =
            eye + vec3f{10.f * static_cast<float>(tick + 1), 0.f, 0.f};
        context.tick_number = first_tick + tick;
        server::update_guided_rockets(context, tick_interval_seconds);
      }
    };

    fly(100, 60);
    Span<entities::Guided_Rocket_Entity> rockets =
        entity_system.entities_of<entities::Guided_Rocket_Entity>();
    check(rockets.size() == 1 && rockets[0].pilot_uid == pilot_uid, "a pilot has exactly one rocket");
    check(rockets.size() == 1 && rockets[0].position.x == 600.f &&
              rockets[0].position.y == shared::player_eye_height,
          "and it is where the pilot's movement flew it");
    check(count_segments() == 0, "a flight leaves nothing while it is live");

    pilot()->movement.active_override = entities::Movement_Override::None;
    context.tick_number               = 160;
    server::update_guided_rockets(context, tick_interval_seconds);

    check(entity_system.entities_of<entities::Guided_Rocket_Entity>().empty(),
          "the rocket goes the tick the flight has ended");
    check(count_segments() == 3, "600 units of straight flight is three segments");
    check(context.world.flight_path_by_rocket_uid.empty(), "and the recording goes with the rocket");

    uint32_t earliest = 0xffffffffu;
    uint32_t latest   = 0;
    for (const entities::Extending_Platform_Entity& platform :
         entity_system.entities_of<entities::Extending_Platform_Entity>())
    {
      earliest = std::min(earliest, platform.spawned_tick);
      latest   = std::max(latest, platform.spawned_tick);
      check(platform.position.y == shared::player_eye_height - row.path.drop,
            "a segment lies the row's drop under the flight");
      check(platform.length == 200.f + row.path.joint_overlap, "as long as its edge and the overlap");
      check(platform.half_width == row.path.half_width, "and as wide as the row says");
    }
    const uint32_t extend_ticks = static_cast<uint32_t>(
        std::ceil((200.f + row.path.joint_overlap) /
                  (entities::Extending_Platform_Entity{}.extend_speed * tick_interval_seconds)));
    check(earliest == 160, "the first segment starts growing the tick the flight ended");
    check(latest == 160 + 2 * extend_ticks, "and each next one the tick the one before it is grown");

    fly(300, 30);
    pilot()->movement.active_override = entities::Movement_Override::None;
    context.tick_number               = 330;
    server::update_guided_rockets(context, tick_interval_seconds);
    check(count_segments() == 2, "a second flight replaces the path the first one left");

    fly(400, 30);
    pilot()->health.current_health = 0;
    context.tick_number            = 430;
    server::update_guided_rockets(context, tick_interval_seconds);
    check(pilot()->movement.override_seconds_remaining == 0.f, "a dead pilot's flight is let go");
    check(entity_system.entities_of<entities::Guided_Rocket_Entity>().empty(), "its rocket goes");
    check(count_segments() == 2, "and it leaves no path of its own");
  }

  // --- a refills_on_ground magazine fills on the map, and nowhere else ---
  {
    server::server_context_t context;
    shared::Entity_System& entity_system = context.world.session.entity_system;

    const shared::weapon_definition_t& platform = PLATFORM;

    const shared::entity_uid_t carrier_uid = entity_system.spawn<entities::Player_Entity>();
    grant_test_loadout(context.world.session, carrier_uid);
    entities::Player_Entity* carrier = entity_system.get<entities::Player_Entity>(carrier_uid);

    const shared::entity_uid_t platform_uid = server::try_grant_weapon(
        context, *carrier, carrier->inventory, entities::Weapon::Platform);
    carrier = entity_system.get<entities::Player_Entity>(carrier_uid);

    const auto platform_ammo = [&]() {
      return entity_system.get<entities::Weapon_Entity>(platform_uid)->ammo;
    };
    const auto knife_ammo = [&]() {
      return entity_system
          .get<entities::Weapon_Entity>(carrier->inventory.weapons[entities::Inventory_Slot::Melee])
          ->ammo;
    };

    check(platform_ammo() == platform.magazine_size, "a granted platform gun starts full");

    entity_system.get<entities::Weapon_Entity>(platform_uid)->ammo = 0;

    carrier->movement.is_grounded      = false;
    carrier->movement.ground_mover_uid = shared::null_entity_uid;
    server::refill_magazines_on_ground(context.world.session, *carrier);
    check(platform_ammo() == 0, "nothing refills in the air");

    carrier->movement.is_grounded      = true;
    carrier->movement.ground_mover_uid = platform_uid;
    server::refill_magazines_on_ground(context.world.session, *carrier);
    check(platform_ammo() == 0, "standing on a mover is not standing on the ground");

    carrier->movement.ground_mover_uid = shared::null_entity_uid;
    server::refill_magazines_on_ground(context.world.session, *carrier);
    check(platform_ammo() == platform.magazine_size, "the map under the feet fills the magazine");
    check(knife_ammo() == shared::UNLIMITED_AMMO, "and a row that does not ask for it is left alone");

    entity_system.get<entities::Weapon_Entity>(platform_uid)->ammo = 0;
    server::refill_inventory(context.world.session, *carrier);
    check(platform_ammo() == 0, "a respawn's reload does not fill it: the ground does");

    entity_system.get<entities::Weapon_Entity>(platform_uid)->magazine_size = 5;
    server::refill_magazines_on_ground(context.world.session, *carrier);
    check(platform_ammo() == 5, "the ground fills to the size written on the weapon, not the row's");

    entity_system.get<entities::Weapon_Entity>(platform_uid)->ammo = shared::UNLIMITED_AMMO;
    server::refill_magazines_on_ground(context.world.session, *carrier);
    check(platform_ammo() == shared::UNLIMITED_AMMO, "and an unlimited one stays unlimited");
  }

  printf("%s (%d failure%s)\n", failure_count == 0 ? "PASSED" : "FAILED", failure_count,
         failure_count == 1 ? "" : "s");
  return failure_count == 0 ? 0 : 1;
}
