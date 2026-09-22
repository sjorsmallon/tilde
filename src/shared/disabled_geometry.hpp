#pragma once

// Which of the session's geometry objects are switched OFF this tick, cut out of
// the entities the way the movement volumes are. prediction_def.md §4.
//
// THE BVH IS THE SHAPE OF THE MAP AND THE SWITCH IS STATE. Both sides build the
// tree from the same map and nothing writes to it after build_session, which is
// what makes player_move a pure function of its arguments. Which brushes are off
// comes from entities, changes per tick, is predicted by the client and is
// corrected -- so it travels as a PARAMETER, derived fresh every step, and never
// as a bit written into the tree. A bit in the tree is a copy of Enabled::value
// that every path writing Enabled has to keep in step, and the reconciliation
// replay would restart from a tree a snapshot had already overwritten.
//
// This is what every brush engine does for a toggle: Jolt's object layers, PhysX
// filter data, Unreal's SetCollisionEnabled. Refit and rebuild are for geometry
// that MOVES, and nothing here moves.
//
// A TEAM WALL IS THE SAME SET WITH ONE MORE INPUT. `Geometry_Owner_Entity::passable_by`
// names the team whose movers and shots the geometry is not there for, so the set
// a sweep reads is a function of WHO is moving -- and the team is the only input,
// so it is one set per team rather than one per player. What the DRAW reads is a
// different question: a team wall is visible to everyone, so the draw asks for
// the switch alone (collect_hidden_geometry) and never for a team's set.

#include "entities/generated/entities_core_generated.hpp"
#include "entity_uid.hpp"
#include "span.hpp"

#include <cstdint>
#include <vector>

namespace entities
{
struct Geometry_Owner_Entity;
}

namespace shared
{

struct Entity_System;

// One byte per geometry index, non-zero meaning "not there this tick". Keyed by
// INDEX because that is what a BVH leaf carries -- Collision_Id::index is the
// array position, chosen so the tree needs no lookup table -- which makes the
// test inside the sweep one array read and resolves no uid at all.
//
// A byte rather than a packed word for that same read: the whole point is that
// the hot loop pays a load and a compare.
using disabled_geometry_t = std::vector<uint8_t>;

// THE ONE RULE, asked by both collects so they cannot disagree about the switch.
// Off is off for everyone. Switched on, the geometry blocks every team but the
// one it is passable by; Free_For_All on the owner means passable by nobody.
[[nodiscard]] bool geometry_owner_blocks(const entities::Geometry_Owner_Entity& owner,
                                         entities::Team_Allegiance          mover_team);

// Rebuilt every tick, on both sides, at the same sites that collect the volumes.
// `owner_of` is game_session_t::owner_of -- already checked at load, so an entry
// here is a uid that WAS a Geometry_Owner_Entity when the map loaded. One that the system
// no longer holds sets no bit and the object stays solid; there is nothing to
// report, because a runtime-destroyed owner is not an authoring mistake.
//
// A disabled brush is deliberately NOT a movement volume: a volume is a box
// tested AFTER the step, while this is consulted INSIDE the sweep by every leaf
// test -- otherwise the step collides with the brush and then reads a volume
// saying "never mind".
//
// The set for a MOVER of `mover_team`: the switch, and the team walls it passes.
// A shot reads the shooter's set, a projectile its owner's -- you shoot through
// what you can walk through.
void collect_disabled_geometry(Entity_System& system, Span<const entity_uid_t> owner_of,
                               entities::Team_Allegiance mover_team, disabled_geometry_t& out);

// The set for the DRAW and for anything with no team: the switch alone. A team
// wall is in nobody's hidden set, which is what keeps it visible to the team
// that walks through it.
void collect_hidden_geometry(Entity_System& system, Span<const entity_uid_t> owner_of,
                             disabled_geometry_t& out);

} // namespace shared
