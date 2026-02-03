#pragma once

#include "entity.hpp"

namespace network
{

class Weapon_Entity : public Entity
{
public:
  // Networked Variables
  Network_Var<linalg::vec3_t<float32>> position;
  Network_Var<linalg::vec3_t<float32>> angles;

  Network_Var<int32> ammo;
  Network_Var<int32> active_weapon_id;

  // Required macro to declare schema functions
  DECLARE_SCHEMA(Weapon_Entity)
};

} // namespace network
