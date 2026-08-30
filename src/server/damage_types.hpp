#pragma once

#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/entity_uid.hpp"
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

} // namespace server
