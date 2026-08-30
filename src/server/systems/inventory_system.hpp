#pragma once

#include "../../shared/entities/generated/entities_generated.hpp"
#include "../../shared/entity_uid.hpp"
#include "../../shared/game_session.hpp"
#include "../server_context.hpp"

namespace server
{

// A player's weapons are ENTITIES and the player holds their uids in
// Inventory::weapons, keyed by INVENTORY SLOT -- where a thing is held, not
// which thing it is (generalization_def.md §1). Per-weapon state -- ammo, and
// the deadline that weapon may next fire on -- lives on the weapon rather than
// the player, which is what lets each one recover on its own clock while
// holstered, and which is also what makes two of a kind cost nothing here.
//
// The list is STORED, not derived: Weapon_Entity::owner_uid is a back-reference
// for teardown, never the thing consulted to answer "what am I carrying".

// Spawn one Weapon_Entity per weapon type, each into the slot its definition
// names, and record their uids on the player.
//
// MUST run in the same tick the player entity is spawned. A snapshot frame is
// atomic with respect to loss -- a fragmented message reassembles or is dropped
// whole, and a client that cannot apply a delta does not advance the tick it
// says it holds -- so a player and its weapons that spawn together arrive
// together, and inventory.weapons can never name a weapon the receiver lacks.
// Split them across ticks and that guarantee is gone.
void grant_default_inventory(shared::game_session_t& session, shared::entity_uid_t player_uid);

// Put one weapon in the slot its definition names, destroying whatever the slot
// held. The pickup / card-draw door, and the one the default grant above goes
// through internally.
//
// Fallible because the spawn is: a full pool is a real outcome and the caller
// gets no uid. Displacing is NOT a failure -- it is what taking a second rifle
// means -- and the displaced entity is destroyed rather than leaked.
//
// The damage type is a property of the granted WEAPON, so it is decided here by
// whoever hands it out -- a trigger's param, a card -- and written at the spawn
// rather than poked onto the entity afterwards.
[[nodiscard]] shared::entity_uid_t
try_grant_weapon(server_context_t&         context,
                 entities::Player_Entity& player,
                 entities::Weapon         weapon,
                 entities::Damage_Type    damage_type = entities::Damage_Type::Normal);

// Destroy every weapon the player carries and clear the list. Same tick rule as
// above, in reverse: a weapon outliving its owner is a leak no one holds a
// handle to.
void destroy_inventory(server_context_t& context, shared::entity_uid_t player_uid);

// Every carried weapon back to a full magazine, with its own fire clock cleared
// and no switch in flight. This is what a (re)spawn resets, and it is here
// rather than in respawn_system because it walks the inventory: the clocks and
// the magazines are per-weapon now, so there is no single field left for a
// spawn to poke. An empty slot is skipped rather than reported -- carrying
// fewer than every slot is the normal state.
//
// The clocks are ABSOLUTE deadlines, so clearing them is not cosmetic -- a
// player who died mid-recovery would otherwise come back still gated by a
// deadline the corpse earned.
void refill_inventory(shared::game_session_t& session, entities::Player_Entity& player);

// The weapon in the active slot, or nullptr if that slot is empty.
//
// Fallible because an empty hand is a legal state, not an error: a slot nothing
// was ever granted into, a player entity that predates the grant, a bot spawned
// by a path that skipped it -- and because active_slot is an enum whose range
// nothing on the wire checks. Every caller wants "no shot" out of that, not a
// fatal.
//
// The returned pointer is invalidated by the next spawn or destroy in the
// Weapon_Entity pool. Do not store it.
[[nodiscard]] entities::Weapon_Entity*
try_find_active_weapon(shared::game_session_t& session, const entities::Player_Entity& player);

} // namespace server
