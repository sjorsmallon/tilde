#include "weapons.hpp"

namespace shared
{

// The hook and the kooh fly the same shot off both buttons.
constexpr shot_t HOOK_SHOT = {.lifetime_seconds = 4.f,
                              .collision_radius = 25.f,
                              .mesh             = "hookshot",
                              .scale            = {0.03f, 0.03f, 0.03f},
                              .color            = {1.f, 1.f, 1.f}};

// Indexed by entities::Weapon, so entry N is the weapon whose enum value is N
// and the order here must track the .def's, not Fire_Resolution's.
constexpr Enum_Array<entities::Weapon, weapon_definition_t> WEAPON_DEFINITIONS = {{
    {.weapon                  = entities::Weapon::Knife,
     .display_name            = "Knife",
     .slot                    = entities::Inventory_Slot::Melee,
     .fire_interval_seconds   = 0.5f,
     .deploy_duration_seconds = 0.4f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Hitscan,
                                 .hitscan    = {.range = 50.f, .leaves_bullet_impact = false},
                                 .contact    = {.effect = contact_effect_t::Damage,
                                                .damage = {.amount = 50.f, .headshot_multiplier = 1.0f}}},
     .sounds                  = {.fire         = "knife_slash1",
                                 .world_impact = "knife_hitwall1"}},
    {.weapon                  = entities::Weapon::Scout,
     .display_name            = "Scout",
     .slot                    = entities::Inventory_Slot::Primary,
     .fire_interval_seconds   = 1.25f,
     .deploy_duration_seconds = 0.7f,
     .magazine_size           = 10,
     .reload_duration_seconds = 2.0f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Hitscan,
                                 .hitscan    = {.range = 10000.f, .leaves_bullet_impact = true},
                                 .contact    = {.effect = contact_effect_t::Damage,
                                                .damage = {.amount = 60.f, .headshot_multiplier = 2.0f}}},
     .secondary_fire          = {.resolution = entities::Fire_Resolution::Zoom},
     .sounds                  = {.fire         = "scout_fire_1",
                                 .world_impact = assets::NO_ASSET_NAME}},
    {.weapon                  = entities::Weapon::Rocket_Launcher,
     .display_name            = "Rocket Launcher",
     .slot                    = entities::Inventory_Slot::Secondary,
     .fire_interval_seconds   = 0.1f,
     .deploy_duration_seconds = 0.9f,
     .magazine_size           = 0,
     .reload_duration_seconds = 2.5f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 600.f,
                                                .gravity_scale = 0.f,
                                                .spawns = entities::entity_type::Shot_Entity,
                                                .expiry = projectile_expiration_t::Arrives,
                                                .shot   = {.lifetime_seconds = 5.f,
                                                          .collision_radius = 12.f,
                                                          .mesh             = "rocket",
                                                          .scale            = {1.f, 1.f, 1.f},
                                                          .color            = {1.f, 1.f, 1.f}}},
                                 .contact    = {.effect  = contact_effect_t::Explode,
                                                .explode = {.radius = 120.f, .knockback = 600.f}}},
     .sounds                  = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},
    {.weapon                        = entities::Weapon::Dash,
     .display_name                  = "Dash",
     .slot                          = entities::Inventory_Slot::Utility_1,
     .fire_interval_seconds         = 0.f,
     .deploy_duration_seconds       = 0.f,
     .magazine_size                 = 0,
     .reload_duration_seconds       = 0.f,
     .primary_fire                  = {.resolution   = entities::Fire_Resolution::Self_Impulse,
                                       .self_impulse = {.mode            = impulse_mode_t::Add,
                                                        .along_aim_speed = 900.f,
                                                        .upward_speed    = 150.f}},
     // The right mouse button is the same dash with the velocity REPLACED
     // rather than added, so the two can be felt side by side on one key each.
     .secondary_fire                = {.resolution   = entities::Fire_Resolution::Self_Impulse,
                                       .self_impulse = {.mode            = impulse_mode_t::Set,
                                                        .along_aim_speed = 900.f,
                                                        .upward_speed    = 0.f}},
     .self_impulse_cooldown_seconds = 1.5f,
     .sounds                        = {.fire         = "gust_of_wind",
                                       .world_impact = assets::NO_ASSET_NAME}},
    {.weapon                  = entities::Weapon::Swapper,
     .display_name            = "Swapper",
     .slot                    = entities::Inventory_Slot::Utility_2,
     .fire_interval_seconds   = 2.f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Hitscan,
                                 .hitscan    = {.range = 10000.f, .leaves_bullet_impact = false},
                                 .contact    = {.effect = contact_effect_t::Swap}},
     .sounds                  = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},
    // The same hook on both buttons: the primary throws the player hit at the shooter, the secondary reels them in.
    {.weapon                  = entities::Weapon::Hook,
     .display_name            = "Hook",
     .slot                    = entities::Inventory_Slot::Secondary,
     .fire_interval_seconds   = 0.1f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 2.5f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 850.f,
                                                .gravity_scale = 0.f,
                                                .spawns = entities::entity_type::Shot_Entity,
                                                .shot   = HOOK_SHOT},
                                 .contact    = {.effect = contact_effect_t::Throw,
                                                .throw_ = {.subject = contact_subject_t::Target}}},
     .secondary_fire          = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 850.f,
                                                .gravity_scale = 0.f,
                                                .spawns = entities::entity_type::Shot_Entity,
                                                .shot   = HOOK_SHOT},
                                 .contact    = {.effect = contact_effect_t::Reel,
                                                .reel   = {.subject = contact_subject_t::Target,
                                                           .seconds = 1.5f}}},
     .sounds                  = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},
    {.weapon                  = entities::Weapon::Bubble,
     .display_name            = "Bubble",
     .slot                    = entities::Inventory_Slot::Primary,
     .fire_interval_seconds   = 0.1f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 2.5f,
     .limit                   = {.max_alive = 3, .at_limit = at_limit_t::Replace_Oldest},
     .primary_fire            = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 500.f,
                                                .gravity_scale = -0.5f,
                                                .spawns = entities::entity_type::Bubble_Entity}},
     .sounds                  = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},

    {.weapon                  = entities::Weapon::Kooh,
     .display_name            = "Kooh",
     .slot                    = entities::Inventory_Slot::Secondary,
     .fire_interval_seconds   = 0.1f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 2.5f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 850.f,
                                                .gravity_scale = 0.f,
                                                .spawns = entities::entity_type::Shot_Entity,
                                                .shot   = HOOK_SHOT},
                                 .contact    = {.effect = contact_effect_t::Throw,
                                                .throw_ = {.subject = contact_subject_t::Shooter}}},
     .secondary_fire          = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 850.f,
                                                .gravity_scale = 0.f,
                                                .spawns = entities::entity_type::Shot_Entity,
                                                .shot   = HOOK_SHOT},
                                 .contact    = {.effect = contact_effect_t::Reel,
                                                .reel   = {.subject = contact_subject_t::Shooter,
                                                           .seconds = 1.5f}}},
     .sounds                  = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},

    {.weapon                  = entities::Weapon::Magnet,
     .display_name            = "Magnet",
     .slot                    = entities::Inventory_Slot::Primary,
     .fire_interval_seconds   = 0.5f,
     .deploy_duration_seconds = 0.7f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Hitscan,
                                 .hitscan    = {.range = 1500.f, .leaves_bullet_impact = false},
                                 .contact    = {.effect = contact_effect_t::Magnet,
                                                .magnet = {.speed = 600.f}}},
     .secondary_fire          = {.resolution = entities::Fire_Resolution::Hitscan,
                                 .hitscan    = {.range = 1500.f, .leaves_bullet_impact = false},
                                 .contact    = {.effect = contact_effect_t::Magnet,
                                                .magnet = {.speed = -600.f}}},
     .sounds                  = {.fire         = "scout_fire_1",
                                 .world_impact = assets::NO_ASSET_NAME}},
    // The fiddle row: both buttons repeat while held, a reel toward the player hit (a lease the next held
    // hit renews) and a bullet stream.
    {.weapon                  = entities::Weapon::Mock,
     .display_name            = "Mock",
     .slot                    = entities::Inventory_Slot::Primary,
     .fire_interval_seconds   = 0.1f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution       = entities::Fire_Resolution::Hitscan,
                                 .hitscan          = {.range = 3000.f, .leaves_bullet_impact = false},
                                 .contact          = {.effect = contact_effect_t::Reel,
                                                      .reel   = {.subject = contact_subject_t::Shooter,
                                                                 .seconds = 0.25f}},
                                 .fires_while_held = true},
     .secondary_fire          = {.resolution       = entities::Fire_Resolution::Hitscan,
                                 .hitscan          = {.range = 10000.f, .leaves_bullet_impact = true},
                                 .contact          = {.effect = contact_effect_t::Damage,
                                                      .damage = {.amount = 8.f, .headshot_multiplier = 2.0f}},
                                 .fires_while_held = true},
     .sounds                  = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},

    {.weapon                  = entities::Weapon::Platform,
     .display_name            = "Platform",
     .slot                    = entities::Inventory_Slot::Primary,
     .fire_interval_seconds   = 1.0f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 2,
     .reload_duration_seconds = 0.f,
     .refills_on_ground       = true,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 700.f,
                                                .gravity_scale = 0.f,
                                                .spawns = entities::entity_type::Platform_Entity}},
     .secondary_fire          = {.resolution = entities::Fire_Resolution::Recall},
     .sounds                 = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},

    {.weapon                  = entities::Weapon::Shrinking_Platform,
     .display_name            = "Shrinking Platform",
     .slot                    = entities::Inventory_Slot::Secondary,
     .fire_interval_seconds   = 1.0f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 3,
     .reload_duration_seconds = 0.f,
     .refills_on_ground       = true,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 700.f,
                                                .gravity_scale = 0.f,
                                                .spawns = entities::entity_type::Shrinking_Platform_Entity}},
     .secondary_fire          = {.resolution = entities::Fire_Resolution::Recall},
     .sounds                 = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},

    {.weapon                  = entities::Weapon::Remnant,
     .display_name            = "Remnant",
     .slot                    = entities::Inventory_Slot::Secondary,
     .fire_interval_seconds   = 1.0f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Place,
                                 .place      = {.spawns = entities::entity_type::Remnant_Entity}},
     // The teleport shot is aimed at the shooter's OWN remnants and at nothing else: a remnant is a marker,
     // not a body, so it never soaks a bullet and nobody else's can be aimed at.
     .secondary_fire          = {.resolution = entities::Fire_Resolution::Hitscan,
                                 .hitscan    = {.range = 10000.f, .leaves_bullet_impact = false},
                                 .contact    = {.effect  = contact_effect_t::Teleport,
                                                .targets = contact_targets_t::Own_Remnants}},
     .sounds                  = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},
    {.weapon                  = entities::Weapon::Ricochet,
     .display_name            = "Ricochet",
     .slot                    = entities::Inventory_Slot::Secondary,
     .fire_interval_seconds   = 1.0f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 900.f,
                                                .gravity_scale = 1.f,
                                                .spawns = entities::entity_type::Shot_Entity,
                                                .shot   = {.lifetime_seconds = 3.f,
                                                          .collision_radius = 8.f,
                                                          .mesh             = "Sphere",
                                                          .scale            = {16.f, 16.f, 16.f},
                                                          .color            = {1.f, 0.45f, 0.15f}}},
                                 .contact    = {.effect = contact_effect_t::Land}},
     .sounds                  = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},
    // A toggle, not a shot: a press raises or lowers the canopy (canopy_system), so the row has no clocks.
    {.weapon                  = entities::Weapon::Canopy,
     .display_name            = "Canopy",
     .slot                    = entities::Inventory_Slot::Primary,
     .fire_interval_seconds   = 0.f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Canopy},
     .sounds                  = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},
    // Both buttons freeze the player hit for the same two seconds; the left keeps their momentum, the right
    // drops them. Shooting a frozen player again with either releases them early.
    {.weapon                  = entities::Weapon::Statue,
     .display_name            = "Statue",
     .slot                    = entities::Inventory_Slot::Primary,
     .fire_interval_seconds   = 0.5f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Hitscan,
                                 .hitscan    = {.range = 10000.f, .leaves_bullet_impact = false},
                                 .contact    = {.effect = contact_effect_t::Freeze,
                                                .freeze = {.kind    = entities::Movement_Override::Stasis,
                                                           .seconds = 2.0f}}},
     .secondary_fire          = {.resolution = entities::Fire_Resolution::Hitscan,
                                 .hitscan    = {.range = 10000.f, .leaves_bullet_impact = false},
                                 .contact    = {.effect = contact_effect_t::Freeze,
                                                .freeze = {.kind    = entities::Movement_Override::Statue,
                                                           .seconds = 2.0f}}},
     .sounds                  = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},
    // LMB delivers the zone to a surface by a shot; RMB fires the zone itself on a straight, unclipped fixed arc.
    // Both rows are the flight alone: the zone's numbers are Timed_Movement_Modifier_Entity's own defaults.
    {.weapon                  = entities::Weapon::Modifier_Gun,
     .display_name            = "Modifier Gun",
     .slot                    = entities::Inventory_Slot::Primary,
     .fire_interval_seconds   = 1.0f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .limit                   = {.max_alive = 2, .at_limit = at_limit_t::Replace_Oldest},
     .primary_fire            = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 900.f,
                                                .gravity_scale = 0.5f,
                                                .spawns = entities::entity_type::Shot_Entity,
                                                .shot   = {.lifetime_seconds = 4.f,
                                                          .collision_radius = 8.f,
                                                          .mesh             = "Sphere",
                                                          .scale            = {16.f, 16.f, 16.f},
                                                          .color            = {0.5f, 1.f, 0.5f}}},
                                 .contact    = {.effect = contact_effect_t::Leave_Zone}},
     .secondary_fire          = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 700.f,
                                                .gravity_scale = 0.f,
                                                .spawns = entities::entity_type::Timed_Movement_Modifier_Entity}},
     .sounds                  = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},
    // Set down at the waist facing the aim; how far it grows is one sweep at fire time, and every other
    // number is Extending_Platform_Entity's own default.
    {.weapon                  = entities::Weapon::Extending_Platform,
     .display_name            = "Extending Platform",
     .slot                    = entities::Inventory_Slot::Primary,
     .fire_interval_seconds   = 1.0f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 2,
     .reload_duration_seconds = 0.f,
     .refills_on_ground       = true,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Place,
                                 .place      = {.spawns = entities::entity_type::Extending_Platform_Entity,
                                                .anchor = place_anchor_t::Waist}},
     .secondary_fire          = {.resolution = entities::Fire_Resolution::Recall},
     .sounds                 = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},
    // The shooter rides the rocket: the body holds, the aim is the heading, a second press lets go.
    // The path lies an eye's height under the flight, so the flight is the view of whoever walks it.
    {.weapon                        = entities::Weapon::Guided_Rocket,
     .display_name                  = "Guided Rocket",
     .slot                          = entities::Inventory_Slot::Primary,
     .fire_interval_seconds         = 0.f,
     .deploy_duration_seconds       = 0.f,
     .magazine_size                 = 0,
     .reload_duration_seconds       = 0.f,
     .primary_fire                  = {.resolution = entities::Fire_Resolution::Pilot,
                                       .pilot      = {.speed   = 700.f,
                                                      .radius  = 8.f,
                                                      .seconds = 5.f,
                                                      .leaves  = pilot_leaves_t::Path,
                                                      .path    = {.turn_degrees     = 10.f,
                                                                  .shortest_segment = 32.f,
                                                                  .longest_segment  = 192.f,
                                                                  .joint_overlap    = 8.f,
                                                                  .drop = player_eye_height + 4.f,
                                                                  .half_width = 46.f,
                                                                  .solid_seconds = 6.f,
                                                                  .colors     = RAINBOW_COLORS}}},
     .self_impulse_cooldown_seconds = 1.f,
     .sounds                        = {.fire         = assets::NO_ASSET_NAME,
                                       .world_impact = assets::NO_ASSET_NAME}},
    // Two toggles, not shots: a press flips Player_Entity::reveal_light_on or reveal_light_overhead, so the row has no clocks.
    {.weapon                  = entities::Weapon::Flashlight,
     .display_name            = "Flashlight",
     .slot                    = entities::Inventory_Slot::Utility_1,
     .fire_interval_seconds   = 0.f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Reveal_Light},
     .secondary_fire          = {.resolution = entities::Fire_Resolution::Reveal_Light_Overhead},
     .sounds                  = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},
    // The Flashlight's two toggles on the same two player flags; the cone it casts Erases (shared/reveal_light.hpp).
    {.weapon                  = entities::Weapon::Eraser,
     .display_name            = "Eraser",
     .slot                    = entities::Inventory_Slot::Utility_1,
     .fire_interval_seconds   = 0.f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Erase_Light},
     .secondary_fire          = {.resolution = entities::Fire_Resolution::Reveal_Light_Overhead},
     .sounds                  = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},
    // The player hit rides inside the shooter for the row's seconds: the shooter walks and jumps, the one hit aims and fires.
    {.weapon                  = entities::Weapon::Merge,
     .display_name            = "Merge",
     .slot                    = entities::Inventory_Slot::Primary,
     .fire_interval_seconds   = 0.5f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Hitscan,
                                 .hitscan    = {.range = 10000.f, .leaves_bullet_impact = false},
                                 .contact    = {.effect = contact_effect_t::Merge,
                                                .merge  = {.seconds = 10.0f}}},
     .sounds                  = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},
    // A red acetate over the owner's own screen: the scene colour is dropped and the ink lines stay. Never leaves the client.
    {.weapon                  = entities::Weapon::Red_Lens,
     .display_name            = "Red Lens",
     .slot                    = entities::Inventory_Slot::Utility_1,
     .fire_interval_seconds   = 0.f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Lens_View,
                                 .lens_view  = {.filter = entities::Lens_Filter::Red}},
     .sounds                  = {.fire         = assets::NO_ASSET_NAME,
                                 .world_impact = assets::NO_ASSET_NAME}},
}};

