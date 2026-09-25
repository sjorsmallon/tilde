#pragma once

#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/entity_uid.hpp"
#include "../shared/hit_region.hpp"
#include "../shared/linalg.hpp"

#include <cstdint>

namespace server
{

struct damage_info_t
{
  shared::entity_uid_t victim_uid     = 0;
  shared::entity_uid_t attacker_uid   = 0; // 0 = world / suicide
  shared::entity_uid_t inflictor_uid  = 0; // 0 = same as attacker
  uint16_t             weapon_id      = 0;
  float                amount         = 0.f;
  linalg::vec3f        source_position{0.f, 0.f, 0.f};
  float                knockback_force = 0.f;
  entities::Damage_Type type          = entities::Damage_Type::Normal;
  bool                 was_headshot   = false;
};

// One row of inflict_damage_batch: a Damage contact resolved against the row, not yet applied.
struct pending_hit_t
{
  damage_info_t        info;
  linalg::vec3f        impact_point{};
  linalg::vec3f        impact_normal{};
  shared::hit_region_t region = shared::hit_region_t::Torso;
};

} // namespace server
