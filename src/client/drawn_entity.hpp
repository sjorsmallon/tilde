#pragma once

// Where and how a session entity is drawn this frame. A drawn pose has two sources and no third: replicated
// state interpolated through the ring (what the server integrates), or launch state evaluated at the drawn tick
// by the shared `*_at_tick` function the predicted world cuts with (what the server could derive, and so never
// stores). try_compute_drawn_entity_at_tick is the one place that says which an entity type is.

#include "client_context.hpp"
#include "renderer.hpp"

#include <optional>

namespace client
{

struct drawn_tick_t
{
  uint32_t tick{};
  float    fraction{};
  uint32_t tickrate_hz{};
};

// The tick and the fraction into it this frame is drawn at: the predicted clock eased by drawn_tick_gap.
[[nodiscard]] drawn_tick_t get_tick_and_fraction_used_for_drawing_this_frame(const client_context_t& ctx);

// The pose plus the look modifiers mesh_draw_t carries; the mesh and the material are the Render component's.
struct drawn_entity_t
{
  vec3f                  position    = {};
  quatf                  orientation = {0.f, 0.f, 0.f, 1.f};
  vec3f                  scale       = {1.f, 1.f, 1.f};
  vec3f                  color       = {1.f, 1.f, 1.f};
  bool                   as_ghost    = false;
  renderer::clock_wipe_t clock_wipe  = {};
  float                  dissolve    = 0.f;
  renderer::peel_t       peel        = {};
};

// Empty when the entity draws nowhere in the world this frame: a player (the remote-player pass draws those), a
// held weapon, the rocket we pilot, a platform that has vanished. `local_feet` is where our own body is drawn, for
// the canopy hanging over it.
[[nodiscard]] std::optional<drawn_entity_t> try_compute_drawn_entity_at_tick(const client_context_t& ctx,
                                                                             const entities::Entity& entity,
                                                                             const entities::Render& render,
                                                                             const drawn_tick_t&     drawn,
                                                                             const vec3f&            local_feet);

// Glued to its carrier's drawn feet: ours, a remote player's, or where it was replicated when neither is found.
[[nodiscard]] vec3f drawn_canopy_center(const client_context_t& ctx, const entities::Canopy_Entity& canopy,
                                        const vec3f& local_feet);

} // namespace client