// The one check, and it has to carry both failures.
//
// Enum_Array sizes the storage from Weapon_COUNT, so the size can no longer
// disagree -- but it does not fill it. Add a weapon to the .def and this
// initializer is one row short, which value-initializes the tail rather than
// failing: the new weapon gets 0 damage and a null display_name, exactly the
// silence the old deduce-the-size-then-compare spelling was avoiding. And a
// REORDER -- swap two rows, or insert a weapon in the middle -- keeps every
// count right while every lookup returns a neighbour's stats.
//
// Each row names its own weapon, so both show up as a row that is not at its
// own index (a zeroed tail row reads as Knife, which is not where it sits).
static_assert(rows_in_enum_order<&weapon_definition_t::weapon>(WEAPON_DEFINITIONS),
              "WEAPON_DEFINITIONS rows are not in Weapon enum order: get_weapon_definition "
              "indexes by enum value, so a row out of place returns another weapon's stats.");

// THE SECOND CHECK: a self-impulse must be gated by exactly one clock.
//
// Its gate is Movement::seconds_until_impulse_ready, because that is the only
// per-player state a client's reconciliation replay restarts from -- the two
// clocks that gate every other weapon (Weapon_Entity::next_fire_time and
// Inventory::deploy_complete_time) are both server-only, so a client that
// predicted a dash against either would be predicting against a number it does
// not have. See try_apply_self_impulse.
//
// An impulse on either button never passes through the shot clocks, so a row
// on which NEITHER button does must carry zero clocks and no magazine: those
// are numbers nothing reads, and the day one is read beside the movement gate
// it is two gates that agree today and drift the first time one is tuned. A
// cooldown on a row with no impulse is the same silence the other way round.
//
// The day a self-impulse genuinely wants a raise time, what earns it is a
// PREDICTED deploy clock in the replay, not a second gate here.
constexpr uint32_t first_self_impulse_row_gated_by_more_than_movement()
{
  for (const weapon_definition_t& definition : WEAPON_DEFINITIONS)
  {
    const uint32_t row = static_cast<uint32_t>(definition.weapon);
    const fire_resolution_traits_t& primary   = get_fire_resolution_traits(definition.primary_fire.resolution);
    const fire_resolution_traits_t& secondary = get_fire_resolution_traits(definition.secondary_fire.resolution);
    const bool any_impulse = primary.gated_by_impulse_timer || secondary.gated_by_impulse_timer;
    const bool any_shot    = primary.runs_shot_clocks || secondary.runs_shot_clocks;

    if (any_impulse != (definition.self_impulse_cooldown_seconds > 0.f))
      return row;

    if (!any_shot &&
        (definition.fire_interval_seconds != 0.f || definition.deploy_duration_seconds != 0.f ||
         definition.magazine_size != 0))
      return row;
  }
  return entities::Weapon_COUNT;
}

