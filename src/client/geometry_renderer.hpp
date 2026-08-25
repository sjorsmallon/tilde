#pragma once

#include "../shared/entity_uid.hpp"
#include "../shared/map_geometry.hpp"
#include "frame_builder.hpp"

namespace client
{

// Draw one piece of map geometry into a pass.
//
// One implementation for both regimes that used to draw geometry: the in-game
// static pass in Play_State and the editor's per-entity traits. Both had grown
// their own copy of "resolve the mesh path, else fall back to the kind's
// primitive", and they had already drifted (the editor cached generated
// displacement meshes, the game regenerated one every frame).
//
// `uid` only names the cache slot for a displacement's generated mesh, so the
// same displacement doesn't get rebuilt once per view per frame.
void draw_geometry(pass_builder_t &draws, const shared::geometry_value_t &geometry,
                   shared::entity_uid_t uid);

// Rebuild the cached mesh for an object whose GENERATED form just changed -- a
// displacement grid, or a brush point set -- and re-upload it. Registers the
// mesh if this is the first time. The cache is keyed by uid only, so it cannot
// notice an edit by itself: call this after one.
//
// Takes the whole geometry rather than one kind, because both kinds that
// generate a mesh need exactly this, and a per-kind entry point is two things
// to remember instead of one.
void refresh_generated_geometry_mesh(const shared::geometry_value_t &geometry,
                                     shared::entity_uid_t uid);

} // namespace client
