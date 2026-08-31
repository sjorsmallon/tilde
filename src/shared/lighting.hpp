#pragma once

// The ONE conversion from a Light component to a radiance, and the reference
// distance that gives `intensity` a meaning. lighting_def.md ss10 and ss11 are the
// design; ss11 is why this is a shared header rather than a static in the solve.

#include "entities/generated/entities_generated.hpp"
#include "linalg.hpp"

namespace shared
{

// One metre, in engine units -- entities.def fixes 1 unit = 1 inch.
//
// `intensity` is DEFINED as the irradiance a light delivers at this distance, so
// radiance is `color * intensity * REF^2`. Without it the falloff is a true
// inverse square in inches: the default intensity of 1, at 100 units (eight feet,
// a lamp to a wall), attenuates to 1e-4 and bakes BLACK. Every metric reference
// number, tutorial and artist intuition sits 39.37^2 ~ 1550x away from an authored
// value, which is a unit problem and not a tuning one.
//
// This is a PLACEHOLDER FOR EXPOSURE, not a rival unit system: `intensity * REF^2`
// IS candela with the scale factor written down, so it chooses where the decimal
// point sits on the same axis. When a camera model with an EV control arrives,
// every map's lights want rescaling -- one multiply per light, scriptable over the
// .source files -- and that is a migration to plan rather than a surprise.
inline constexpr float LIGHT_REFERENCE_DISTANCE = 39.3701f;

// Nothing multiplies `color` by `intensity` itself. The bake, the runtime gather
// pass and every tool that shows a light go through here, because the reference
// distance living at a call site is how the three quietly become three lighting
// models -- which is exactly what shader_editor_state's hardcoded 1500 and 30000
// already are.
[[nodiscard]] inline linalg::vec3 radiance_of(const entities::Light &light)
{
  return light.color * (light.intensity * LIGHT_REFERENCE_DISTANCE *
                        LIGHT_REFERENCE_DISTANCE);
}

} // namespace shared