static_assert(first_self_impulse_row_gated_by_more_than_movement() == entities::Weapon_COUNT,
              "THE LEFT NUMBER BELOW IS THE OFFENDING ROW'S Weapon VALUE. "
              "a row firing a self-impulse or a pilot on either button carries a positive "
              "self_impulse_cooldown_seconds and a row with none carries zero, and a row on "
              "which no button is Hitscan or Projectile carries zero fire_interval_seconds, "
              "zero deploy_duration_seconds and no magazine: the only gate an impulse has is "
              "Movement::seconds_until_impulse_ready, which is the only one the client can "
              "replay.");

// THE THIRD CHECK: each button's parameters match its resolution, and its
// contact's numbers match its effect. The struct a resolution reads must be
// filled and the ones it does not read must be zero -- a range on a
// Self_Impulse fire is a number nothing reads, which is the same silence as an
// unfilled row. The contact is union-shaped the same way over its effect. None
// and Zoom read nothing. The primary may be neither: an unfilled primary is a
// zeroed one, and Zoom is the client's right-click toggle.
constexpr bool contact_parameters_match_effect(const contact_t& contact)
{
  const bool damage_is_zero  = contact.damage.amount == 0.f && contact.damage.headshot_multiplier == 0.f;
  const bool magnet_is_zero  = contact.magnet.speed == 0.f;
  const bool reel_is_zero    = contact.reel.subject == contact_subject_t::Target && contact.reel.seconds == 0.f;
  const bool throw_is_zero   = contact.throw_.subject == contact_subject_t::Target;
  const bool freeze_is_zero  = contact.freeze.kind == entities::Movement_Override::None &&
                               contact.freeze.seconds == 0.f;
  const bool explode_is_zero = contact.explode.radius == 0.f && contact.explode.knockback == 0.f;
  const bool merge_is_zero   = contact.merge.seconds == 0.f;
  const bool aims_at_bodies  = contact.targets == contact_targets_t::Bodies;

  switch (contact.effect)
  {
  case contact_effect_t::None:
  case contact_effect_t::Swap:
  case contact_effect_t::Land:
  case contact_effect_t::Leave_Zone:
    return damage_is_zero && magnet_is_zero && reel_is_zero && throw_is_zero && freeze_is_zero &&
           explode_is_zero && merge_is_zero && aims_at_bodies;
  case contact_effect_t::Damage:
    return contact.damage.amount > 0.f && contact.damage.headshot_multiplier > 0.f &&
           magnet_is_zero && reel_is_zero && throw_is_zero && freeze_is_zero && explode_is_zero &&
           merge_is_zero && aims_at_bodies;
  case contact_effect_t::Magnet:
    return contact.magnet.speed != 0.f && damage_is_zero && reel_is_zero && throw_is_zero &&
           freeze_is_zero && explode_is_zero && merge_is_zero && aims_at_bodies;
  case contact_effect_t::Reel:
    return contact.reel.seconds > 0.f && damage_is_zero && magnet_is_zero && throw_is_zero &&
           freeze_is_zero && explode_is_zero && merge_is_zero && aims_at_bodies;
  case contact_effect_t::Throw:
    return damage_is_zero && magnet_is_zero && reel_is_zero && freeze_is_zero &&
           explode_is_zero && merge_is_zero && aims_at_bodies;
  case contact_effect_t::Teleport:
    return damage_is_zero && magnet_is_zero && reel_is_zero && throw_is_zero && freeze_is_zero &&
           explode_is_zero && merge_is_zero && contact.targets == contact_targets_t::Own_Remnants;
  case contact_effect_t::Freeze:
    return (contact.freeze.kind == entities::Movement_Override::Stasis ||
            contact.freeze.kind == entities::Movement_Override::Statue) &&
           contact.freeze.seconds > 0.f && damage_is_zero && magnet_is_zero && reel_is_zero &&
           throw_is_zero && explode_is_zero && merge_is_zero && aims_at_bodies;
  case contact_effect_t::Explode:
    return contact.explode.radius > 0.f && contact.explode.knockback > 0.f && damage_is_zero &&
           magnet_is_zero && reel_is_zero && throw_is_zero && freeze_is_zero && merge_is_zero &&
           aims_at_bodies;
  case contact_effect_t::Merge:
    return contact.merge.seconds > 0.f && damage_is_zero && magnet_is_zero && reel_is_zero &&
           throw_is_zero && freeze_is_zero && explode_is_zero && aims_at_bodies;
  }
  return false;
}

