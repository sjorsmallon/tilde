// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// The dispatch table and its shims. A shim is the ONLY code that names a
// union member or downcasts an entity: it adapts the table's uniform call
// into one handler's typed one. A declared handler nobody defined is a
// LINK error naming the symbol -- there is no registration and no bind
// step, so "forgot to register" is not representable.
#include "entity_io_generated.hpp"
#include "entity_io_queue.hpp"
#include "entities/entity_reflection.hpp"
#include "log.hpp"

namespace entities
{

namespace
{

void shim_sound_emitter_entity_enable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Sound_Emitter_Entity& self = *entity_as<Sound_Emitter_Entity>(&entity);
  enable(self, self.switch_state, data.as_enable(), context);
}

void shim_point_light_entity_enable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Point_Light_Entity& self = *entity_as<Point_Light_Entity>(&entity);
  enable(self, self.switch_state, data.as_enable(), context);
}

void shim_spot_light_entity_enable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Spot_Light_Entity& self = *entity_as<Spot_Light_Entity>(&entity);
  enable(self, self.switch_state, data.as_enable(), context);
}

void shim_trigger_volume_entity_enable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Trigger_Volume_Entity& self = *entity_as<Trigger_Volume_Entity>(&entity);
  enable(self, self.switch_state, data.as_enable(), context);
}

void shim_jump_pad_entity_enable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Jump_Pad_Entity& self = *entity_as<Jump_Pad_Entity>(&entity);
  enable(self, self.switch_state, data.as_enable(), context);
}

void shim_geometry_owner_entity_enable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Geometry_Owner_Entity& self = *entity_as<Geometry_Owner_Entity>(&entity);
  enable(self, self.switch_state, data.as_enable(), context);
}

void shim_mover_entity_enable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Mover_Entity& self = *entity_as<Mover_Entity>(&entity);
  enable(self, self.switch_state, data.as_enable(), context);
}

void shim_launcher_entity_enable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Launcher_Entity& self = *entity_as<Launcher_Entity>(&entity);
  enable(self, self.switch_state, data.as_enable(), context);
}

void shim_movement_modifier_entity_enable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Movement_Modifier_Entity& self = *entity_as<Movement_Modifier_Entity>(&entity);
  enable(self, self.switch_state, data.as_enable(), context);
}

void shim_weapon_emancipation_grill_entity_enable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Weapon_Emancipation_Grill_Entity& self = *entity_as<Weapon_Emancipation_Grill_Entity>(&entity);
  enable(self, self.switch_state, data.as_enable(), context);
}

void shim_sound_emitter_entity_disable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Sound_Emitter_Entity& self = *entity_as<Sound_Emitter_Entity>(&entity);
  disable(self, self.switch_state, data.as_disable(), context);
}

void shim_point_light_entity_disable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Point_Light_Entity& self = *entity_as<Point_Light_Entity>(&entity);
  disable(self, self.switch_state, data.as_disable(), context);
}

void shim_spot_light_entity_disable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Spot_Light_Entity& self = *entity_as<Spot_Light_Entity>(&entity);
  disable(self, self.switch_state, data.as_disable(), context);
}

void shim_trigger_volume_entity_disable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Trigger_Volume_Entity& self = *entity_as<Trigger_Volume_Entity>(&entity);
  disable(self, self.switch_state, data.as_disable(), context);
}

void shim_jump_pad_entity_disable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Jump_Pad_Entity& self = *entity_as<Jump_Pad_Entity>(&entity);
  disable(self, self.switch_state, data.as_disable(), context);
}

void shim_geometry_owner_entity_disable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Geometry_Owner_Entity& self = *entity_as<Geometry_Owner_Entity>(&entity);
  disable(self, self.switch_state, data.as_disable(), context);
}

void shim_mover_entity_disable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Mover_Entity& self = *entity_as<Mover_Entity>(&entity);
  disable(self, self.switch_state, data.as_disable(), context);
}

void shim_launcher_entity_disable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Launcher_Entity& self = *entity_as<Launcher_Entity>(&entity);
  disable(self, self.switch_state, data.as_disable(), context);
}

void shim_movement_modifier_entity_disable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Movement_Modifier_Entity& self = *entity_as<Movement_Modifier_Entity>(&entity);
  disable(self, self.switch_state, data.as_disable(), context);
}

void shim_weapon_emancipation_grill_entity_disable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Weapon_Emancipation_Grill_Entity& self = *entity_as<Weapon_Emancipation_Grill_Entity>(&entity);
  disable(self, self.switch_state, data.as_disable(), context);
}

void shim_sound_emitter_entity_toggle_enabled(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Sound_Emitter_Entity& self = *entity_as<Sound_Emitter_Entity>(&entity);
  toggle_enabled(self, self.switch_state, data.as_toggle_enabled(), context);
}

void shim_point_light_entity_toggle_enabled(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Point_Light_Entity& self = *entity_as<Point_Light_Entity>(&entity);
  toggle_enabled(self, self.switch_state, data.as_toggle_enabled(), context);
}

void shim_spot_light_entity_toggle_enabled(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Spot_Light_Entity& self = *entity_as<Spot_Light_Entity>(&entity);
  toggle_enabled(self, self.switch_state, data.as_toggle_enabled(), context);
}

void shim_trigger_volume_entity_toggle_enabled(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Trigger_Volume_Entity& self = *entity_as<Trigger_Volume_Entity>(&entity);
  toggle_enabled(self, self.switch_state, data.as_toggle_enabled(), context);
}

void shim_jump_pad_entity_toggle_enabled(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Jump_Pad_Entity& self = *entity_as<Jump_Pad_Entity>(&entity);
  toggle_enabled(self, self.switch_state, data.as_toggle_enabled(), context);
}

