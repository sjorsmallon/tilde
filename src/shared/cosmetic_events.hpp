#pragma once

#include "linalg.hpp"
#include "map.hpp" // shared::entity_uid_t
#include "network/bitstream.hpp"

#include <cstdint>
#include <vector>

namespace shared
{

// Closed enum of every cosmetic effect the server can dispatch. The set is
// intentionally finite — grep finds every dispatch site. Add new types here
// (and a matching client handler in src/client/effects/) as needed.
enum class effect_type_t : uint16_t
{
  ROCKET_EXPLOSION,
  BULLET_IMPACT,
  FOOTSTEP,
};

// One fixed-shape payload for every effect. Handlers read the fields they
// care about and ignore the rest. Sentinel values: zero vectors / zero
// entity / zero material id mean "not applicable" for that effect.
struct effect_data_t
{
  vec3f                origin;
  vec3f                normal;
  vec3f                color;
  float                scale;
  shared::entity_uid_t attached_entity; // 0 if world-space
  uint16_t             surface_material; // 0 if unknown
};

// Queue entry on the server (also the received form on the client). Stores
// the discriminator alongside the payload so a single std::vector holds the
// per-tick stream of pending effects.
struct dispatched_effect_t
{
  effect_type_t   type;
  effect_data_t   data;
};

// Wire format: [type:u16][origin:3*coord][normal:3*coord][color:3*coord]
// [scale:coord][attached_entity:var_uint][surface_material:u16].
// Riders inside the snapshot packet; see plan §"Wire format (cosmetic)".
void serialize_effect(network::Bit_Writer &writer,
                      const dispatched_effect_t &effect);

// Reads one effect from `reader`. Aborts (log_error + assert) if the type id
// is outside the closed enum — the server should never write an unknown type
// and silently dropping it would hide a bug.
dispatched_effect_t deserialize_effect(network::Bit_Reader &reader);

void serialize_effect_batch(network::Bit_Writer &writer,
                            const std::vector<dispatched_effect_t> &effects);

std::vector<dispatched_effect_t>
deserialize_effect_batch(network::Bit_Reader &reader);

} // namespace shared
