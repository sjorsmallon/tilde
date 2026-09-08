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

void shim_trigger_volume_entity_enable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Trigger_Volume_Entity& self = *entity_as<Trigger_Volume_Entity>(&entity);
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

void shim_trigger_volume_entity_disable(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Trigger_Volume_Entity& self = *entity_as<Trigger_Volume_Entity>(&entity);
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

void shim_trigger_volume_entity_toggle_enabled(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Trigger_Volume_Entity& self = *entity_as<Trigger_Volume_Entity>(&entity);
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

void shim_damageable_entity_kill(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Damageable_Entity& self = *entity_as<Damageable_Entity>(&entity);
  kill(self, self.health, data.as_kill(), context);
}

void shim_damageable_entity_set_health(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Damageable_Entity& self = *entity_as<Damageable_Entity>(&entity);
  set_health(self, self.health, data.as_set_health(), context);
}

void shim_damageable_entity_damage(Entity& entity, const action_data_t& data, input_context_t& context)
{
  Damageable_Entity& self = *entity_as<Damageable_Entity>(&entity);
  damage(self, self.health, data.as_damage(), context);
}

using action_shim_fn = void (*)(Entity&, const action_data_t&, input_context_t&);

// A non-null cell means the type accepts the action. Rows are entity
// types in tag order, columns the derived action enum.
constexpr action_shim_fn ACTION_DISPATCH[ENTITY_TYPE_COUNT][ENTITY_ACTION_COUNT] = {
  {},   // Invalid
  {},   // Reflection_Volume_Entity
  {},   // Player_Spawn_Entity
  {},   // Player_Spectate_Entity
  {},   // Player_Entity
  {},   // Weapon_Entity
  {},   // Rocket_Entity
  {},   // Particle_Emitter_Entity
  {   // Damageable_Entity
    nullptr,   // Use
    nullptr,   // Enable
    nullptr,   // Disable
    nullptr,   // Toggle_Enabled
    nullptr,   // Set_Color
    shim_damageable_entity_kill,
    shim_damageable_entity_set_health,
    shim_damageable_entity_damage,
  },
  {   // Trigger_Volume_Entity
    nullptr,   // Use
    shim_trigger_volume_entity_enable,
    shim_trigger_volume_entity_disable,
    shim_trigger_volume_entity_toggle_enabled,
    nullptr,   // Set_Color
    nullptr,   // Kill
    nullptr,   // Set_Health
    nullptr,   // Damage
  },
  {   // Point_Light_Entity
    nullptr,   // Use
    shim_point_light_entity_enable,
    shim_point_light_entity_disable,
    shim_point_light_entity_toggle_enabled,
    shim_point_light_entity_set_color,
    nullptr,   // Kill
    nullptr,   // Set_Health
    nullptr,   // Damage
  },
  {   // Spot_Light_Entity
    nullptr,   // Use
    shim_spot_light_entity_enable,
    shim_spot_light_entity_disable,
    shim_spot_light_entity_toggle_enabled,
    shim_spot_light_entity_set_color,
    nullptr,   // Kill
    nullptr,   // Set_Health
    nullptr,   // Damage
  },
  {},   // Directional_Light_Entity
  {},   // Physics_Body_Entity
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

bool try_use(Entity& entity, const Use_Data& payload, input_context_t& context)
{
  if (entity.type <= entity_type::Invalid || (uint32_t)entity.type >= ENTITY_TYPE_COUNT)
    return false;
  const action_shim_fn shim = ACTION_DISPATCH[(uint16_t)entity.type][(uint16_t)entity_action::Use];
  if (shim == nullptr)
    return false;
  shim(entity, erase(payload), context);
  return true;
}

void use(Entity& entity, const Use_Data& payload, input_context_t& context)
{
  if (!try_use(entity, payload, context))
    fatal_error("{} does not accept Use", entity_info(entity.type).classname);
}

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

} // namespace entities
