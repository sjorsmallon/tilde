#pragma once

// The ONE conversion from a Light component to a radiance, and the reference
// distance that gives `intensity` a meaning. lighting_def.md ss10 and ss11 are the
// design; ss11 is why this is a shared header rather than a static in the solve.

#include "array.hpp"
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
enum class light_kind_t : uint8_t
{
  Point,
  Spot,
  Directional
};

// A DIRECTIONAL light has no distance for the reference to be measured at: its
// arrival attenuates by 1 everywhere, so multiplying by REF^2 there made an
// intensity of 1 deliver 1550 -- every surface under the sun burned white and
// no exposure could hold both it and a lamp. For the sun `intensity` IS the
// irradiance it delivers, the same number a point light delivers at one metre.
[[nodiscard]]
inline linalg::vec3 radiance_of(const entities::Light &light, light_kind_t kind)
{
  if (kind == light_kind_t::Directional) return light.color * light.intensity;
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

  // Gate 9. Whether the light asked for a shadow map; the renderer decides which
  // LAYER it gets per frame, ranked against the pool. Both copies of a Mixed
  // light carry the same uid, which is how they end up naming the same layer.
  bool         casts_shadows = true;
  entity_uid_t uid           = null_entity_uid;
};

// What a light's shadow map is rendered and sampled through (gate 9). The texel
// size is measured ONE UNIT from the light, so the receiver's normal offset is
// `texel_size_at_unit_distance * distance` -- the same distance-divided-out
// spelling source_radius uses for a directional light.
struct shadow_projection_t
{
  linalg::mat4f view_projection;
  float         texel_size_at_unit_distance = 0.f;
  // near_plane 0 means orthographic (shadow_linear_depth); a perspective map's is SHADOW_NEAR_PLANE
  float         near_plane = 0.f;
  float         far_plane  = 0.f;
};

inline constexpr float SHADOW_NEAR_PLANE = 1.f;

// A spot light's frustum IS its cone: outer angle as the fov, range as the far
// plane. The cone is clamped to [1, 85] degrees of half angle, since a
// perspective map cannot hold a hemisphere.
[[nodiscard]] shadow_projection_t spot_shadow_projection(const scene_light_t &light,
                                                         uint32_t             resolution);

// --- Cascades, the directional light's shadow map (gate 9 step 2) ---
//
// A directional light has no extent, so one orthographic map over the view
// frustum spreads its texels over the whole level. The frustum is cut into
// slices by view depth, and each slice gets its own map: a CASCADE. Every
// cascade is one layer of the pool, consecutive from the layer the light names,
// and the receiver picks by view depth (direct_light.glsl). scene.glsl spells
// this count as a literal.
inline constexpr uint32_t MAX_SHADOW_CASCADES = 4;

// The camera a cascade fit is made against, reduced to what the fit reads. The
// renderer translates its view into this; a test builds one by hand.
struct shadow_view_t
{
  linalg::vec3 position{0.f, 0.f, 0.f};
  linalg::vec3 forward{1.f, 0.f, 0.f};
  linalg::vec3 right{0.f, 0.f, 1.f};
  linalg::vec3 up{0.f, 1.f, 0.f};
  float        tan_half_fov_y     = 1.f; // 0 means orthographic: read ortho_half_height
  float        aspect             = 1.f;
  float        ortho_half_height  = 0.f;
  float        near_plane         = 1.f;
  // Read only by the point-light face cull (below): a face is drawn only if
  // its frustum meets the camera's, and that needs the whole camera frustum.
  float        far_plane          = 50000.f;
};

struct cascade_settings_t
{
  uint32_t count  = 3;    // clamped to [1, MAX_SHADOW_CASCADES]
  // The practical split: 0 is uniform in depth, 1 is logarithmic (each cascade
  // the same texel density at its far edge), and a blend of the two between.
  float    lambda = 0.7f;
  // How far from the camera the last cascade reaches. Past it the sun casts no
  // runtime shadow and the receiver reads fully lit.
  float    far_distance = 4096.f;
  // How far TOWARD the light a cascade's box extends past its slice, so a
  // caster between the sun and the slice -- a roof, a tower -- is in the map.
  float    caster_extent = 8192.f;
};

