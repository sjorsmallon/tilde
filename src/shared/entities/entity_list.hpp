#ifndef SHARED_ENTITY_LIST_HPP
#define SHARED_ENTITY_LIST_HPP

// entity_list.hpp is the "give me everything" header: the X-macro, the enum,
// and (under ENTITIES_WANT_INCLUDES) every entity class definition.
//
// The enum + X-macro live in entity_type.hpp so entity.hpp can include them
// without recursively pulling in entity headers (which depend on entity.hpp).
// See entity_type.hpp for the rationale.

#include "entity_type.hpp"

#endif // SHARED_ENTITY_LIST_HPP


// The full entity-header pull-in is opt-in (ENTITIES_WANT_INCLUDES) and lives
// AFTER the include guard on purpose — it must run every time the user
// re-includes this file with ENTITIES_WANT_INCLUDES newly defined.
#ifdef ENTITIES_WANT_INCLUDES
#include "entities/player_entity.hpp"
#include "entities/static_entities.hpp"
#include "entities/displacement_entity.hpp"
#include "entities/weapon_entity.hpp"
#include "entities/rocket_entity.hpp"
#include "entities/particle_emitter_entity.hpp"
#include "entities/trigger_volume_entity.hpp"
#include "entities/light_entity.hpp"
#include "entities/physics_body_entity.hpp"
#endif
