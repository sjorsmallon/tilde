#pragma once

#include "../shared/linalg.hpp"
#include "../shared/entity_uid.hpp"
#include "server_context.hpp"

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
  vec3f                source_position{0.f, 0.f, 0.f};
  float                knockback_force = 0.f;
  damage_type_t        type           = damage_type_t::GENERIC;
  bool                 was_headshot   = false;
};

// Apply damage to whatever entity `info.victim_uid` resolves to. Owns:
//   - pre-checks (victim exists? still alive? — corpses stop taking damage)
//   - HP subtract (Player_Entity) or impulse application (Physics_Body_Entity)
//   - the >0 → <=0 crossing detection + PLAYER_DIED + schedule_respawn
//   - knockback velocity write
//
// Unknown entity types (anything not in the dispatch switch) log_error and
// do nothing — per the project's no-silent-failures rule.
void inflict_damage(server_context_t &context, const damage_info_t &info);

} // namespace server