// One cascade: its map, the slice it covers and the geometry a debug view draws.
struct shadow_cascade_t
{
  shadow_projection_t projection;
  float               near_depth = 0.f; // view depths bounding the slice
  float               far_depth  = 0.f;
  // The slice's bounding SPHERE, which is what the ortho box is fit to: a
  // sphere is the one bound a camera turn cannot change, so the map's texel
  // grid stays put and the shadow does not shimmer.
  linalg::vec3        sphere_center{0.f, 0.f, 0.f};
  float               sphere_radius = 0.f;
  // The frustum slice, near quad then far quad, then the ortho box, light-near
  // quad then light-far quad. r_shadow_freeze draws both.
  Array<linalg::vec3, 8> slice_corners;
  Array<linalg::vec3, 8> box_corners;
};

struct shadow_cascades_t
{
  Array<shadow_cascade_t, MAX_SHADOW_CASCADES> cascades;
  uint32_t                                     count = 0;
  linalg::vec3                                 light_direction{0.f, -1.f, 0.f};
  linalg::vec3                                 camera_position{0.f, 0.f, 0.f};
};

// View depth of split `index` in [0, count]: 0 is the near plane, count is
// far_distance, and the ones between are Zhang's practical split.
[[nodiscard]] float cascade_split_depth(const shadow_view_t &view, const cascade_settings_t &settings,
                                        uint32_t index);

// Fits `settings.count` cascades of a directional light to the view. Each ortho
// box is a square of the slice sphere's diameter, its origin snapped to a whole
// texel of that map, reaching caster_extent toward the light.
[[nodiscard]] shadow_cascades_t directional_shadow_cascades(const scene_light_t     &light,
                                                            const shadow_view_t     &view,
                                                            const cascade_settings_t &settings,
                                                            uint32_t                  resolution);

// --- The point-light cube, six faces as six layers (gate 9 step 3) ---
//
// A point light has no forward, so its map is six 90-degree perspective faces
// in the order +X, -X, +Y, -Y, +Z, -Z -- consecutive layers of the pool from
// the one the light names, the receiver picking by the MAJOR AXIS of the
// light-to-point vector (direct_light.glsl's point_shadow_face). No cube
// sampler and no cube image: the seam between two faces is where the pick
// changes layer, and the PCF kernel near it would read outside the map, so
// every face is widened by POINT_SHADOW_FACE_GUARD_TEXELS past the 45-degree
// edge -- the pick is exact, the map is a little bigger than the pick needs.
// The guard must exceed the receiver's kernel radius plus its normal offset;
// the shader clamps the radius to 3.
inline constexpr uint32_t POINT_SHADOW_FACE_COUNT       = 6;
inline constexpr float    POINT_SHADOW_FACE_GUARD_TEXELS = 4.f;

// The face a direction from the light falls in, in the order above: the
// C++ twin of the shader's pick, pinned by shadow_test.
[[nodiscard]] uint32_t point_shadow_face_of(const linalg::vec3 &light_to_point);

// One face: its map, whether the camera can see anything it covers, and the
// pyramid a debug view draws -- the apex then the four far corners.
struct point_shadow_face_t
{
  shadow_projection_t    projection;
  bool                   visible = true;
  Array<linalg::vec3, 5> corners;
};

struct point_shadow_faces_t
{
  Array<point_shadow_face_t, POINT_SHADOW_FACE_COUNT> faces;
  linalg::vec3                                        position{0.f, 0.f, 0.f};
  float                                               range = 0.f;
};

// The six faces of one point light, and which of them the view can see. A face
// whose frustum and the camera's are separated by one of either's planes is
// culled: nothing the camera draws can read it, so it is not rendered. The
// test is conservative -- it keeps a face it cannot prove hidden.
[[nodiscard]] point_shadow_faces_t point_shadow_faces(const scene_light_t &light,
                                                      const shadow_view_t &view,
                                                      uint32_t             resolution);

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
