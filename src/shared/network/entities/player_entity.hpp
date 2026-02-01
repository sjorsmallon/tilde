#pragma once

#include "../entity.hpp"

namespace network
{

class Player_Entity : public Entity
{
public:
  // Networked Variables
  Network_Var<linalg::vec3_t<float32>> position;
  Network_Var<float32> view_angle_yaw;
  Network_Var<float32> view_angle_pitch;

  Network_Var<int32> health;
  Network_Var<int32> ammo;
  Network_Var<int32> active_weapon_id;

  // Required macro to declare schema functions
  DECLARE_SCHEMA(Player_Entity)
};

} // namespace network
