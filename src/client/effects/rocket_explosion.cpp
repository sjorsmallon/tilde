#include "../../shared/cosmetic_events.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"
#include "../../shared/physics.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"

namespace client::effects
{

// ROCKET_EXPLOSION handler. The server tells us:
//   - data.origin: world-space detonation point
//   - data.normal: surface normal at impact (zero = airburst, no surface)
//   - data.scale:  splash radius (used as decal probe distance)
//
// For a surface hit we want a decal anchored to the surface the *client* sees,
// not the server's view — they can drift slightly under reconciliation. We
// cast from a point stepped out along the normal back into the wall to find
// the local contact. Airbursts skip the cast entirely (no surface).
//
// Phase 1: cast result is logged; decal renderer bolts in later. The visible
// explosion particle effect is pushed onto ctx.explosion_effects here —
// previously inferred from "rocket disappeared from snapshot" in play_state,
// now produced by the explicit cosmetic dispatch. The explosion sound is
// played spatialized at the detonation origin.
void on_rocket_explosion(client_context_t &context,
                         const shared::effect_data_t &data)
{
  if (!context.physics_state)
  {
    log_error("rocket_explosion handler invoked with no client physics_state — "
              "PlayState should set context.physics_state on entry");
    return;
  }

  const bool is_airburst =
      data.normal.x == 0.f && data.normal.y == 0.f && data.normal.z == 0.f;

  if (is_airburst)
  {
    log_terminal("[CLIENT FX] rocket_explosion at ({:.1f},{:.1f},{:.1f}) → airburst",
                 data.origin.x, data.origin.y, data.origin.z);
  }
  else
  {
    // Step out along the surface normal, then probe back toward the surface
    // a short distance past the original origin. The 4-unit step keeps the
    // cast start clear of the wall; the 12-unit probe is well within typical
    // rocket hull thickness so we still land on the surface.
    constexpr float probe_step = 4.f;
    constexpr float probe_depth = 12.f;
    constexpr float probe_radius = 4.f;
    vec3f surface_normal = linalg::normalize(data.normal);
    vec3f probe_from = data.origin + surface_normal * probe_step;
    vec3f probe_to   = data.origin - surface_normal * probe_depth;

    hit_result_t hit;
    bool surface_hit = cast_sphere_static(*context.physics_state,
                                          probe_from, probe_to,
                                          probe_radius, hit);

    if (surface_hit)
    {
      log_terminal("[CLIENT FX] rocket_explosion at ({:.1f},{:.1f},{:.1f}) "
                   "→ decal at ({:.1f},{:.1f},{:.1f}) n=({:.2f},{:.2f},{:.2f})",
                   data.origin.x, data.origin.y, data.origin.z,
                   hit.position.x, hit.position.y, hit.position.z,
                   hit.normal.x, hit.normal.y, hit.normal.z);
    }
    else
    {
      // Server saw a surface but the client doesn't — usually means the
      // client physics state is out of sync (mid-load, late connect). Log
      // and fall through to a particle-only explosion.
      log_terminal("[CLIENT FX] rocket_explosion at ({:.1f},{:.1f},{:.1f}) "
                   "→ server reported surface but local cast missed",
                   data.origin.x, data.origin.y, data.origin.z);
    }
  }

  // Spawn the visible particle effect at the detonation origin. Lifetime is
  // long enough for the particle emitter's emit-then-fade window (see the
  // draw site in play_state.cpp).
  client_context_t::explosion_effect_t fx{};
  fx.position        = data.origin;
  fx.time_remaining  = 1.2f;
  fx.explosion_index = context.next_explosion_index++;
  context.explosion_effects.push_back(fx);

  // Spatialized blast at the detonation point. The audio system is borrowed
  // and may be null (audio init failed / non-play state); play_3d also no-ops
  // gracefully if the file is missing.
  if (context.audio)
    context.audio->play_3d("resources/sounds/rocket_explosion.wav", data.origin);
}

} // namespace client::effects
