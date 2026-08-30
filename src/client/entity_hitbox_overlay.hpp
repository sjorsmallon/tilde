#pragma once

// The volumes an entity is SHOT against, drawn over the model rather than
// instead of it.
//
// That is the whole reason this is its own entry point. A hit volume lives
// INSIDE the thing it belongs to, so a draw site that picks one or the other is
// answering a question nobody asked -- which is what the editor did, and why a
// crate's hitbox was invisible for exactly as long as its mesh resolved.
//
// The SKELETAL half is not here: a player's volumes come off a posed rig
// (shared::compute_player_hitboxes) rather than off fields, and the two sites
// that draw those already share that one function. This is the fields-only
// half, and the two meet at hitbox_debug_draw.hpp, which knows neither.

#include "../shared/entities/generated/entities_generated.hpp"
#include "frame_builder.hpp"

namespace client
{

// Returns whether anything was appended. Cvar-free on purpose: whether the
// overlay is wanted is the call site's question (debug_show_hitboxes in game,
// the same cvar in the editor), and passing the answer keeps this a function of
// the entity alone.
bool draw_entity_hitbox_overlay(const entities::Entity *entity, pass_builder_t &draws);

} // namespace client
