#include "game_events.hpp"

#include "log.hpp"
#include "network/quantization.hpp"

#include <cassert>

using namespace linalg;

namespace shared
{

static void serialize_rocket_detonated(network::Bit_Writer &writer,
                                       const rocket_detonated_payload_t &payload)
{
  network::write_var_uint(writer, payload.attacker_id);
  network::write_var_uint(writer, payload.victim_id);
  writer.write_bits(payload.weapon_id, 16);
}

static rocket_detonated_payload_t
deserialize_rocket_detonated(network::Bit_Reader &reader)
{
  rocket_detonated_payload_t payload{};
  payload.attacker_id = network::read_var_uint(reader);
  payload.victim_id   = network::read_var_uint(reader);
  payload.weapon_id   = static_cast<uint16_t>(reader.read_bits(16));
  return payload;
}

static void serialize_player_died(network::Bit_Writer &writer,
                                  const player_died_payload_t &payload)
{
  network::write_var_uint(writer, payload.victim_id);
  network::write_var_uint(writer, payload.attacker_id);
  writer.write_bits(payload.weapon_id, 16);
  writer.write_bits(payload.was_headshot ? 1u : 0u, 1);
}

static player_died_payload_t
deserialize_player_died(network::Bit_Reader &reader)
{
  player_died_payload_t payload{};
  payload.victim_id    = network::read_var_uint(reader);
  payload.attacker_id  = network::read_var_uint(reader);
  payload.weapon_id    = static_cast<uint16_t>(reader.read_bits(16));
  payload.was_headshot = reader.read_bits(1) != 0;
  return payload;
}

// Same quantized coord encoding as cosmetic_events.cpp uses for vec3f fields
// (5 bits of fractional precision). Spawn positions are world coordinates,
// orientations are Euler degrees in [-180, 180] — both fit comfortably.
static void write_vec3_coord(network::Bit_Writer &writer, const vec3f &v)
{
  network::write_coord(writer, v.x);
  network::write_coord(writer, v.y);
  network::write_coord(writer, v.z);
}

static vec3f read_vec3_coord(network::Bit_Reader &reader)
{
  vec3f v;
  v.x = network::read_coord(reader);
  v.y = network::read_coord(reader);
  v.z = network::read_coord(reader);
  return v;
}

static void serialize_player_spawned(network::Bit_Writer &writer,
                                     const player_spawned_payload_t &payload)
{
  network::write_var_uint(writer, payload.player_id);
  write_vec3_coord(writer, payload.spawn_position);
  write_vec3_coord(writer, payload.spawn_orientation);
}

static player_spawned_payload_t
deserialize_player_spawned(network::Bit_Reader &reader)
{
  player_spawned_payload_t payload{};
  payload.player_id         = network::read_var_uint(reader);
  payload.spawn_position    = read_vec3_coord(reader);
  payload.spawn_orientation = read_vec3_coord(reader);
  return payload;
}

void serialize_game_event(network::Bit_Writer &writer,
                          const game_event_t &event)
{
  writer.write_bits(static_cast<uint32_t>(event.kind), 16);
  switch (event.kind)
  {
    case game_event_kind_t::ROCKET_DETONATED:
      serialize_rocket_detonated(writer, event.rocket_detonated);
      break;
    case game_event_kind_t::PLAYER_DIED:
      serialize_player_died(writer, event.player_died);
      break;
    case game_event_kind_t::PLAYER_SPAWNED:
      serialize_player_spawned(writer, event.player_spawned);
      break;
  }
}

game_event_t deserialize_game_event(network::Bit_Reader &reader)
{
  game_event_t event{};
  uint32_t kind_id = reader.read_bits(16);

  switch (static_cast<game_event_kind_t>(kind_id))
  {
    case game_event_kind_t::ROCKET_DETONATED:
      event.kind = game_event_kind_t::ROCKET_DETONATED;
      event.rocket_detonated = deserialize_rocket_detonated(reader);
      return event;
    case game_event_kind_t::PLAYER_DIED:
      event.kind = game_event_kind_t::PLAYER_DIED;
      event.player_died = deserialize_player_died(reader);
      return event;
    case game_event_kind_t::PLAYER_SPAWNED:
      event.kind = game_event_kind_t::PLAYER_SPAWNED;
      event.player_spawned = deserialize_player_spawned(reader);
      return event;
  }

  log_error("deserialize_game_event: unknown game_event_kind_t {}", kind_id);
  assert(false);
  return event;
}

void serialize_game_event_batch(network::Bit_Writer &writer,
                                const std::vector<game_event_t> &events)
{
  // Cap at uint16 — a single tick producing > 65k gameplay events would be a
  // bug in the dispatch path, not a legitimate use case.
  assert(events.size() <= 0xFFFFu);
  writer.write_bits(static_cast<uint32_t>(events.size()), 16);
  for (const auto &event : events)
    serialize_game_event(writer, event);
}

std::vector<game_event_t>
deserialize_game_event_batch(network::Bit_Reader &reader)
{
  uint32_t count = reader.read_bits(16);
  std::vector<game_event_t> result;
  result.reserve(count);
  for (uint32_t i = 0; i < count; ++i)
    result.push_back(deserialize_game_event(reader));
  return result;
}

} // namespace shared
