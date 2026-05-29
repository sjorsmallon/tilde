#include "cosmetic_events.hpp"

#include "log.hpp"
#include "network/quantization.hpp"

#include <cassert>

using namespace linalg;

namespace shared
{

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

void serialize_effect(network::Bit_Writer &writer,
                      const dispatched_effect_t &effect)
{
  writer.write_bits(static_cast<uint32_t>(effect.type), 16);
  write_vec3_coord(writer, effect.data.origin);
  write_vec3_coord(writer, effect.data.normal);
  write_vec3_coord(writer, effect.data.color);
  network::write_coord(writer, effect.data.scale);
  network::write_var_uint(writer, effect.data.attached_entity);
  writer.write_bits(effect.data.surface_material, 16);
}

dispatched_effect_t deserialize_effect(network::Bit_Reader &reader)
{
  dispatched_effect_t effect{};
  uint32_t type_id = reader.read_bits(16);

  // The enum is contiguous from 0, so a range check against COUNT is exactly a
  // membership test — no per-type allowlist to keep in sync with the enum.
  if (type_id >= static_cast<uint32_t>(effect_type_t::COUNT))
  {
    log_error("deserialize_effect: unknown effect_type_t {}", type_id);
    assert(false);
  }
  effect.type = static_cast<effect_type_t>(type_id);

  effect.data.origin           = read_vec3_coord(reader);
  effect.data.normal           = read_vec3_coord(reader);
  effect.data.color            = read_vec3_coord(reader);
  effect.data.scale            = network::read_coord(reader);
  effect.data.attached_entity  = network::read_var_uint(reader);
  effect.data.surface_material = static_cast<uint16_t>(reader.read_bits(16));
  return effect;
}

void serialize_effect_batch(network::Bit_Writer &writer,
                            const std::vector<dispatched_effect_t> &effects)
{
  // Cap at uint16 — a single tick producing > 65k cosmetic effects would be a
  // bug in the dispatch path, not a legitimate use case.
  assert(effects.size() <= 0xFFFFu);
  writer.write_bits(static_cast<uint32_t>(effects.size()), 16);
  for (const auto &effect : effects)
    serialize_effect(writer, effect);
}

std::vector<dispatched_effect_t>
deserialize_effect_batch(network::Bit_Reader &reader)
{
  uint32_t count = reader.read_bits(16);
  std::vector<dispatched_effect_t> result;
  result.reserve(count);
  for (uint32_t i = 0; i < count; ++i)
    result.push_back(deserialize_effect(reader));
  return result;
}

} // namespace shared
