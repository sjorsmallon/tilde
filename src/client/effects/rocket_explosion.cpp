#include "../../shared/effects/generated/effects_generated.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"
#include "../../shared/physics.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"

namespace client::effects
{

// ROCKET_EXPLOSION handler. The server tells us:
void on_rocket_explosion(client_context_t &context,
                         const shared::Rocket_Explosion &data)
{
  if (!context.world.physics_state)
  {
    log_error("rocket_explosion handler invoked with no client physics world — "
              "an effect arrived before any map was loaded (context.world.ready "
              "is false), so there is no surface to resolve the decal against");
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
    // World geometry only -- the decal belongs on the surface the server
    // described, not on whatever player happens to be standing in front of it.
    // Front faces only, so a probe starting inside a wall doesn't return a
    // fraction-0 hit with a flipped normal.
    const query_filter_t filter{.layers     = query_layers_t::Static_Only,
                                .back_faces = back_face_mode_t::Ignore};
    bool surface_hit = cast_sphere(*context.world.physics_state,
                                   probe_from, probe_to,
                                   probe_radius, filter, hit);

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
  explosion_effect_t fx{};
  fx.position        = data.origin;
  fx.time_remaining  = 1.2f;
  fx.explosion_index = context.visuals.next_explosion_index++;
  context.visuals.explosion_effects.push_back(fx);

  if (context.audio)
    context.audio->play_3d("resources/sounds/rocket_explosion.wav", data.origin);
}

} // namespace client::effects
