#pragma once

#include "../shared/entity_uid.hpp"
#include "../shared/linalg.hpp"

#include <cstdint>

namespace server
{

// Single damage-type discriminator. GENERIC is the only value used today.
// Grows when something genuinely needs to discriminate (resistances, armor
// categories, fall vs explosion FX) — no DMG_* bitfield yet.
enum class damage_type_t : uint16_t
{
  GENERIC = 0,
};

struct damage_info_t
{
  shared::entity_uid_t victim_uid     = 0;
  shared::entity_uid_t attacker_uid   = 0; // 0 = world / suicide
  shared::entity_uid_t inflictor_uid  = 0; // 0 = same as attacker
  uint16_t             weapon_id      = 0;
  float                amount         = 0.f;
  linalg::vec3f        source_position{0.f, 0.f, 0.f};
  float                knockback_force = 0.f;
  damage_type_t        type           = damage_type_t::GENERIC;
  bool                 was_headshot   = false;
};

} // namespace server
