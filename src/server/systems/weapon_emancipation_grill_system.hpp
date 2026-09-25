#pragma once

namespace server
{

struct server_context_t;

// Every enabled Weapon_Emancipation_Grill_Entity, tested against where the tick ended: a living
// player inside loses every weapon carried, a weapon lying ownerless inside is taken. Each taken
// weapon leaves an Emancipated_Weapon_Entity where it was -- in front of the eye for a carried one,
// in place for a lying one -- which this same pass destroys once its dissolve has run out. The
// grill is the one that ACTS on its own overlap; Touched / Left still emit from update_triggers
// for whatever a map wires to them.
//
// Runs after the dropped-weapon pickup so a weapon a player walked onto inside the grill is
// taken from the hand it just reached rather than left on the floor for a tick.
void update_weapon_emancipation_grills(server_context_t& context);

} // namespace server