void shim_geometry_owner_entity_toggle_enabled(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Geometry_Owner_Entity& self = *entity_as<Geometry_Owner_Entity>(&entity);
  toggle_enabled(self, self.switch_state, data.as_toggle_enabled(), context);
}

void shim_mover_entity_toggle_enabled(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Mover_Entity& self = *entity_as<Mover_Entity>(&entity);
  toggle_enabled(self, self.switch_state, data.as_toggle_enabled(), context);
}

void shim_launcher_entity_toggle_enabled(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Launcher_Entity& self = *entity_as<Launcher_Entity>(&entity);
  toggle_enabled(self, self.switch_state, data.as_toggle_enabled(), context);
}

void shim_movement_modifier_entity_toggle_enabled(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Movement_Modifier_Entity& self = *entity_as<Movement_Modifier_Entity>(&entity);
  toggle_enabled(self, self.switch_state, data.as_toggle_enabled(), context);
}

void shim_weapon_emancipation_grill_entity_toggle_enabled(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Weapon_Emancipation_Grill_Entity& self = *entity_as<Weapon_Emancipation_Grill_Entity>(&entity);
  toggle_enabled(self, self.switch_state, data.as_toggle_enabled(), context);
}

void shim_sound_emitter_entity_play(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Sound_Emitter_Entity& self = *entity_as<Sound_Emitter_Entity>(&entity);
  play(self, self.playback, data.as_play(), context);
}

void shim_sound_emitter_entity_stop_playing(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Sound_Emitter_Entity& self = *entity_as<Sound_Emitter_Entity>(&entity);
  stop_playing(self, self.playback, data.as_stop_playing(), context);
}

void shim_point_light_entity_set_color(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Point_Light_Entity& self = *entity_as<Point_Light_Entity>(&entity);
  set_color(self, data.as_set_color(), context);
}

void shim_spot_light_entity_set_color(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Spot_Light_Entity& self = *entity_as<Spot_Light_Entity>(&entity);
  set_color(self, data.as_set_color(), context);
}

void shim_logic_counter_entity_add(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Logic_Counter_Entity& self = *entity_as<Logic_Counter_Entity>(&entity);
  add(self, self.counter, data.as_add(), context);
}

void shim_logic_counter_entity_reset(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Logic_Counter_Entity& self = *entity_as<Logic_Counter_Entity>(&entity);
  reset(self, self.counter, data.as_reset(), context);
}

void shim_player_entity_kill(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Player_Entity& self = *entity_as<Player_Entity>(&entity);
  kill(self, self.health, data.as_kill(), context);
}

void shim_damageable_entity_kill(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Damageable_Entity& self = *entity_as<Damageable_Entity>(&entity);
  kill(self, self.health, data.as_kill(), context);
}

void shim_player_entity_set_health(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Player_Entity& self = *entity_as<Player_Entity>(&entity);
  set_health(self, self.health, data.as_set_health(), context);
}

void shim_damageable_entity_set_health(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Damageable_Entity& self = *entity_as<Damageable_Entity>(&entity);
  set_health(self, self.health, data.as_set_health(), context);
}

void shim_player_entity_damage(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Player_Entity& self = *entity_as<Player_Entity>(&entity);
  damage(self, self.health, data.as_damage(), context);
}

void shim_damageable_entity_damage(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Damageable_Entity& self = *entity_as<Damageable_Entity>(&entity);
  damage(self, self.health, data.as_damage(), context);
}

void shim_player_entity_teleport(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Player_Entity& self = *entity_as<Player_Entity>(&entity);
  teleport(self, data.as_teleport(), context);
}

void shim_player_entity_set_velocity(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Player_Entity& self = *entity_as<Player_Entity>(&entity);
  set_velocity(self, data.as_set_velocity(), context);
}

void shim_player_entity_add_velocity(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Player_Entity& self = *entity_as<Player_Entity>(&entity);
  add_velocity(self, data.as_add_velocity(), context);
}

void shim_player_entity_grant_weapon(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Player_Entity& self = *entity_as<Player_Entity>(&entity);
  grant_weapon(self, self.inventory, data.as_grant_weapon(), context);
}

void shim_player_entity_take_weapon(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Player_Entity& self = *entity_as<Player_Entity>(&entity);
  take_weapon(self, self.inventory, data.as_take_weapon(), context);
}

void shim_player_entity_set_respawn_point(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Player_Entity& self = *entity_as<Player_Entity>(&entity);
  set_respawn_point(self, data.as_set_respawn_point(), context);
}

void shim_game_rules_entity_complete_level(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Game_Rules_Entity& self = *entity_as<Game_Rules_Entity>(&entity);
  complete_level(self, data.as_complete_level(), context);
}

void shim_logic_timer_entity_start(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Logic_Timer_Entity& self = *entity_as<Logic_Timer_Entity>(&entity);
  start(self, self.timer, data.as_start(), context);
}

void shim_logic_timer_entity_stop(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Logic_Timer_Entity& self = *entity_as<Logic_Timer_Entity>(&entity);
  stop(self, self.timer, data.as_stop(), context);
}

void shim_logic_timer_entity_restart(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Logic_Timer_Entity& self = *entity_as<Logic_Timer_Entity>(&entity);
  restart(self, self.timer, data.as_restart(), context);
}

void shim_logic_timer_entity_pause(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Logic_Timer_Entity& self = *entity_as<Logic_Timer_Entity>(&entity);
  pause(self, self.timer, data.as_pause(), context);
}

void shim_logic_timer_entity_resume(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Logic_Timer_Entity& self = *entity_as<Logic_Timer_Entity>(&entity);
  resume(self, self.timer, data.as_resume(), context);
}

void shim_game_rules_entity_start_match(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Game_Rules_Entity& self = *entity_as<Game_Rules_Entity>(&entity);
  start_match(self, self.match, data.as_start_match(), context);
}

