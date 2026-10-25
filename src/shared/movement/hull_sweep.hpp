#pragma once
// The ONE question a moving hull asks: how far along this move does the box get
// before it touches something, and what did it touch? The same candidates the
// penetration resolver collects (the map through the tick's disabled set, the
// movers at their end pose), each plane grown by the hull's support radius, the
// centre swept as a ray. A fraction of 1 is the whole move; 0 is a hull already
// touching or inside, which is the depenetration resolver's business.
#include "collision/collision_detection.hpp"
#include "collision/predicted_world.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"

#include <optional>

namespace shared
{

struct hull_sweep_hit_t
{

  float t;
  vec3 normal;
  Collision_Id id;
  entity_uid_t mover_uid = null_entity_uid;
};

[[nodiscard]] std::optional<hull_sweep_hit_t> sweep_hull(const Bounding_Volume_Hierarchy& bvh,
                                                         const predicted_world_t& world,
                                                         const vec3& from, const vec3& to,
                                                         float half_width, float half_height);

} // namespace shared
