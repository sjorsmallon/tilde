// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/events/events.def by def_gen. Do not edit.
#include "events/generated/events_generated.hpp"

#include "log.hpp"
#include "network/field_codec.hpp"

#include <cassert>
#include <cstddef>
#include <format>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#elif defined(_MSC_VER)
#pragma warning(disable : 4841)
#endif

namespace shared
{

namespace
{

constexpr const char* ROUND_PHASE_VALUE_NAMES[] = {
  "Warmup",
  "Countdown",
  "Live",
  "Round_End",
  "Game_Over",
};

constexpr enum_type_info_t ENUM_INFOS[] = {
  {"Round_Phase", {ROUND_PHASE_VALUE_NAMES, 5}},
};

constexpr field_info_t ROCKET_DETONATED_FIELDS[] = {
  {.name = "attacker_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Rocket_Detonated, attacker_id),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Detonated::attacker_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "victim_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Rocket_Detonated, victim_id),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Detonated::victim_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "weapon_id",
   .type = FIELD_TYPE_U16,
   .offset = (uint32_t)offsetof(Rocket_Detonated, weapon_id),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Detonated::weapon_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t PLAYER_DIED_FIELDS[] = {
  {.name = "victim_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Player_Died, victim_id),
   .size_in_bytes = (uint32_t)sizeof(Player_Died::victim_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "attacker_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Player_Died, attacker_id),
   .size_in_bytes = (uint32_t)sizeof(Player_Died::attacker_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "weapon_id",
   .type = FIELD_TYPE_U16,
   .offset = (uint32_t)offsetof(Player_Died, weapon_id),
   .size_in_bytes = (uint32_t)sizeof(Player_Died::weapon_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "was_headshot",
   .type = FIELD_TYPE_BOOL,
   .offset = (uint32_t)offsetof(Player_Died, was_headshot),
   .size_in_bytes = (uint32_t)sizeof(Player_Died::was_headshot),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t PLAYER_SPAWNED_FIELDS[] = {
  {.name = "player_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Player_Spawned, player_id),
   .size_in_bytes = (uint32_t)sizeof(Player_Spawned::player_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "spawn_position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Player_Spawned, spawn_position),
   .size_in_bytes = (uint32_t)sizeof(Player_Spawned::spawn_position),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "spawn_orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Player_Spawned, spawn_orientation),
   .size_in_bytes = (uint32_t)sizeof(Player_Spawned::spawn_orientation),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t ROUND_PHASE_CHANGED_FIELDS[] = {
  {.name = "phase",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Round_Phase_Changed, phase),
   .size_in_bytes = (uint32_t)sizeof(Round_Phase_Changed::phase),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &ENUM_INFOS[0]},
  {.name = "round_number",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Round_Phase_Changed, round_number),
   .size_in_bytes = (uint32_t)sizeof(Round_Phase_Changed::round_number),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "phase_end_tick",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Round_Phase_Changed, phase_end_tick),
   .size_in_bytes = (uint32_t)sizeof(Round_Phase_Changed::phase_end_tick),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

} // namespace

const char* to_string(Round_Phase value)
{
  switch (value)
  {
    case Round_Phase::Warmup: return "Warmup";
    case Round_Phase::Countdown: return "Countdown";
    case Round_Phase::Live: return "Live";
    case Round_Phase::Round_End: return "Round_End";
    case Round_Phase::Game_Over: return "Game_Over";
  }
  assert(false && "invalid Round_Phase");
  return "";
}

template <> std::optional<Round_Phase> try_from_string<Round_Phase>(std::string_view text)
{
  if (text == "Warmup") return Round_Phase::Warmup;
  if (text == "Countdown") return Round_Phase::Countdown;
  if (text == "Live") return Round_Phase::Live;
  if (text == "Round_End") return Round_Phase::Round_End;
  if (text == "Game_Over") return Round_Phase::Game_Over;
  return std::nullopt;
}

const char* to_string(game_event_type value)
{
  switch (value)
  {
    case game_event_type::Rocket_Detonated: return "Rocket_Detonated";
    case game_event_type::Player_Died: return "Player_Died";
    case game_event_type::Player_Spawned: return "Player_Spawned";
    case game_event_type::Round_Phase_Changed: return "Round_Phase_Changed";
  }
  assert(false && "invalid game_event_type");
  return "";
}

void fire_rocket_detonated(event_stream_t& stream, const Rocket_Detonated& payload)
{
  stream.writer.write_bits((uint32_t)game_event_type::Rocket_Detonated, 16);
  for (const field_info_t& field : Span<const field_info_t>{ROCKET_DETONATED_FIELDS, 3})
    network::write_field(stream.writer, reinterpret_cast<const uint8_t*>(&payload), field, field.offset);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[event fired] {}", to_text(payload));
}

std::optional<Rocket_Detonated> try_read_rocket_detonated(network::Bit_Reader& reader)
{
  Rocket_Detonated payload;
  for (const field_info_t& field : Span<const field_info_t>{ROCKET_DETONATED_FIELDS, 3})
    if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&payload), field, field.offset))
      return std::nullopt;
  return payload;
}

std::string to_text(const Rocket_Detonated& value)
{
  return std::string("Rocket_Detonated") + fields_to_text({ROCKET_DETONATED_FIELDS, 3}, &value);
}

void fire_player_died(event_stream_t& stream, const Player_Died& payload)
{
  stream.writer.write_bits((uint32_t)game_event_type::Player_Died, 16);
  for (const field_info_t& field : Span<const field_info_t>{PLAYER_DIED_FIELDS, 4})
    network::write_field(stream.writer, reinterpret_cast<const uint8_t*>(&payload), field, field.offset);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[event fired] {}", to_text(payload));
}

std::optional<Player_Died> try_read_player_died(network::Bit_Reader& reader)
{
  Player_Died payload;
  for (const field_info_t& field : Span<const field_info_t>{PLAYER_DIED_FIELDS, 4})
    if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&payload), field, field.offset))
      return std::nullopt;
  return payload;
}

std::string to_text(const Player_Died& value)
{
  return std::string("Player_Died") + fields_to_text({PLAYER_DIED_FIELDS, 4}, &value);
}

void fire_player_spawned(event_stream_t& stream, const Player_Spawned& payload)
{
  stream.writer.write_bits((uint32_t)game_event_type::Player_Spawned, 16);
  for (const field_info_t& field : Span<const field_info_t>{PLAYER_SPAWNED_FIELDS, 3})
    network::write_field(stream.writer, reinterpret_cast<const uint8_t*>(&payload), field, field.offset);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[event fired] {}", to_text(payload));
}

std::optional<Player_Spawned> try_read_player_spawned(network::Bit_Reader& reader)
{
  Player_Spawned payload;
  for (const field_info_t& field : Span<const field_info_t>{PLAYER_SPAWNED_FIELDS, 3})
    if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&payload), field, field.offset))
      return std::nullopt;
  return payload;
}