void shim_game_rules_entity_end_round(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Game_Rules_Entity& self = *entity_as<Game_Rules_Entity>(&entity);
  end_round(self, self.match, data.as_end_round(), context);
}

void shim_game_rules_entity_restart_round(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Game_Rules_Entity& self = *entity_as<Game_Rules_Entity>(&entity);
  restart_round(self, self.match, data.as_restart_round(), context);
}

void shim_game_rules_entity_end_match(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Game_Rules_Entity& self = *entity_as<Game_Rules_Entity>(&entity);
  end_match(self, self.match, data.as_end_match(), context);
}

void shim_mover_entity_reverse(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Mover_Entity& self = *entity_as<Mover_Entity>(&entity);
  reverse(self, self.follow, data.as_reverse(), context);
}

void shim_mover_entity_go_to(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Mover_Entity& self = *entity_as<Mover_Entity>(&entity);
  go_to(self, self.follow, data.as_go_to(), context);
}

void shim_launcher_entity_fire(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Launcher_Entity& self = *entity_as<Launcher_Entity>(&entity);
  fire(self, data.as_fire(), context);
}

using action_shim_fn = void (*)(Entity&, const action_data_t&, input_context_t&);

// A non-null cell means the type accepts the action. Rows are entity
// types in tag order, columns the derived action enum.
constexpr action_shim_fn ACTION_DISPATCH[ENTITY_TYPE_COUNT][ENTITY_ACTION_COUNT] = {
  {},   // Invalid
  {},   // Player_Spawn_Entity
  {},   // Player_Spectate_Entity
  {   // Player_Entity
    nullptr,   // Enable
    nullptr,   // Disable
    nullptr,   // Toggle_Enabled
    nullptr,   // Play
    nullptr,   // Stop_Playing
    nullptr,   // Set_Color
    nullptr,   // Add
    nullptr,   // Reset
    shim_player_entity_kill,
    shim_player_entity_set_health,
    shim_player_entity_damage,
    shim_player_entity_teleport,
    shim_player_entity_set_velocity,
    shim_player_entity_add_velocity,
    shim_player_entity_grant_weapon,
    shim_player_entity_take_weapon,
    shim_player_entity_set_respawn_point,
    nullptr,   // Complete_Level
    nullptr,   // Start
    nullptr,   // Stop
    nullptr,   // Restart
    nullptr,   // Pause
    nullptr,   // Resume
    nullptr,   // Start_Match
    nullptr,   // End_Round
    nullptr,   // Restart_Round
    nullptr,   // End_Match
    nullptr,   // Reverse
    nullptr,   // Go_To
    nullptr,   // Fire
  },
  {},   // Weapon_Entity
  {},   // Rocket_Entity
  {},   // Hook_Entity
  {},   // Kooh_Entity
  {},   // Ricochet_Entity
  {},   // Platform_Entity
  {},   // Shrinking_Platform_Entity
  {},   // Canopy_Entity
  {},   // Bubble_Entity
  {},   // Physics_Body_Entity
  {   // Damageable_Entity
    nullptr,   // Enable
    nullptr,   // Disable
    nullptr,   // Toggle_Enabled
    nullptr,   // Play
    nullptr,   // Stop_Playing
    nullptr,   // Set_Color
    nullptr,   // Add
    nullptr,   // Reset
    shim_damageable_entity_kill,
    shim_damageable_entity_set_health,
    shim_damageable_entity_damage,
    nullptr,   // Teleport
    nullptr,   // Set_Velocity
    nullptr,   // Add_Velocity
    nullptr,   // Grant_Weapon
    nullptr,   // Take_Weapon
    nullptr,   // Set_Respawn_Point
    nullptr,   // Complete_Level
    nullptr,   // Start
    nullptr,   // Stop
    nullptr,   // Restart
    nullptr,   // Pause
    nullptr,   // Resume
    nullptr,   // Start_Match
    nullptr,   // End_Round
    nullptr,   // Restart_Round
    nullptr,   // End_Match
    nullptr,   // Reverse
    nullptr,   // Go_To
    nullptr,   // Fire
  },
  {},   // Particle_Emitter_Entity
  {   // Sound_Emitter_Entity
    shim_sound_emitter_entity_enable,
    shim_sound_emitter_entity_disable,
    shim_sound_emitter_entity_toggle_enabled,
    shim_sound_emitter_entity_play,
    shim_sound_emitter_entity_stop_playing,
    nullptr,   // Set_Color
    nullptr,   // Add
    nullptr,   // Reset
    nullptr,   // Kill
    nullptr,   // Set_Health
    nullptr,   // Damage
    nullptr,   // Teleport
    nullptr,   // Set_Velocity
    nullptr,   // Add_Velocity
    nullptr,   // Grant_Weapon
    nullptr,   // Take_Weapon
    nullptr,   // Set_Respawn_Point
    nullptr,   // Complete_Level
    nullptr,   // Start
    nullptr,   // Stop
    nullptr,   // Restart
    nullptr,   // Pause
    nullptr,   // Resume
    nullptr,   // Start_Match
    nullptr,   // End_Round
    nullptr,   // Restart_Round
    nullptr,   // End_Match
    nullptr,   // Reverse
    nullptr,   // Go_To
    nullptr,   // Fire
  },
  {   // Point_Light_Entity
    shim_point_light_entity_enable,
    shim_point_light_entity_disable,
    shim_point_light_entity_toggle_enabled,
    nullptr,   // Play
    nullptr,   // Stop_Playing
    shim_point_light_entity_set_color,
    nullptr,   // Add
    nullptr,   // Reset
    nullptr,   // Kill
    nullptr,   // Set_Health
    nullptr,   // Damage
    nullptr,   // Teleport
    nullptr,   // Set_Velocity
    nullptr,   // Add_Velocity
    nullptr,   // Grant_Weapon
    nullptr,   // Take_Weapon
    nullptr,   // Set_Respawn_Point
    nullptr,   // Complete_Level
    nullptr,   // Start
    nullptr,   // Stop
    nullptr,   // Restart
    nullptr,   // Pause
    nullptr,   // Resume
    nullptr,   // Start_Match
    nullptr,   // End_Round
    nullptr,   // Restart_Round
    nullptr,   // End_Match
    nullptr,   // Reverse
    nullptr,   // Go_To
    nullptr,   // Fire
  },
  {   // Spot_Light_Entity
    shim_spot_light_entity_enable,
    shim_spot_light_entity_disable,
    shim_spot_light_entity_toggle_enabled,
    nullptr,   // Play
    nullptr,   // Stop_Playing
    shim_spot_light_entity_set_color,
    nullptr,   // Add
    nullptr,   // Reset
    nullptr,   // Kill
    nullptr,   // Set_Health
    nullptr,   // Damage
    nullptr,   // Teleport
    nullptr,   // Set_Velocity
    nullptr,   // Add_Velocity
    nullptr,   // Grant_Weapon
    nullptr,   // Take_Weapon
    nullptr,   // Set_Respawn_Point
    nullptr,   // Complete_Level
    nullptr,   // Start
    nullptr,   // Stop
    nullptr,   // Restart
    nullptr,   // Pause
    nullptr,   // Resume
    nullptr,   // Start_Match
    nullptr,   // End_Round
    nullptr,   // Restart_Round
    nullptr,   // End_Match
    nullptr,   // Reverse
    nullptr,   // Go_To
    nullptr,   // Fire
  },
  {},   // Directional_Light_Entity
  {   // Trigger_Volume_Entity
    shim_trigger_volume_entity_enable,
    shim_trigger_volume_entity_disable,
    shim_trigger_volume_entity_toggle_enabled,
    nullptr,   // Play
    nullptr,   // Stop_Playing
    nullptr,   // Set_Color
    nullptr,   // Add
    nullptr,   // Reset
    nullptr,   // Kill
    nullptr,   // Set_Health
    nullptr,   // Damage
    nullptr,   // Teleport
    nullptr,   // Set_Velocity
    nullptr,   // Add_Velocity
    nullptr,   // Grant_Weapon
    nullptr,   // Take_Weapon
    nullptr,   // Set_Respawn_Point
    nullptr,   // Complete_Level
    nullptr,   // Start
    nullptr,   // Stop
    nullptr,   // Restart
    nullptr,   // Pause
    nullptr,   // Resume
    nullptr,   // Start_Match
    nullptr,   // End_Round
    nullptr,   // Restart_Round
    nullptr,   // End_Match
    nullptr,   // Reverse
    nullptr,   // Go_To
    nullptr,   // Fire
  },
  {   // Jump_Pad_Entity
    shim_jump_pad_entity_enable,
    shim_jump_pad_entity_disable,
    shim_jump_pad_entity_toggle_enabled,
    nullptr,   // Play
    nullptr,   // Stop_Playing
    nullptr,   // Set_Color
    nullptr,   // Add
    nullptr,   // Reset
    nullptr,   // Kill
    nullptr,   // Set_Health
    nullptr,   // Damage
    nullptr,   // Teleport
    nullptr,   // Set_Velocity
    nullptr,   // Add_Velocity
    nullptr,   // Grant_Weapon
    nullptr,   // Take_Weapon
    nullptr,   // Set_Respawn_Point
    nullptr,   // Complete_Level
    nullptr,   // Start
    nullptr,   // Stop
    nullptr,   // Restart
    nullptr,   // Pause
    nullptr,   // Resume
    nullptr,   // Start_Match
    nullptr,   // End_Round
    nullptr,   // Restart_Round
    nullptr,   // End_Match
    nullptr,   // Reverse
    nullptr,   // Go_To
    nullptr,   // Fire
  },
  {},   // Reflection_Volume_Entity
  {   // Game_Rules_Entity
    nullptr,   // Enable
    nullptr,   // Disable
    nullptr,   // Toggle_Enabled
    nullptr,   // Play
    nullptr,   // Stop_Playing
    nullptr,   // Set_Color
    nullptr,   // Add
    nullptr,   // Reset
    nullptr,   // Kill
    nullptr,   // Set_Health
    nullptr,   // Damage
    nullptr,   // Teleport
    nullptr,   // Set_Velocity
    nullptr,   // Add_Velocity
    nullptr,   // Grant_Weapon
    nullptr,   // Take_Weapon
    nullptr,   // Set_Respawn_Point
    shim_game_rules_entity_complete_level,
    nullptr,   // Start
    nullptr,   // Stop
    nullptr,   // Restart
    nullptr,   // Pause
    nullptr,   // Resume
    shim_game_rules_entity_start_match,
    shim_game_rules_entity_end_round,
    shim_game_rules_entity_restart_round,
    shim_game_rules_entity_end_match,
    nullptr,   // Reverse
    nullptr,   // Go_To
    nullptr,   // Fire
  },
  {   // Logic_Counter_Entity
    nullptr,   // Enable
    nullptr,   // Disable
    nullptr,   // Toggle_Enabled
    nullptr,   // Play
    nullptr,   // Stop_Playing
    nullptr,   // Set_Color
    shim_logic_counter_entity_add,
    shim_logic_counter_entity_reset,
    nullptr,   // Kill
    nullptr,   // Set_Health
    nullptr,   // Damage
    nullptr,   // Teleport
    nullptr,   // Set_Velocity
    nullptr,   // Add_Velocity
    nullptr,   // Grant_Weapon
    nullptr,   // Take_Weapon
    nullptr,   // Set_Respawn_Point
    nullptr,   // Complete_Level
    nullptr,   // Start
    nullptr,   // Stop
    nullptr,   // Restart
    nullptr,   // Pause
    nullptr,   // Resume
    nullptr,   // Start_Match
    nullptr,   // End_Round
    nullptr,   // Restart_Round
    nullptr,   // End_Match
    nullptr,   // Reverse
    nullptr,   // Go_To
    nullptr,   // Fire
  },
  {   // Geometry_Owner_Entity
    shim_geometry_owner_entity_enable,
    shim_geometry_owner_entity_disable,
    shim_geometry_owner_entity_toggle_enabled,
    nullptr,   // Play
    nullptr,   // Stop_Playing
    nullptr,   // Set_Color
    nullptr,   // Add
    nullptr,   // Reset
    nullptr,   // Kill
    nullptr,   // Set_Health
    nullptr,   // Damage
    nullptr,   // Teleport
    nullptr,   // Set_Velocity
    nullptr,   // Add_Velocity
    nullptr,   // Grant_Weapon
    nullptr,   // Take_Weapon
    nullptr,   // Set_Respawn_Point
    nullptr,   // Complete_Level
    nullptr,   // Start
    nullptr,   // Stop
    nullptr,   // Restart
    nullptr,   // Pause
    nullptr,   // Resume
    nullptr,   // Start_Match
    nullptr,   // End_Round
    nullptr,   // Restart_Round
    nullptr,   // End_Match
    nullptr,   // Reverse
    nullptr,   // Go_To
    nullptr,   // Fire
  },
  {},   // Ping_Marker_Entity
  {   // Logic_Timer_Entity
    nullptr,   // Enable
    nullptr,   // Disable
    nullptr,   // Toggle_Enabled
    nullptr,   // Play
    nullptr,   // Stop_Playing
    nullptr,   // Set_Color
    nullptr,   // Add
    nullptr,   // Reset
    nullptr,   // Kill
    nullptr,   // Set_Health
    nullptr,   // Damage
    nullptr,   // Teleport
    nullptr,   // Set_Velocity
    nullptr,   // Add_Velocity
    nullptr,   // Grant_Weapon
    nullptr,   // Take_Weapon
    nullptr,   // Set_Respawn_Point
    nullptr,   // Complete_Level
    shim_logic_timer_entity_start,
    shim_logic_timer_entity_stop,
    shim_logic_timer_entity_restart,
    shim_logic_timer_entity_pause,
    shim_logic_timer_entity_resume,
    nullptr,   // Start_Match
    nullptr,   // End_Round
    nullptr,   // Restart_Round
    nullptr,   // End_Match
    nullptr,   // Reverse
    nullptr,   // Go_To
    nullptr,   // Fire
  },
  {},   // Path_Node_Entity
  {   // Mover_Entity
    shim_mover_entity_enable,
    shim_mover_entity_disable,
    shim_mover_entity_toggle_enabled,
    nullptr,   // Play
    nullptr,   // Stop_Playing
    nullptr,   // Set_Color
    nullptr,   // Add
    nullptr,   // Reset
    nullptr,   // Kill
    nullptr,   // Set_Health
    nullptr,   // Damage
    nullptr,   // Teleport
    nullptr,   // Set_Velocity
    nullptr,   // Add_Velocity
    nullptr,   // Grant_Weapon
    nullptr,   // Take_Weapon
    nullptr,   // Set_Respawn_Point
    nullptr,   // Complete_Level
    nullptr,   // Start
    nullptr,   // Stop
    nullptr,   // Restart
    nullptr,   // Pause
    nullptr,   // Resume
    nullptr,   // Start_Match
    nullptr,   // End_Round
    nullptr,   // Restart_Round
    nullptr,   // End_Match
    shim_mover_entity_reverse,
    shim_mover_entity_go_to,
    nullptr,   // Fire
  },
  {   // Launcher_Entity
    shim_launcher_entity_enable,
    shim_launcher_entity_disable,
    shim_launcher_entity_toggle_enabled,
    nullptr,   // Play
    nullptr,   // Stop_Playing
    nullptr,   // Set_Color
    nullptr,   // Add
    nullptr,   // Reset
    nullptr,   // Kill
    nullptr,   // Set_Health
    nullptr,   // Damage
    nullptr,   // Teleport
    nullptr,   // Set_Velocity
    nullptr,   // Add_Velocity
    nullptr,   // Grant_Weapon
    nullptr,   // Take_Weapon
    nullptr,   // Set_Respawn_Point
    nullptr,   // Complete_Level
    nullptr,   // Start
    nullptr,   // Stop
    nullptr,   // Restart
    nullptr,   // Pause
    nullptr,   // Resume
    nullptr,   // Start_Match
    nullptr,   // End_Round
    nullptr,   // Restart_Round
    nullptr,   // End_Match
    nullptr,   // Reverse
    nullptr,   // Go_To
    shim_launcher_entity_fire,
  },
  {   // Movement_Modifier_Entity
    shim_movement_modifier_entity_enable,
    shim_movement_modifier_entity_disable,
    shim_movement_modifier_entity_toggle_enabled,
    nullptr,   // Play
    nullptr,   // Stop_Playing
    nullptr,   // Set_Color
    nullptr,   // Add
    nullptr,   // Reset
    nullptr,   // Kill
    nullptr,   // Set_Health
    nullptr,   // Damage
    nullptr,   // Teleport
    nullptr,   // Set_Velocity
    nullptr,   // Add_Velocity
    nullptr,   // Grant_Weapon
    nullptr,   // Take_Weapon
    nullptr,   // Set_Respawn_Point
    nullptr,   // Complete_Level
    nullptr,   // Start
    nullptr,   // Stop
    nullptr,   // Restart
    nullptr,   // Pause
    nullptr,   // Resume
    nullptr,   // Start_Match
    nullptr,   // End_Round
    nullptr,   // Restart_Round
    nullptr,   // End_Match
    nullptr,   // Reverse
    nullptr,   // Go_To
    nullptr,   // Fire
  },
  {},   // Remnant_Entity
  {},   // Modifier_Shot_Entity
  {},   // Timed_Movement_Modifier_Entity
  {   // Weapon_Emancipation_Grill_Entity
    shim_weapon_emancipation_grill_entity_enable,
    shim_weapon_emancipation_grill_entity_disable,
    shim_weapon_emancipation_grill_entity_toggle_enabled,
    nullptr,   // Play
    nullptr,   // Stop_Playing
    nullptr,   // Set_Color
    nullptr,   // Add
    nullptr,   // Reset
    nullptr,   // Kill
    nullptr,   // Set_Health
    nullptr,   // Damage
    nullptr,   // Teleport
    nullptr,   // Set_Velocity
    nullptr,   // Add_Velocity
    nullptr,   // Grant_Weapon
    nullptr,   // Take_Weapon
    nullptr,   // Set_Respawn_Point
    nullptr,   // Complete_Level
    nullptr,   // Start
    nullptr,   // Stop
    nullptr,   // Restart
    nullptr,   // Pause
    nullptr,   // Resume
    nullptr,   // Start_Match
    nullptr,   // End_Round
    nullptr,   // Restart_Round
    nullptr,   // End_Match
    nullptr,   // Reverse
    nullptr,   // Go_To
    nullptr,   // Fire
  },
  {},   // Emancipated_Weapon_Entity
};