constexpr bool fire_parameters_match_resolution(const weapon_fire_t& fire)
{
  const hitscan_t&      hitscan    = fire.hitscan;
  const projectile_t&   projectile = fire.projectile;
  const place_t&        place      = fire.place;
  const self_impulse_t& impulse    = fire.self_impulse;
  const bool hitscan_is_zero    = hitscan.range == 0.f && !hitscan.leaves_bullet_impact;
  const bool projectile_is_zero = projectile.speed == 0.f && projectile.gravity_scale == 0.f &&
                                  projectile.spawns == entities::entity_type::Invalid &&
                                  projectile.expiry == projectile_expiration_t::Vanishes &&
                                  shot_is_zero(projectile.shot);
  const bool shot_matches_spawn = projectile.spawns == entities::entity_type::Shot_Entity
                                      ? shot_is_filled(projectile.shot)
                                      : shot_is_zero(projectile.shot);
  const bool impulse_is_zero    = impulse.along_aim_speed == 0.f && impulse.upward_speed == 0.f;
  const bool place_is_zero      = place.spawns == entities::entity_type::Invalid;
  const bool pilot_is_zero      = fire.pilot.speed == 0.f && fire.pilot.radius == 0.f &&
                                  fire.pilot.seconds == 0.f &&
                                  fire.pilot.leaves == pilot_leaves_t::Nothing &&
                                  flight_path_settings_are_zero(fire.pilot.path);
  const bool contact_is_none    = fire.contact.effect == contact_effect_t::None;
  const bool contact_matches    = contact_parameters_match_effect(fire.contact);

  switch (fire.resolution)
  {
  // Canopy, Recall, the two Reveal_Lights and Lens_View read nothing either: a press toggles the canopy, the light or where its cone hangs, or takes the row's shots back, and carries no numbers.
  case entities::Fire_Resolution::None:
  case entities::Fire_Resolution::Zoom:
  case entities::Fire_Resolution::Canopy:
  case entities::Fire_Resolution::Recall:
  case entities::Fire_Resolution::Reveal_Light:
  case entities::Fire_Resolution::Reveal_Light_Overhead:
  case entities::Fire_Resolution::Erase_Light:
  case entities::Fire_Resolution::Lens_View:
    return hitscan_is_zero && projectile_is_zero && place_is_zero && impulse_is_zero &&
           pilot_is_zero && contact_is_none && contact_matches && !fire.fires_while_held;
  case entities::Fire_Resolution::Hitscan:
    return hitscan.range > 0.f && !contact_is_none && contact_matches && projectile_is_zero &&
           place_is_zero && impulse_is_zero && pilot_is_zero;
  case entities::Fire_Resolution::Projectile:
    return projectile.speed > 0.f && projectile.spawns != entities::entity_type::Invalid &&
           shot_matches_spawn &&
           (projectile.expiry == projectile_expiration_t::Vanishes || !contact_is_none) &&
           contact_matches && hitscan_is_zero && place_is_zero && impulse_is_zero && pilot_is_zero;
  case entities::Fire_Resolution::Place:
    return place.spawns != entities::entity_type::Invalid && hitscan_is_zero &&
           projectile_is_zero && impulse_is_zero && pilot_is_zero && contact_is_none &&
           contact_matches && !fire.fires_while_held;
  case entities::Fire_Resolution::Self_Impulse:
    return hitscan_is_zero && projectile_is_zero && place_is_zero && pilot_is_zero &&
           contact_is_none && contact_matches && !fire.fires_while_held;
  case entities::Fire_Resolution::Pilot:
    return fire.pilot.speed > 0.f && fire.pilot.radius > 0.f && fire.pilot.seconds > 0.f &&
           pilot_path_matches_what_it_leaves(fire.pilot) && hitscan_is_zero &&
           projectile_is_zero && place_is_zero && impulse_is_zero && contact_is_none &&
           contact_matches && !fire.fires_while_held;
  }
  return false;
}

