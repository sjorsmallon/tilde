#include "map_connection.hpp"

#include "map.hpp"

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

namespace
{

// "front_door (uid 42)" when the author labelled it, "uid 42" when they did
// not. What makes a load error readable is the label, which is the whole
// reason `name` sits on the base Entity.
std::string describe(const map_t& map, entity_uid_t uid)
{
  const map_entity_t* entry = map.find_by_uid(uid);
  if (entry == nullptr || !entry->entity)
    return std::format("uid {}", uid);
  if (entry->entity->name.length == 0)
    return std::format("{} uid {}", entities::classname_of(entry->entity.get()), uid);
  return std::format("\"{}\" ({} uid {})", entry->entity->name.c_str(),
                     entities::classname_of(entry->entity.get()), uid);
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

} // namespace shared