// The shared ACCEPTANCE mask and this table are two artifacts of one
// declaration, and this is what makes that a proof rather than an
// intention: a loader that refuses a connection whose cell is null must
// agree with the drain that calls through it, or the drain's fatal_error
// on a null cell would be reachable from a map the loader accepted.
constexpr bool dispatch_matches_acceptance()
{
  for (uint32_t type = 0; type < ENTITY_TYPE_COUNT; ++type)
    for (uint32_t action = 0; action < ENTITY_ACTION_COUNT; ++action)
    {
      const bool accepted = (ACTION_ACCEPTED_MASKS[type] & (1ull << action)) != 0;
      if (accepted != (ACTION_DISPATCH[type][action] != nullptr))
        return false;
    }
  return true;
}

static_assert(dispatch_matches_acceptance(),
              "the shared acceptance mask and this dispatch table disagree");

} // namespace

bool try_enable(Entity& entity, const Enable_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Enable];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void enable(Entity& entity, const Enable_Data& payload, input_context_t& context)
{
  if (!try_enable(entity, payload, context))
    fatal_error("{} does not accept Enable", entity_info(entity.type).classname);
}

bool try_disable(Entity& entity, const Disable_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Disable];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void disable(Entity& entity, const Disable_Data& payload, input_context_t& context)
{
  if (!try_disable(entity, payload, context))
    fatal_error("{} does not accept Disable", entity_info(entity.type).classname);
}