constexpr uint32_t first_row_whose_parameters_mismatch_its_resolution()
{
  for (const weapon_definition_t& definition : WEAPON_DEFINITIONS)
  {
    const uint32_t row = static_cast<uint32_t>(definition.weapon);
    const fire_resolution_traits_t& primary   = get_fire_resolution_traits(definition.primary_fire.resolution);
    const fire_resolution_traits_t& secondary = get_fire_resolution_traits(definition.secondary_fire.resolution);
    if (!primary.acts_on_server)
      return row;
    if (!fire_parameters_match_resolution(definition.primary_fire) ||
        !fire_parameters_match_resolution(definition.secondary_fire))
      return row;
    if ((definition.primary_fire.fires_while_held || definition.secondary_fire.fires_while_held) &&
        !(definition.fire_interval_seconds > 0.f))
      return row;
    if (definition.refills_on_ground &&
        !(definition.magazine_size > 0 && definition.reload_duration_seconds == 0.f &&
          definition.limit.max_alive == 0))
      return row;
    const bool spawns_an_entity = primary.spawns_an_entity || secondary.spawns_an_entity;
    if (definition.limit.max_alive > 0 && !spawns_an_entity)
      return row;
    if ((definition.primary_fire.resolution == entities::Fire_Resolution::Recall ||
         definition.secondary_fire.resolution == entities::Fire_Resolution::Recall) &&
        !spawns_an_entity)
      return row;
    for (const weapon_fire_t* fire : {&definition.primary_fire, &definition.secondary_fire})
      if (fire->fires_while_held && fire->contact.effect == contact_effect_t::Reel &&
          !(fire->contact.reel.seconds > definition.fire_interval_seconds))
        return row;
  }
  return entities::Weapon_COUNT;
}

