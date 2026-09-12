#include "map_connection.hpp"

#include "map.hpp"

#include <cstring>
#include <format>

namespace shared
{

const char* to_string(connection_target_t kind)
{
  switch (kind)
  {
  case connection_target_t::Uid: return "Uid";
  case connection_target_t::Activator: return "Activator";
  case connection_target_t::Self: return "Self";
  case connection_target_t::Unbound: return "Unbound";
  }
  return "<unknown>";
}

std::optional<connection_target_t> try_connection_target_from_text(std::string_view text)
{
  if (text == "Uid")
    return connection_target_t::Uid;
  if (text == "Activator")
    return connection_target_t::Activator;
  if (text == "Self")
    return connection_target_t::Self;
  if (text == "Unbound")
    return connection_target_t::Unbound;
  return std::nullopt;
}

bool signal_payload_passes_through(entities::entity_signal signal, entities::entity_action action)
{
  if (entities::signal_payload_size(signal) != entities::action_payload_size(action))
    return false;

  const Span<const field_info_t> from = entities::signal_payload_fields(signal);
  const Span<const field_info_t> into = entities::action_payload_fields(action);
  if (from.size() != into.size())
    return false;

  for (size_t index = 0; index < from.size(); ++index)
  {
    const field_info_t& left  = from[index];
    const field_info_t& right = into[index];
    if (left.type != right.type || left.offset != right.offset ||
        left.size_in_bytes != right.size_in_bytes ||
        left.string_capacity != right.string_capacity || left.enum_info != right.enum_info)
      return false;
    if (std::string_view(left.name) != std::string_view(right.name))
      return false;
  }
  return true;
}

bool connections_equal(const connection_t& left, const connection_t& right)
{
  if (left.sender != right.sender || left.signal != right.signal ||
      left.target_kind != right.target_kind || left.target != right.target ||
      left.has_override != right.has_override || left.delay_seconds != right.delay_seconds ||
      left.fire_once != right.fire_once || left.data.tag != right.data.tag)
    return false;

  const uint32_t payload_size = entities::action_payload_size(left.data.tag);
  return std::memcmp(entities::action_payload_bytes(left.data),
                     entities::action_payload_bytes(right.data), payload_size) == 0;
}

std::string describe_map_entity(const map_t& map, entity_uid_t uid)
{
  const map_entity_t* entry = map.find_by_uid(uid);
  if (entry == nullptr || !entry->entity)
    return std::format("uid {}", uid);
  if (entry->entity->name.length == 0)
    return std::format("{} uid {}", entities::classname_of(entry->entity.get()), uid);
  return std::format("\"{}\" ({} uid {})", entry->entity->name.c_str(),
                     entities::classname_of(entry->entity.get()), uid);
}

namespace
{

std::string describe(const map_t& map, entity_uid_t uid)
{
  return describe_map_entity(map, uid);
}

const entities::Entity* entity_at(const map_t& map, entity_uid_t uid)
{
  const map_entity_t* entry = map.find_by_uid(uid);
  return entry != nullptr ? entry->entity.get() : nullptr;
}

} // namespace

std::vector<connection_refusal_t> validate_map_connections(const map_t& map)
{
  std::vector<connection_refusal_t> refusals;

  for (size_t index = 0; index < map.connections.size(); ++index)
  {
    const connection_t& connection = map.connections[index];

    const auto refuse = [&](std::string reason)
    { refusals.push_back({index, std::move(reason)}); };

    const entities::Entity* sender = entity_at(map, connection.sender);
    if (sender == nullptr)
    {
      refuse(std::format("sender uid {} names no entity in this map", connection.sender));
      continue;
    }

    if (!entities::type_emits_signal(sender->type, connection.signal))
    {
      refuse(std::format("{} does not emit {}", describe(map, connection.sender),
                         entities::to_string(connection.signal)));
      continue;
    }

    // Whether the receiver accepts the action. A Uid and a Self name ONE type,
    // so those two are exact. An Activator does not: it resolves to a different
    // entity every time it fires, and the `by` list is every type that could be
    // it.
    //
    // SOME of them must accept, not all of them, and that asymmetry is the
    // decision. All-accept sounds stronger and is unusable: `Touchable` is
    // truthfully activated by a player OR a physics body, a crate is not
    // Mortal and never will be, so "kill whoever touched this" -- the most
    // ordinary trigger in any level -- could not be spelled. Worse, it made a
    // `by` list unwidenable: adding a type would refuse every !activator row in
    // every map already on disk.
    //
    // What some-accept gives up is that a row is no longer proof that every
    // activation does something. The drain absorbs the rest -- an activator
    // that does not accept is a logged miss, not a fatal -- which is why the
    // queue record remembers that it came from an Activator row.
    const char* action_name = entities::to_string(connection.data.tag);

    switch (connection.target_kind)
    {
    case connection_target_t::Uid:
    {
      const entities::Entity* target = entity_at(map, connection.target);
      if (target == nullptr)
      {
        refuse(std::format("target uid {} names no entity in this map", connection.target));
        continue;
      }
      if (!entities::type_accepts_action(target->type, connection.data.tag))
      {
        refuse(std::format("{} does not accept {}", describe(map, connection.target), action_name));
        continue;
      }
      break;
    }

    case connection_target_t::Unbound:
      refuse(std::format("{} target is unbound -- pick one in the editor", action_name));
      continue;

    case connection_target_t::Self:
      if (!entities::type_accepts_action(sender->type, connection.data.tag))
      {
        refuse(std::format("{} targets itself with {}, which it does not accept",
                           describe(map, connection.sender), action_name));
        continue;
      }
      break;

    case connection_target_t::Activator:
    {
      const uint64_t activators = entities::SIGNAL_ACTIVATOR_MASKS[(uint16_t)connection.signal];
      if (activators == 0)
      {
        refuse(std::format("{} declares no `by` types, so nothing can target its activator",
                           entities::to_string(connection.signal)));
        continue;
      }

      bool any_activator_accepts = false;
      for (uint32_t type = 0; type < entities::ENTITY_TYPE_COUNT; ++type)
      {
        if ((activators & (1ull << type)) == 0)
          continue;
        any_activator_accepts =
            any_activator_accepts ||
            entities::type_accepts_action((entities::entity_type)type, connection.data.tag);
      }

      if (!any_activator_accepts)
      {
        refuse(std::format("nothing that can activate {} accepts {}, so this row could never do "
                           "anything",
                           entities::to_string(connection.signal), action_name));
        continue;
      }
      break;
    }
    }

    if (!connection.has_override && !signal_payload_passes_through(connection.signal, connection.data.tag))
      refuse(std::format("{} cannot pass its payload through to {} -- the two do not have the "
                         "same parameters, so this connection needs an override",
                         entities::to_string(connection.signal), action_name));
  }

  return refusals;
}

connection_remap_result_t remap_connection_uids(connection_t& connection, const uid_remap_t& remap)
{
  const auto follow = [&remap](entity_uid_t uid, entity_uid_t& out) -> bool
  {
    auto it = remap.find(uid);
    if (it == remap.end())
      return false;
    out = it->second;
    return true;
  };

  // Written into a copy and committed only once every end has mapped, so a
  // refused row is left exactly as it was and the caller has nothing to undo.
  connection_t rewritten = connection;

  if (!follow(connection.sender, rewritten.sender))
    return {false, "sender", connection.sender};

  if (connection.target_kind == connection_target_t::Uid &&
      !follow(connection.target, rewritten.target))
    return {false, "target", connection.target};

  if (connection.has_override)
  {
    uint8_t* payload = entities::action_payload_bytes(rewritten.data);

    for (const field_info_t& field : entities::action_payload_fields(rewritten.data.tag))
    {
      if (field.type != FIELD_TYPE_ENTITY_UID)
        continue;

      entity_uid_t named = null_entity_uid;
      std::memcpy(&named, payload + field.offset, sizeof(named));

      // A payload uid may legitimately name NOBODY -- Died.killer on a fall, a
      // Teleport with no destination yet. That is a value, not an end that
      // failed to survive, so it passes through rather than dropping the row.
      if (named == null_entity_uid)
        continue;

      entity_uid_t moved = null_entity_uid;
      if (!follow(named, moved))
        return {false, field.name, named};

      std::memcpy(payload + field.offset, &moved, sizeof(moved));
    }
  }

  connection = rewritten;
  return {};
}

} // namespace shared