bool try_toggle_enabled(Entity& entity, const Toggle_Enabled_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Toggle_Enabled];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void toggle_enabled(Entity& entity, const Toggle_Enabled_Data& payload, input_context_t& context)
{
  if (!try_toggle_enabled(entity, payload, context))
    fatal_error("{} does not accept Toggle_Enabled", entity_info(entity.type).classname);
}

bool try_play(Entity& entity, const Play_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Play];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void play(Entity& entity, const Play_Data& payload, input_context_t& context)
{
  if (!try_play(entity, payload, context))
    fatal_error("{} does not accept Play", entity_info(entity.type).classname);
}

bool try_stop_playing(Entity& entity, const Stop_Playing_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Stop_Playing];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void stop_playing(Entity& entity, const Stop_Playing_Data& payload, input_context_t& context)
{
  if (!try_stop_playing(entity, payload, context))
    fatal_error("{} does not accept Stop_Playing", entity_info(entity.type).classname);
}

bool try_set_color(Entity& entity, const Set_Color_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Set_Color];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void set_color(Entity& entity, const Set_Color_Data& payload, input_context_t& context)
{
  if (!try_set_color(entity, payload, context))
    fatal_error("{} does not accept Set_Color", entity_info(entity.type).classname);
}

bool try_add(Entity& entity, const Add_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Add];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void add(Entity& entity, const Add_Data& payload, input_context_t& context)
{
  if (!try_add(entity, payload, context))
    fatal_error("{} does not accept Add", entity_info(entity.type).classname);
}

