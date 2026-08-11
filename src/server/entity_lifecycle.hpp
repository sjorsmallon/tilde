#pragma once

#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/entity_uid.hpp"
#include "server_context.hpp"

namespace server
{

// What a Player_Entity IS, physically and visually. Called at spawn by BOTH
// spawn_player_for_slot and spawn_bot, because **a bot IS a Player_Entity** --
// one that was shaped or drawn differently from a human would make every aim
// test a lie, and the hitbox half of this was already written out twice before
// the render half gave it a second reason to drift.
//
// Does NOT touch position, orientation, slot or health: those differ per spawn
// and are the caller's business. This is only the part that is the same for
// every player in the game.
void initialize_player_body(entities::Player_Entity &player);

// Destroy a server-side entity: its Jolt body, its server-side side-table
// entries, and finally the entity itself. Returns what
// Entity_System::destroy(uid) returned -- false meaning no entity held that uid,
// which the caller may read as "already gone" rather than as an error.
//
// **Server code destroys entities through here, not through
// `session.entity_system.destroy(uid)` directly.** That call is the storage
// primitive: it removes a value from a pool and repairs the uid index, and it
// knows nothing about Jolt because `Entity_System` lives in `game_shared` and
// `physics_state_t` is not its business. Everything an entity owns OUTSIDE the
// pool has to be torn down by whoever knows about it, and on the server that is
// this function.
//
// WHY A FUNCTION AND NOT A HOOK ON Entity_System (P7 step 5, the decision the
// step existed to make): the alternative was a `std::function` installed on the
// session at init, so that `destroy(uid)` could call back into physics. That is
// registration wearing a different hat -- invisible at the call site, silently
// absent on any Entity_System nobody remembered to install it on (the client
// builds one too), and the exact shape the last seven phases spent their time
// deleting. A parameter says what it needs; an installed hook asks you to hope.
// The cost is that this is a convention rather than something the compiler
// enforces: `entity_system.destroy` is still callable and still correct as
// storage. The comment on it points back here.
bool destroy_entity(server_context_t &context, shared::entity_uid_t uid);

} // namespace server