std::string to_text(const Player_Spawned& value)
{
  return std::string("Player_Spawned") + fields_to_text({PLAYER_SPAWNED_FIELDS, 3}, &value);
}

void fire_round_phase_changed(event_stream_t& stream, const Round_Phase_Changed& payload)
{
  stream.writer.write_bits((uint32_t)game_event_type::Round_Phase_Changed, 16);
  for (const field_info_t& field : Span<const field_info_t>{ROUND_PHASE_CHANGED_FIELDS, 3})
    network::write_field(stream.writer, reinterpret_cast<const uint8_t*>(&payload), field, field.offset);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[event fired] {}", to_text(payload));
}

std::optional<Round_Phase_Changed> try_read_round_phase_changed(network::Bit_Reader& reader)
{
  Round_Phase_Changed payload;
  for (const field_info_t& field : Span<const field_info_t>{ROUND_PHASE_CHANGED_FIELDS, 3})
    if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&payload), field, field.offset))
      return std::nullopt;
  return payload;
}

std::string to_text(const Round_Phase_Changed& value)
{
  return std::string("Round_Phase_Changed") + fields_to_text({ROUND_PHASE_CHANGED_FIELDS, 3}, &value);
}

std::string game_event_stream_to_text(const event_stream_t& stream)
{
  if (stream.empty())
    return "<nothing pending>";

  network::Bit_Reader reader(stream.writer.buffer.data(),
                             stream.writer.buffer.size());
  reader.read_bits(16); // the count slot, backpatched only at send

  std::string text;
  for (uint32_t index = 0; index < stream.count; ++index)
  {
    if (index > 0)
      text += '\n';

    const uint32_t kind = reader.read_bits(16);
    if (kind >= GAME_EVENT_TYPE_COUNT)
    {
      text += std::format("<unknown kind {}; the rest is unreadable>", kind);
      break;
    }

    switch ((game_event_type)kind)
    {
      case game_event_type::Rocket_Detonated:
      {
        const std::optional<Rocket_Detonated> payload = try_read_rocket_detonated(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case game_event_type::Player_Died:
      {
        const std::optional<Player_Died> payload = try_read_player_died(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case game_event_type::Player_Spawned:
      {
        const std::optional<Player_Spawned> payload = try_read_player_spawned(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case game_event_type::Round_Phase_Changed:
      {
        const std::optional<Round_Phase_Changed> payload = try_read_round_phase_changed(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
    }
  }

  return text;
}

} // namespace shared