bool try_reset(Entity& entity, const Reset_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Reset];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void reset(Entity& entity, const Reset_Data& payload, input_context_t& context)
{
  if (!try_reset(entity, payload, context))
    fatal_error("{} does not accept Reset", entity_info(entity.type).classname);
}

bool try_kill(Entity& entity, const Kill_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Kill];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void kill(Entity& entity, const Kill_Data& payload, input_context_t& context)
{
  if (!try_kill(entity, payload, context))
    fatal_error("{} does not accept Kill", entity_info(entity.type).classname);
}

bool try_set_health(Entity& entity, const Set_Health_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Set_Health];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void set_health(Entity& entity, const Set_Health_Data& payload, input_context_t& context)
{
  if (!try_set_health(entity, payload, context))
    fatal_error("{} does not accept Set_Health", entity_info(entity.type).classname);
}

bool try_damage(Entity& entity, const Damage_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Damage];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void damage(Entity& entity, const Damage_Data& payload, input_context_t& context)
{
  if (!try_damage(entity, payload, context))
    fatal_error("{} does not accept Damage", entity_info(entity.type).classname);
}

bool try_teleport(Entity& entity, const Teleport_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Teleport];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void teleport(Entity& entity, const Teleport_Data& payload, input_context_t& context)
{
  if (!try_teleport(entity, payload, context))
    fatal_error("{} does not accept Teleport", entity_info(entity.type).classname);
}

