#pragma once

// =============================================================================
// Centralized damage routing
// =============================================================================
// One server-side choke point through which every damage source flows.
// Modeled after Source 2's `CBaseEntity::OnTakeDamage` with the OOP machinery
// removed: no virtuals on Entity, no per-type CBaseEntity inheritance, no
// CLIENT_DLL / SERVER_DLL ifdef soup. The dispatch is a small switch on
// `entity_type` inside `inflict_damage`; promote to a registration table only
// when the switch grows past ~6 damageable types or one case grows past
// ~50 lines (see events_plan.md §"Per-entity damage dispatch").
//
// Why a single helper:
//   - Death detection (the health>0 → health<=0 crossing) lives here, so a
//     new damage source (hitscan, fall, void volume, melee) cannot quietly
//     forget to fire PLAYER_DIED + schedule_respawn — both are owned by
//     `inflict_damage`.
//   - Knockback velocity write lives here, so the kinematic-Jolt-body
//     convention (write player->velocity directly; player_move reads it
//     next tick) doesn't have to be re-rediscovered at every call site.
//   - The cosmetic ROCKET_EXPLOSION / gameplay ROCKET_DETONATED events stay
//     where they fire (the *detonation* is distinct from the per-victim
//     damage application). This helper only owns the per-victim dance.
//
// Inflictor vs attacker:
//   - attacker_uid  = the player credited for the kill (kill feed, score).
//   - inflictor_uid = the entity that actually did the damaging (knockback
//                     direction reference for projectiles; equals attacker
//                     for direct-touch damage). Pinning this now avoids a
//                     retrofit later when grenade-like inflictors land.
// =============================================================================

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
