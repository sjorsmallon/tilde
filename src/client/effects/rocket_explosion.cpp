#include "../../shared/effects/generated/effects_generated.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"
#include "../../shared/collision_detection.hpp"
#include "../../shared/disabled_geometry.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"

namespace client::effects
{

// ROCKET_EXPLOSION handler. The server tells us:
void on_rocket_explosion(client_context_t &context,
                         const shared::Rocket_Explosion &data)
{
  if (!context.world.ready)
  {
    log_error("rocket_explosion handler invoked with no client world -- an effect "
              "arrived before any map was loaded, so there is no surface to "
              "resolve the decal against");
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
    // probe start clear of the wall; the 12-unit reach is well within typical
    // rocket hull thickness so we still land on the surface. The map through
    // the hidden set the draw uses: a decal belongs on a wall you can see,
    // never on a gate that is switched off, and never on a player.
    constexpr float probe_step  = 4.f;
    constexpr float probe_depth = 12.f;
    const vec3f     surface_normal = linalg::normalize(data.normal);
    const vec3f     probe_from     = data.origin + surface_normal * probe_step;

    shared::disabled_geometry_t hidden;
    shared::collect_hidden_geometry(context.world.session.entity_system,
                                    context.world.session.owner_of, hidden);

    ray_hit_result_t hit;
    const bool surface_hit = bvh_intersect_ray(context.world.session.bvh, probe_from,
                                               surface_normal * -1.f, hit, hidden) &&
                             hit.t <= probe_step + probe_depth;

    if (surface_hit)
    {
      const vec3f decal_position = probe_from - surface_normal * hit.t;
      log_terminal("[CLIENT FX] rocket_explosion at ({:.1f},{:.1f},{:.1f}) "
                   "→ decal at ({:.1f},{:.1f},{:.1f}) n=({:.2f},{:.2f},{:.2f})",
                   data.origin.x, data.origin.y, data.origin.z,
                   decal_position.x, decal_position.y, decal_position.z,
                   hit.normal.x, hit.normal.y, hit.normal.z);
    }
    else
    {
      // Server saw a surface but the client doesn't -- usually the client's
      // world is out of sync (mid-load, late connect). Log and fall through
      // to a particle-only explosion.
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
    context.audio->play_3d(assets::sound_asset::rocket_explosion, data.origin);
}

} // namespace client::effects