bool try_set_velocity(Entity& entity, const Set_Velocity_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Set_Velocity];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void set_velocity(Entity& entity, const Set_Velocity_Data& payload, input_context_t& context)
{
  if (!try_set_velocity(entity, payload, context))
    fatal_error("{} does not accept Set_Velocity", entity_info(entity.type).classname);
}

bool try_add_velocity(Entity& entity, const Add_Velocity_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Add_Velocity];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void add_velocity(Entity& entity, const Add_Velocity_Data& payload, input_context_t& context)
{
  if (!try_add_velocity(entity, payload, context))
    fatal_error("{} does not accept Add_Velocity", entity_info(entity.type).classname);
}

bool try_grant_weapon(Entity& entity, const Grant_Weapon_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Grant_Weapon];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void grant_weapon(Entity& entity, const Grant_Weapon_Data& payload, input_context_t& context)
{
  if (!try_grant_weapon(entity, payload, context))
    fatal_error("{} does not accept Grant_Weapon", entity_info(entity.type).classname);
}

bool try_take_weapon(Entity& entity, const Take_Weapon_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Take_Weapon];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void take_weapon(Entity& entity, const Take_Weapon_Data& payload, input_context_t& context)
{
  if (!try_take_weapon(entity, payload, context))
    fatal_error("{} does not accept Take_Weapon", entity_info(entity.type).classname);
}

bool try_set_respawn_point(Entity& entity, const Set_Respawn_Point_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Set_Respawn_Point];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void set_respawn_point(Entity& entity, const Set_Respawn_Point_Data& payload, input_context_t& context)
{
  if (!try_set_respawn_point(entity, payload, context))
    fatal_error("{} does not accept Set_Respawn_Point", entity_info(entity.type).classname);
}

bool try_complete_level(Entity& entity, const Complete_Level_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Complete_Level];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void complete_level(Entity& entity, const Complete_Level_Data& payload, input_context_t& context)
{
  if (!try_complete_level(entity, payload, context))
    fatal_error("{} does not accept Complete_Level", entity_info(entity.type).classname);
}

bool try_start(Entity& entity, const Start_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Start];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void start(Entity& entity, const Start_Data& payload, input_context_t& context)
{
  if (!try_start(entity, payload, context))
    fatal_error("{} does not accept Start", entity_info(entity.type).classname);
}

bool try_stop(Entity& entity, const Stop_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Stop];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void stop(Entity& entity, const Stop_Data& payload, input_context_t& context)
{
  if (!try_stop(entity, payload, context))
    fatal_error("{} does not accept Stop", entity_info(entity.type).classname);
}

bool try_restart(Entity& entity, const Restart_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Restart];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void restart(Entity& entity, const Restart_Data& payload, input_context_t& context)
{
  if (!try_restart(entity, payload, context))
    fatal_error("{} does not accept Restart", entity_info(entity.type).classname);
}

bool try_pause(Entity& entity, const Pause_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Pause];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void pause(Entity& entity, const Pause_Data& payload, input_context_t& context)
{
  if (!try_pause(entity, payload, context))
    fatal_error("{} does not accept Pause", entity_info(entity.type).classname);
}

bool try_resume(Entity& entity, const Resume_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Resume];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void resume(Entity& entity, const Resume_Data& payload, input_context_t& context)
{
  if (!try_resume(entity, payload, context))
    fatal_error("{} does not accept Resume", entity_info(entity.type).classname);
}

bool try_start_match(Entity& entity, const Start_Match_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Start_Match];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void start_match(Entity& entity, const Start_Match_Data& payload, input_context_t& context)
{
  if (!try_start_match(entity, payload, context))
    fatal_error("{} does not accept Start_Match", entity_info(entity.type).classname);
}

bool try_end_round(Entity& entity, const End_Round_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::End_Round];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void end_round(Entity& entity, const End_Round_Data& payload, input_context_t& context)
{
  if (!try_end_round(entity, payload, context))
    fatal_error("{} does not accept End_Round", entity_info(entity.type).classname);
}

bool try_restart_round(Entity& entity, const Restart_Round_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Restart_Round];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void restart_round(Entity& entity, const Restart_Round_Data& payload, input_context_t& context)
{
  if (!try_restart_round(entity, payload, context))
    fatal_error("{} does not accept Restart_Round", entity_info(entity.type).classname);
}

bool try_end_match(Entity& entity, const End_Match_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::End_Match];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void end_match(Entity& entity, const End_Match_Data& payload, input_context_t& context)
{
  if (!try_end_match(entity, payload, context))
    fatal_error("{} does not accept End_Match", entity_info(entity.type).classname);
}

bool try_reverse(Entity& entity, const Reverse_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Reverse];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void reverse(Entity& entity, const Reverse_Data& payload, input_context_t& context)
{
  if (!try_reverse(entity, payload, context))
    fatal_error("{} does not accept Reverse", entity_info(entity.type).classname);
}

bool try_go_to(Entity& entity, const Go_To_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Go_To];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void go_to(Entity& entity, const Go_To_Data& payload, input_context_t& context)
{
  if (!try_go_to(entity, payload, context))
    fatal_error("{} does not accept Go_To", entity_info(entity.type).classname);
}

bool try_fire(Entity& entity, const Fire_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Fire];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void fire(Entity& entity, const Fire_Data& payload, input_context_t& context)
{
  if (!try_fire(entity, payload, context))
    fatal_error("{} does not accept Fire", entity_info(entity.type).classname);
}

bool try_send_action(Entity& target, const action_data_t& data, input_context_t& context)
{
  if (target.type <= entity_type::Invalid || (uint32_t)target.type >= ENTITY_TYPE_COUNT)
    return false;
  if ((uint32_t)data.tag >= ENTITY_ACTION_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)target.type][(uint16_t)data.tag];
  if (shim == nullptr)
    return false;
  shim(target, data, context);
  return true;
}

