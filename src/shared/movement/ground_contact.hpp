#pragma once

#include "collision/collision_detection.hpp"
#include "entity_uid.hpp"
#include "plane.hpp"

namespace shared
{

// The ground, as the thing it belongs to: which geometry (or mover) and which
// of its planes, so the face it is can be asked afterwards (surface_lookup.hpp).
// The plane is the hull's own, never the contact point, since a face's identity
// is its plane.
struct ground_contact_t
{
  Collision_Id id        = {};
  entity_uid_t mover_uid = null_entity_uid;
  Plane        plane     = {};
};

} // namespace shared
