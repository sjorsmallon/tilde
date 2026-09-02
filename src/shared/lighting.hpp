#pragma once

// The ONE conversion from a Light component to a radiance, and the reference
// distance that gives `intensity` a meaning. lighting_def.md ss10 and ss11 are the
// design; ss11 is why this is a shared header rather than a static in the solve.

#include "entities/generated/entities_generated.hpp"
#include "lightmap.hpp"
#include "linalg.hpp"

#include <optional>
#include <vector>

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
[[nodiscard]]
inline linalg::vec3 radiance_of(const entities::Light &light)
{
  return light.color * (light.intensity * LIGHT_REFERENCE_DISTANCE *
                        LIGHT_REFERENCE_DISTANCE);
}


[[nodiscard]]
inline bool light_is_baked(entities::Light_Mode mode)
{
  return mode != entities::Light_Mode::Dynamic;
}

[[nodiscard]] 
inline bool light_is_analytic(entities::Light_Mode mode)
{
  return mode != entities::Light_Mode::Baked;
}

enum class light_kind_t : uint8_t
{
  Point,
  Spot,
  Directional
};

struct scene_light_t
{
  light_kind_t kind = light_kind_t::Point;
  entities::Light_Mode mode = entities::Light_Mode::Baked;
  linalg::vec3 position{0.f, 0.f, 0.f};
  linalg::vec3 forward{0.f, -1.f, 0.f}; // basis_from(orientation).forward
  linalg::vec3 radiance{1.f, 1.f, 1.f};
  float        range     = 0.f;
  float        cos_inner = 1.f;
  float        cos_outer = 0.f;

  // The emitter's RADIUS: world units for a point or a spot, and for a
  // DIRECTIONAL light the tangent of half its angular diameter, which is the
  // same sphere measured at unit distance. That is what lets the shader and the
  // bake take one radius with no light-type branch -- a directional arrival
  // reports a distance of 1, so `source_radius * distance` is the disc either
  // kind subtends either way.
  //
  // Zero is a punctual light and is exactly what everything did before area
  // lights existed: no penumbra in the bake, an unbroadened highlight, and the
  // unclamped inverse square.
  float        source_radius = 0.f;

  // Which slot of the map's bake this light is, or LIGHTMAP_NO_LIGHT_SLOT.
  // try_light_of does NOT fill it -- a light entity knows nothing about a bake
  // -- so the GATHER pass resolves it through find_baked_light_slot, which is
  // the one place a bake's uid table meets a frame's light array.
  //
  // It is what the shader needs and not a bool: a lightmapped surface carries a
  // visibility per slot, so "which of my four channels is this light" is a
  // comparison against this number. A light with no slot is one the atlas knows
  // nothing about, and is shaded with no baked shadow at all.
  int16_t      baked_slot = -1;
};

// The ONE fold from the three authoring light types; empty means "not a light".
// It does NOT filter by mode -- see scene_light_t::mode.
[[nodiscard]] std::optional<scene_light_t> try_light_of(const entities::Entity &entity);

// A frame's light array, LAID OUT the way scene.glsl reads it. Two regions, and
// the split is who evaluates what:
//
//   [0, baked_count)  indexed by BAKED SLOT. A lightmapped surface reads the
//                     four entries its chart named at bake time and never walks
//                     the rest, which is what makes a map with sixty lights cost
//                     a brush face exactly four -- the atlas IS the light
//                     culling (lighting_def.md ss14 step 6).
//   [baked_count, N)  the tail: every light the bake never saw, plus a SECOND
//                     COPY of every Mixed one. It is what a surface with no
//                     chart evaluates, and it is small by construction, which is
//                     the premise ss4 rests the forward renderer on.
//
// The two are filled by the two functions below and never separately, because a
// count that disagrees with the array it describes is a chart resolving to the
// wrong light.
struct frame_lights_t
{
  std::vector<scene_light_t> entries;
  uint32_t                   baked_count = 0;
};

// Sizes and clears the slot-indexed region from the bake's resolve table. A slot
// no live light claims keeps ZERO radiance and contributes nothing, which is the
// self-healing answer for a light entity deleted since the bake -- the chart
// still names the slot and the slot now says "no light here".
void begin_frame_lights(frame_lights_t &frame, const lightmap_t &lightmap);

// Places one entity, if it is a light at all. Called once per candidate entity
// between begin_frame_lights and the draw; nothing else may touch `entries`.
//
// The uid is a PARAMETER because its two callers spell it differently -- a
// session entity carries it as `entity_id`, a map entry as `map_entity_t::uid`
// -- and it is what the bake's resolve table is keyed by.
void add_frame_light(frame_lights_t &frame, const lightmap_t &lightmap,
                     entity_uid_t uid, const entities::Entity &entity);

} // namespace shared