// The drain's entry point. Reaching a null cell here is a generator or
// loader bug and never the map's: the loader refused every connection
// whose cell was null, against the very mask this table is checked
// against above.
void send_action(Entity& target, const action_data_t& data, input_context_t& context)
{
  if (!try_send_action(target, data, context))
    fatal_error("{} does not accept {}", entity_info(target.type).classname,
                to_string(data.tag));
}

// One emit per signal. A sender that does not declare the signal is a
// CODE bug, not a map's -- the loader refuses a connection whose sender
// does not emit it -- so this is fatal rather than a quiet return.
void emit_color_changed(const Entity& sender, const Color_Changed_Data& payload, input_context_t& context)
{
  if (!type_emits_signal(sender.type, entity_signal::Color_Changed))
    fatal_error("{} does not emit Color_Changed", entity_info(sender.type).classname);
  server::queue_signal_connections(context, sender, entity_signal::Color_Changed,
                                   &payload, (uint32_t)sizeof(payload));
}

void emit_limit_reached(const Entity& sender, const Limit_Reached_Data& payload, input_context_t& context)
{
  if (!type_emits_signal(sender.type, entity_signal::Limit_Reached))
    fatal_error("{} does not emit Limit_Reached", entity_info(sender.type).classname);
  server::queue_signal_connections(context, sender, entity_signal::Limit_Reached,
                                   &payload, (uint32_t)sizeof(payload));
}

void emit_fell_below_limit(const Entity& sender, const Fell_Below_Limit_Data& payload, input_context_t& context)
{
  if (!type_emits_signal(sender.type, entity_signal::Fell_Below_Limit))
    fatal_error("{} does not emit Fell_Below_Limit", entity_info(sender.type).classname);
  server::queue_signal_connections(context, sender, entity_signal::Fell_Below_Limit,
                                   &payload, (uint32_t)sizeof(payload));
}

void emit_touched(const Entity& sender, const Touched_Data& payload, input_context_t& context)
{
  if (!type_emits_signal(sender.type, entity_signal::Touched))
    fatal_error("{} does not emit Touched", entity_info(sender.type).classname);
  server::queue_signal_connections(context, sender, entity_signal::Touched,
                                   &payload, (uint32_t)sizeof(payload));
}

void emit_left(const Entity& sender, const Left_Data& payload, input_context_t& context)
{
  if (!type_emits_signal(sender.type, entity_signal::Left))
    fatal_error("{} does not emit Left", entity_info(sender.type).classname);
  server::queue_signal_connections(context, sender, entity_signal::Left,
                                   &payload, (uint32_t)sizeof(payload));
}

void emit_died(const Entity& sender, const Died_Data& payload, input_context_t& context)
{
  if (!type_emits_signal(sender.type, entity_signal::Died))
    fatal_error("{} does not emit Died", entity_info(sender.type).classname);
  server::queue_signal_connections(context, sender, entity_signal::Died,
                                   &payload, (uint32_t)sizeof(payload));
}

void emit_health_changed(const Entity& sender, const Health_Changed_Data& payload, input_context_t& context)
{
  if (!type_emits_signal(sender.type, entity_signal::Health_Changed))
    fatal_error("{} does not emit Health_Changed", entity_info(sender.type).classname);
  server::queue_signal_connections(context, sender, entity_signal::Health_Changed,
                                   &payload, (uint32_t)sizeof(payload));
}

void emit_elapsed(const Entity& sender, const Elapsed_Data& payload, input_context_t& context)
{
  if (!type_emits_signal(sender.type, entity_signal::Elapsed))
    fatal_error("{} does not emit Elapsed", entity_info(sender.type).classname);
  server::queue_signal_connections(context, sender, entity_signal::Elapsed,
                                   &payload, (uint32_t)sizeof(payload));
}

void emit_match_started(const Entity& sender, const Match_Started_Data& payload, input_context_t& context)
{
  if (!type_emits_signal(sender.type, entity_signal::Match_Started))
    fatal_error("{} does not emit Match_Started", entity_info(sender.type).classname);
  server::queue_signal_connections(context, sender, entity_signal::Match_Started,
                                   &payload, (uint32_t)sizeof(payload));
}

void emit_round_started(const Entity& sender, const Round_Started_Data& payload, input_context_t& context)
{
  if (!type_emits_signal(sender.type, entity_signal::Round_Started))
    fatal_error("{} does not emit Round_Started", entity_info(sender.type).classname);
  server::queue_signal_connections(context, sender, entity_signal::Round_Started,
                                   &payload, (uint32_t)sizeof(payload));
}

void emit_round_ended(const Entity& sender, const Round_Ended_Data& payload, input_context_t& context)
{
  if (!type_emits_signal(sender.type, entity_signal::Round_Ended))
    fatal_error("{} does not emit Round_Ended", entity_info(sender.type).classname);
  server::queue_signal_connections(context, sender, entity_signal::Round_Ended,
                                   &payload, (uint32_t)sizeof(payload));
}

void emit_match_ended(const Entity& sender, const Match_Ended_Data& payload, input_context_t& context)
{
  if (!type_emits_signal(sender.type, entity_signal::Match_Ended))
    fatal_error("{} does not emit Match_Ended", entity_info(sender.type).classname);
  server::queue_signal_connections(context, sender, entity_signal::Match_Ended,
                                   &payload, (uint32_t)sizeof(payload));
}

void emit_node_reached(const Entity& sender, const Node_Reached_Data& payload, input_context_t& context)
{
  if (!type_emits_signal(sender.type, entity_signal::Node_Reached))
    fatal_error("{} does not emit Node_Reached", entity_info(sender.type).classname);
  server::queue_signal_connections(context, sender, entity_signal::Node_Reached,
                                   &payload, (uint32_t)sizeof(payload));
}

} // namespace entities
