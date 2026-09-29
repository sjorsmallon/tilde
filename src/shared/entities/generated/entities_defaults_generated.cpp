// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// Every entity's defaults, as the constructor its header declares. They are
// out of line so that tuning a value is a change to this one TU rather than
// to a header the build includes almost everywhere. The base's own
// fields keep their in-class initializers; `type` is the one the derived
// constructor overwrites.
#include "entities_generated.hpp"

namespace entities
{

Player_Spawn_Entity::Player_Spawn_Entity()
  : spawn_type(Spawn_Type::Human),
    team_allegiance(Team_Allegiance::Free_For_All)
{
  type = entity_type::Player_Spawn_Entity;
}

Player_Spectate_Entity::Player_Spectate_Entity()
{
  type = entity_type::Player_Spectate_Entity;
}

Player_Entity::Player_Entity()
  : view_angle_yaw{},
    view_angle_pitch{},
    body_yaw{},
    health({.current_health = 100, .max_health = 100}),
    death_tick{},
    last_fire_tick{},
    last_fire_weapon(Weapon::Knife),
    reload_complete_time{},
    last_empty_fire_warning_tick{},
    checkpoint_uid{},
    last_hit_tick{},
    last_hit_was_headshot{},
    client_slot_index{},
    ready{},
    wants_to_skip_freeze{},
    display_name{},
    kills{},
    deaths{},
    velocity{},
    inventory{},
    movement{},
    render({.mesh = assets::mesh_asset::Leet_Full}),
    team_allegiance(Team_Allegiance::Free_For_All)
{
  type = entity_type::Player_Entity;
}

Weapon_Entity::Weapon_Entity()
  : ammo(-1),
    reserve_ammo(-1),
    weapon_id{},
    magazine_size(-1),
    max_alive(-1),
    owner_uid{},
    next_fire_time{},
    pickup_allowed_tick{},
    damage_type(Damage_Type::Normal),
    volume({.half_extents = {40.0f, 40.0f, 40.0f}}),
    bounce({.restitution = 0.2f}),
    render({.mesh = assets::mesh_asset::Error})
{
  type = entity_type::Weapon_Entity;
}

Rocket_Entity::Rocket_Entity()
  : projectile({.weapon_id = Weapon::Rocket_Launcher}),
    lifetime(5.0f),
    collision_radius(12.0f),
    render({.mesh = assets::mesh_asset::rocket})
{
  type = entity_type::Rocket_Entity;
}

Hook_Entity::Hook_Entity()
  : projectile({.weapon_id = Weapon::Hook}),
    lifetime(4.0f),
    collision_radius(25.0f),
    render({.mesh = assets::mesh_asset::hookshot, .scale = {0.03f, 0.03f, 0.03f}})
{
  type = entity_type::Hook_Entity;
}

Kooh_Entity::Kooh_Entity()
  : projectile({.weapon_id = Weapon::Kooh}),
    lifetime(4.0f),
    collision_radius(25.0f),
    render({.mesh = assets::mesh_asset::hookshot, .scale = {0.03f, 0.03f, 0.03f}})
{
  type = entity_type::Kooh_Entity;
}

Ricochet_Entity::Ricochet_Entity()
  : projectile({.weapon_id = Weapon::Ricochet}),
    lifetime(3.0f),
    collision_radius(8.0f),
    render({.mesh = assets::mesh_asset::Sphere, .scale = {16.0f, 16.0f, 16.0f}, .material = {.color = {1.0f, 0.45f, 0.15f}}})
{
  type = entity_type::Ricochet_Entity;
}

Platform_Entity::Platform_Entity()
  : projectile({.weapon_id = Weapon::Platform}),
    flight{},
    flight_seconds(0.6f),
    solid_seconds(6.0f),
    half_extents({128.0f, 4.0f, 128.0f}),
    render({.mesh = assets::mesh_asset::Box, .material = {.color = {1.0f, 0.75f, 0.3f}}})
{
  type = entity_type::Platform_Entity;
}

Shrinking_Platform_Entity::Shrinking_Platform_Entity()
  : projectile({.weapon_id = Weapon::Shrinking_Platform}),
    flight{},
    flight_seconds(0.6f),
    solid_seconds(6.0f),
    half_extents({64.0f, 4.0f, 64.0f}),
    half_extents_when_vanishing({8.0f, 4.0f, 8.0f}),
    render({.mesh = assets::mesh_asset::Box, .material = {.color = {0.3f, 0.9f, 0.6f}}})
{
  type = entity_type::Shrinking_Platform_Entity;
}

Extending_Platform_Entity::Extending_Platform_Entity()
  : projectile({.weapon_id = Weapon::Extending_Platform}),
    spawned_tick{},
    length{},
    max_length(1024.0f),
    extend_speed(1200.0f),
    solid_seconds(6.0f),
    half_width(32.0f),
    half_thickness(4.0f),
    render({.mesh = assets::mesh_asset::Box, .material = {.color = {0.95f, 0.55f, 0.85f}}})
{
  type = entity_type::Extending_Platform_Entity;
}

Guided_Rocket_Entity::Guided_Rocket_Entity()
  : pilot_uid{},
    weapon_id(Weapon::Guided_Rocket),
    render({.mesh = assets::mesh_asset::rocket})
{
  type = entity_type::Guided_Rocket_Entity;
}

Canopy_Entity::Canopy_Entity()
  : carrier_uid{},
    position_at_previous_tick{},
    half_extents({48.0f, 4.0f, 48.0f}),
    render({.mesh = assets::mesh_asset::Box, .material = {.color = {0.4f, 0.9f, 1.0f}}})
{
  type = entity_type::Canopy_Entity;
}

Bubble_Entity::Bubble_Entity()
  : projectile({.weapon_id = Weapon::Bubble}),
    flight{},
    popped_tick{},
    popped_by{},
    popped_direction{},
    swell_seconds(0.08f),
    swell_scale(1.2f),
    peel_seconds(0.2f),
    linger_seconds(0.35f),
    flight_seconds(1.0f),
    rest_seconds(8.0f),
    arm_seconds(0.2f),
    radius(32.0f),
    bounce_speed(700.0f),
    render({.mesh = assets::mesh_asset::high_res_sphere, .scale = {2.0f, 2.0f, 2.0f}, .material = {.shader_type = Shader_Type::Ghost, .color = {0.55f, 0.85f, 1.0f}}})
{
  type = entity_type::Bubble_Entity;
}

Physics_Body_Entity::Physics_Body_Entity()
  : shape(Shape_Kind::Box),
    size{},
    bounce{},
    mass(10.0f),
    render{}
{
  type = entity_type::Physics_Body_Entity;
}

Damageable_Entity::Damageable_Entity()
  : health{},
    volume({.half_extents = {16.0f, 32.0f, 16.0f}}),
    weakness(Damage_Type::Orange),
    render{}
{
  type = entity_type::Damageable_Entity;
}

Particle_Emitter_Entity::Particle_Emitter_Entity()
  : sprite(assets::texture_asset::Smoke),
    emit_rate(20.0f),
    max_particles(64),
    lifetime_min(0.5f),
    lifetime_max(1.5f),
    velocity_min(2.0f),
    velocity_max(5.0f),
    spread(0.5f),
    gravity({0.0f, 0.5f, 0.0f}),
    drag(0.3f),
    size_start(0.5f),
    size_end(2.0f),
    rotation_speed_min(-1.0f),
    rotation_speed_max(1.0f),
    color_start({1.0f, 1.0f, 1.0f}),
    color_end({0.5f, 0.5f, 0.5f}),
    alpha_start(0.8f),
    alpha_end(0.0f)
{
  type = entity_type::Particle_Emitter_Entity;
}

Sound_Emitter_Entity::Sound_Emitter_Entity()
  : switch_state{},
    playback{},
    sound{},
    volume(1.0f),
    loop(false),
    spatial(true),
    range(1024.0f)
{
  type = entity_type::Sound_Emitter_Entity;
}

Point_Light_Entity::Point_Light_Entity()
  : switch_state{},
    light{},
    range(256.0f)
{
  type = entity_type::Point_Light_Entity;
}

Spot_Light_Entity::Spot_Light_Entity()
  : switch_state{},
    light{},
    range(512.0f),
    inner_degrees(20.0f),
    outer_degrees(35.0f)
{
  type = entity_type::Spot_Light_Entity;
}

Directional_Light_Entity::Directional_Light_Entity()
  : light{},
    angular_diameter_degrees(0.0f)
{
  type = entity_type::Directional_Light_Entity;
}

Trigger_Volume_Entity::Trigger_Volume_Entity()
  : switch_state{},
    volume({.half_extents = {64.0f, 64.0f, 64.0f}})
{
  type = entity_type::Trigger_Volume_Entity;
}

Jump_Pad_Entity::Jump_Pad_Entity()
  : switch_state{},
    volume({.half_extents = {32.0f, 8.0f, 32.0f}}),
    launch_speed(900.0f),
    render({.mesh = assets::mesh_asset::Duck})
{
  type = entity_type::Jump_Pad_Entity;
}

Reflection_Volume_Entity::Reflection_Volume_Entity()
  : volume({.half_extents = {256.0f, 256.0f, 256.0f}})
{
  type = entity_type::Reflection_Volume_Entity;
}

Game_Rules_Entity::Game_Rules_Entity()
  : match{}
{
  type = entity_type::Game_Rules_Entity;
}

Logic_Counter_Entity::Logic_Counter_Entity()
  : counter{}
{
  type = entity_type::Logic_Counter_Entity;
}

Geometry_Owner_Entity::Geometry_Owner_Entity()
  : switch_state{},
    wipe_timer{},
    passable_by(Team_Allegiance::Free_For_All)
{
  type = entity_type::Geometry_Owner_Entity;
}

Ping_Marker_Entity::Ping_Marker_Entity()
  : lifetime(10.0f),
    pinged_by{},
    spawned_tick(0),
    render({.mesh = assets::mesh_asset::arrow, .rotation = {0.0f, 0.0f, -0.7071068f, 0.7071068f}})
{
  type = entity_type::Ping_Marker_Entity;
}

Logic_Timer_Entity::Logic_Timer_Entity()
  : timer{}
{
  type = entity_type::Logic_Timer_Entity;
}

Path_Node_Entity::Path_Node_Entity()
  : next{},
    traversal_seconds(1.0f),
    wait_seconds(0.0f),
    easing(Easing::Linear)
{
  type = entity_type::Path_Node_Entity;
}

Mover_Entity::Mover_Entity()
  : switch_state{},
    follow{}
{
  type = entity_type::Mover_Entity;
}

Launcher_Entity::Launcher_Entity()
  : switch_state{},
    weapon(Weapon::Bubble),
    trigger(Fire_Trigger::Primary),
    spread_yaw_degrees(0.0f),
    spread_pitch_degrees(0.0f),
    speed_variation(0.0f),
    flight_seconds_variation(0.0f),
    rest_seconds_variation(0.0f),
    fire_interval_seconds(0.0f),
    next_fire_tick{},
    shots_fired{},
    render({.mesh = assets::mesh_asset::Box, .scale = {16.0f, 16.0f, 16.0f}})
{
  type = entity_type::Launcher_Entity;
}

Movement_Modifier_Entity::Movement_Modifier_Entity()
  : switch_state{},
    volume({.half_extents = {128.0f, 128.0f, 128.0f}}),
    gravity_scale(1.0f),
    run_speed_scale(1.0f),
    jump_speed_scale(1.0f),
    friction_scale(1.0f),
    control_scale(1.0f)
{
  type = entity_type::Movement_Modifier_Entity;
}

Remnant_Entity::Remnant_Entity()
  : owner_uid{},
    hit_radius(48.0f),
    render({.mesh = assets::mesh_asset::Duck})
{
  type = entity_type::Remnant_Entity;
}

Modifier_Shot_Entity::Modifier_Shot_Entity()
  : projectile({.weapon_id = Weapon::Modifier_Gun}),
    lifetime(4.0f),
    collision_radius(8.0f),
    render({.mesh = assets::mesh_asset::Sphere, .scale = {16.0f, 16.0f, 16.0f}, .material = {.color = {0.5f, 1.0f, 0.5f}}})
{
  type = entity_type::Modifier_Shot_Entity;
}

Timed_Movement_Modifier_Entity::Timed_Movement_Modifier_Entity()
  : projectile({.weapon_id = Weapon::Modifier_Gun}),
    flight{},
    flight_seconds(1.5f),
    spawned_tick{},
    lifetime_seconds(6.0f),
    half_extents({128.0f, 128.0f, 128.0f}),
    gravity_scale(-1.0f),
    run_speed_scale(1.0f),
    jump_speed_scale(1.0f),
    friction_scale(1.0f),
    control_scale(1.0f),
    render({.mesh = assets::mesh_asset::Box, .material = {.shader_type = Shader_Type::Ghost, .color = {0.5f, 1.0f, 0.5f}}})
{
  type = entity_type::Timed_Movement_Modifier_Entity;
}

Weapon_Emancipation_Grill_Entity::Weapon_Emancipation_Grill_Entity()
  : switch_state{},
    volume({.half_extents = {64.0f, 128.0f, 8.0f}}),
    render({.mesh = assets::mesh_asset::Box, .material = {.shader_type = Shader_Type::Ghost, .color = {1.0f, 0.25f, 0.2f}}})
{
  type = entity_type::Weapon_Emancipation_Grill_Entity;
}

Emancipated_Weapon_Entity::Emancipated_Weapon_Entity()
  : spawned_tick{},
    lifetime_seconds(5.0f),
    rise_distance(48.0f),
    spin_degrees_per_second(90.0f),
    render{}
{
  type = entity_type::Emancipated_Weapon_Entity;
}

Void_Entity::Void_Entity()
  : switch_state{},
    volume({.half_extents = {64.0f, 128.0f, 8.0f}}),
    render({.mesh = assets::mesh_asset::Box, .material = {.shader_type = Shader_Type::Procedural_Blending, .color = {1.0f, 1.0f, 1.0f}}})
{
  type = entity_type::Void_Entity;
}

} // namespace entities