static_assert(first_row_whose_parameters_mismatch_its_resolution() == entities::Weapon_COUNT,
              "THE LEFT NUMBER BELOW IS THE OFFENDING ROW'S Weapon VALUE. "
              "each fire of a WEAPON_DEFINITIONS row fills the parameter struct of its own "
              "Fire_Resolution (hitscan: positive range; projectile: positive speed and a spawned "
              "entity type, a filled shot block exactly when that type is Shot_Entity, and an Arrives expiry only beside a contact effect; place: a spawned entity type; pilot: positive speed, radius and "
              "seconds, and path numbers only when it leaves a Path) and leaves the others zero, and the "
              "primary is neither None nor Zoom: resolve_player_shot reads exactly one struct, "
              "so a value in another is a number nothing reads. A Hitscan carries a contact "
              "effect; a Projectile may; None, Zoom, Canopy, both Reveal_Lights, Place, Self_Impulse and Pilot carry "
              "contact_effect_t::None. The contact fills the sub-struct of its own effect "
              "(Damage: positive amount and headshot_multiplier; Magnet: a non-zero speed; Reel: "
              "positive seconds; Freeze: Stasis or Statue and positive seconds; Explode: positive "
              "radius and knockback; Merge: positive seconds) and leaves the others zero; Teleport aims at Own_Remnants and "
              "everything else at Bodies. fires_while_held is for a Hitscan or Projectile fire on "
              "a row with a positive fire_interval_seconds: with no interval a held trigger fires "
              "once per sub-tick step, which is a rate set by how many edges the tick had. A held "
              "Reel's seconds is LONGER than the fire interval: it is a lease the next hit renews, "
              "and one that runs out between two hits drops the reel ten times a second. An "
              "alive limit is for a row with a Projectile or Place fire: spawn_projectile and "
              "spawn_placed_entity are the two places that enforce it. A row that "
              "refills_on_ground carries a magazine, zero reload_duration_seconds and NO alive "
              "limit: the ground is its only reload and the magazine is already how many of its "
              "shots exist. A Recall sits beside a Projectile or Place fire on the other button: "
              "it takes back what that one spawned.");

} // namespace shared
