#include "lightmap_lights.hpp"

#include "brush.hpp"
#include "entities/generated/entities_generated.hpp"
#include "lightmap_bake.hpp"
#include "lightmap_trace.hpp"
#include "log.hpp"
#include "map_geometry.hpp"
#include "shader_math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace shared
{

namespace
{

// The falloff and the cone are shader_math.hpp's, which is
// resources/shaders/light_falloff.glsl compiled as C++ -- the same text the
// shaders compile as GLSL. It used to be a copy of pbr.frag's, with a comment
// saying so; lighting_def.md decision I is why a copy was not good enough.
using shader_math::distance_attenuation;
using shader_math::spot_cone_factor;

} // namespace

std::vector<baked_light_t> collect_lights(const map_t &map)
{
  std::vector<baked_light_t> lights;

  for (const map_entity_t &entry : map.entities)
  {
    if (!entry.entity)
      continue;

    const std::optional<scene_light_t> light = try_light_of(*entry.entity);
    if (!light) continue;

    // A light the author left switched off is off for the bake too: baking it
    // and then having the runtime gather skip it would leave every chart naming
    // a slot whose radiance is permanently zero.
    if (!light_is_switched_on(*entry.entity)) continue;

    if (!light_is_baked(light->mode)) continue;

    lights.push_back({entry.uid, *light});
  }

  return lights;
}

// The ONE walk, over whichever half of the map the caller asked for. Two calls
// with opposite answers partition the geometry exactly, so a brush cannot be in
// both sets or in neither -- which a second hand-written loop could not promise.
static Bounding_Volume_Hierarchy build_geometry_bvh(const map_t &map,
                                                   light_occlusion_t wanted)
{
  const bool occluding = wanted == light_occlusion_t::Opaque;
  std::vector<BVH_Input> inputs;
  inputs.reserve(map.geometry.size());

  for (const map_geometry_t &entry : map.geometry)
  {
    if (light_occlusion_of(entry.value, map.materials) != wanted)
      continue;

    // A static mesh COLLIDES as its bound and must not SHADOW as it: a texel on
    // a sphere sits inside the sphere's box, and every ray from it would start
    // in shadow. So the bake's own BVH gets one zero-thickness convex piece per
    // triangle -- the triangle's plane both ways and its three edge planes --
    // which the slab test handles exactly, and which is two-sided by
    // construction. The runtime BVH is untouched.
    if (get_kind(entry.value) == geometry_kind_t::Static_Mesh)
    {
      const std::vector<world_triangle_t> triangles =
          static_mesh_world_triangles(std::get<static_mesh_geometry_t>(entry.value));
      if (triangles.empty() && occluding)
        log_warning("[lightmap] static mesh {} names no mesh that resolves and casts no "
                    "shadow.", entry.uid);

      for (const world_triangle_t &triangle : triangles)
      {
        if (triangle.is_degenerate()) continue;

        BVH_Input input;
        input.id = {Collision_Id::Type::Static_Geometry, entry.uid};
        input.collision_planes.push_back({triangle.corners[0], triangle.normal});
        input.collision_planes.push_back({triangle.corners[0], triangle.normal * -1.f});
        for (int edge = 0; edge < 3; ++edge)
        {
          const linalg::vec3 &a = triangle.corners[edge];
          const linalg::vec3 &b = triangle.corners[(edge + 1) % 3];
          const linalg::vec3 &opposite = triangle.corners[(edge + 2) % 3];
          linalg::vec3 outward = linalg::normalize(linalg::cross(b - a, triangle.normal));
          if (linalg::dot(opposite - a, outward) > 0.f) outward = outward * -1.f;
          input.collision_planes.push_back({a, outward});
        }

        // The bound is a broadphase and a flat triangle has none on one axis;
        // padding it costs a rejected slab test and buys never depending on
        // how the traversal treats a zero-extent box.
        constexpr float BOUND_PADDING = 0.5f;
        input.aabb.min = triangle.corners[0];
        input.aabb.max = triangle.corners[0];
        for (const linalg::vec3 &corner : triangle.corners)
        {
          input.aabb.min = {std::min(input.aabb.min.x, corner.x),
                            std::min(input.aabb.min.y, corner.y),
                            std::min(input.aabb.min.z, corner.z)};
          input.aabb.max = {std::max(input.aabb.max.x, corner.x),
                            std::max(input.aabb.max.y, corner.y),
                            std::max(input.aabb.max.z, corner.z)};
        }
        input.aabb.min = input.aabb.min - linalg::vec3{BOUND_PADDING, BOUND_PADDING, BOUND_PADDING};
        input.aabb.max = input.aabb.max + linalg::vec3{BOUND_PADDING, BOUND_PADDING, BOUND_PADDING};
        inputs.push_back(std::move(input));
      }
      continue;
    }

    const std::vector<collision_piece_t> pieces =
        get_collision_pieces(entry.value, entry.uid);
    if (pieces.empty() && occluding)
      log_warning("[lightmap] object {} has no collision pieces and casts no shadow.",
                  entry.uid);

    for (const collision_piece_t &piece : pieces)
    {
      BVH_Input input;
      input.aabb = piece.bounds;
      input.id = {Collision_Id::Type::Static_Geometry, entry.uid};
      input.collision_planes = piece.planes;
      inputs.push_back(input);
    }
  }

  return build_bvh(inputs);
}

Bounding_Volume_Hierarchy build_occluder_bvh(const map_t &map)
{
  return build_geometry_bvh(map, light_occlusion_t::Opaque);
}

Bounding_Volume_Hierarchy build_alpha_tested_bvh(const map_t &map)
{
  return build_geometry_bvh(map, light_occlusion_t::Alpha_Tested);
}

Bounding_Volume_Hierarchy build_transmissive_bvh(const map_t &map)
{
  return build_geometry_bvh(map, light_occlusion_t::Transmissive);
}

light_arrival_t arrival_at(const scene_light_t &light,
                           const linalg::vec3 &surface_position,
                           const linalg::vec3 &surface_normal,
                           float directional_shadow_distance)
{
  light_arrival_t arrival;

  if (light.kind == light_kind_t::Directional)
  {
    arrival.direction = linalg::normalize(light.forward * -1.f);
    arrival.distance = directional_shadow_distance;
    arrival.attenuation = 1.f;
    arrival.shadow_disc_radius = light.source_radius * arrival.distance;
  }
  else
  {
    const linalg::vec3 to_light = light.position - surface_position;
    const float squared_distance = linalg::dot(to_light, to_light);
    arrival.distance = std::sqrt(squared_distance);

    if (arrival.distance > light.range || arrival.distance < 1e-4f) return arrival;

    arrival.direction = to_light * (1.f / arrival.distance);
    arrival.attenuation =
        distance_attenuation(squared_distance, light.range, light.source_radius);
    arrival.shadow_disc_radius = light.source_radius;

    if (light.kind == light_kind_t::Spot)
    {
      const float cos_angle =
          linalg::dot(arrival.direction * -1.f, linalg::normalize(light.forward));
      const float spot_factor =
          spot_cone_factor(cos_angle, light.cos_inner, light.cos_outer);
      if (spot_factor <= 0.f) return arrival;
      arrival.attenuation *= spot_factor;
    }
  }

  if (arrival.attenuation <= 0.f) return arrival;
  arrival.arrives = true;

  arrival.normal_dot_light = linalg::dot(surface_normal, arrival.direction);
  if (arrival.normal_dot_light <= 0.f) return arrival;

  arrival.reaches = true;
  return arrival;
}

// The opaque half: did anything stop this ray before the light? The test the
// whole shadow term was, unchanged, and still the first thing asked.
static bool shadow_ray_is_unoccluded(const Bounding_Volume_Hierarchy &bvh,
                                     const linalg::vec3 &origin,
                                     const linalg::vec3 &direction, float travel)
{
  ray_hit_result_t hit = {};
  if (!bvh_intersect_ray(bvh, origin, direction, hit)) return true;

  return !(hit.hit && hit.t > 0.f && hit.t < travel);
}

// The transmissive half: the product of what the ray crossed on its way to the
// light. Walked ENTRY to ENTRY -- a brush is a convex solid, so `t_exit` is
// where the ray leaves the piece it just tinted by, and stepping a hair past `t`
// instead would re-enter the same pane forever.
//
// One tint per PIECE crossed, taken from the face the ray entered through: a
// pane is a thin brush and tints once. There is no absorption over a thickness
// and deliberately no exit-face tint -- a solid the light passes through is
// authored as the filter it is, not as two surfaces with a medium between them.
static linalg::vec3 transmittance_along(const traced_scene_t &surfaces,
                                        const Bounding_Volume_Hierarchy &bvh,
                                        const linalg::vec3 &origin,
                                        const linalg::vec3 &direction, float travel)
{
  linalg::vec3 transmittance{1.f, 1.f, 1.f};

  // A ray crossing more panes than this is a bug in the map or in `t_exit`, and
  // an unbounded march is a bake that never finishes.
  constexpr int MAX_CROSSINGS = 16;
  constexpr float STEP_EPSILON = 1e-3f;

  float travelled = 0.f;
  for (int crossing = 0; crossing < MAX_CROSSINGS; ++crossing)
  {
    ray_hit_result_t hit = {};
    const linalg::vec3 at = origin + direction * travelled;
    if (!bvh_intersect_ray(bvh, at, direction, hit)) break;
    if (!hit.hit || hit.t >= travel - travelled) break;

    // A hit at t ~ 0 is the piece we just LEFT, reported again because the march
    // resumes on its exit face -- the entry parameter of a solid the origin is
    // already on. Stepping past it without tinting is what keeps one crossing
    // one filter; counting it squared every pane.
    if (hit.t > STEP_EPSILON)
    {
      transmittance = multiply_channels(
          transmittance, transmittance_at(surfaces, hit, at + direction * hit.t));
      if (transmittance.x <= 0.f && transmittance.y <= 0.f && transmittance.z <= 0.f)
        return {0.f, 0.f, 0.f};
    }

    travelled += std::max(hit.t_exit, hit.t) + STEP_EPSILON;
  }

  return transmittance;
}

// The alpha-tested half: a fence stops the ray where its texel is opaque and
// passes it where the texel is cut away. Asked with the opaque test and not with
// the tint, because what it decides is the same thing -- whether the ray got
// through -- and a cutout that passes passes WHOLE: its alpha is a coverage that
// has already been resolved to yes or no, not a filter.
static bool alpha_test_lets_the_ray_through(const traced_scene_t &surfaces,
                                            const Bounding_Volume_Hierarchy &bvh,
                                            const linalg::vec3 &origin,
                                            const linalg::vec3 &direction, float travel)
{
  constexpr int MAX_CROSSINGS = 16;
  constexpr float STEP_EPSILON = 1e-3f;

  float travelled = 0.f;
  for (int crossing = 0; crossing < MAX_CROSSINGS; ++crossing)
  {
    ray_hit_result_t hit = {};
    const linalg::vec3 at = origin + direction * travelled;
    if (!bvh_intersect_ray(bvh, at, direction, hit)) break;
    if (!hit.hit || hit.t >= travel - travelled) break;

    // Solid texel, solid shadow: a bar stops the ray and the gap beside it does
    // not, which is the whole of what an alpha-tested occluder is.
    if (hit.t > STEP_EPSILON && alpha_test_is_solid_at(surfaces, hit, at + direction * hit.t))
      return false;

    travelled += std::max(hit.t_exit, hit.t) + STEP_EPSILON;
  }
  return true;
}

// The nearest hit on a fence whose texel is SOLID, which is the only part of one
// a ray can land on or be stopped by.
static bool nearest_solid_alpha_tested_hit(const traced_scene_t &surfaces,
                                           const Bounding_Volume_Hierarchy &bvh,
                                           const linalg::vec3 &origin,
                                           const linalg::vec3 &direction, float travel,
                                           ray_hit_result_t &out_hit)
{
  constexpr int MAX_CROSSINGS = 16;
  constexpr float STEP_EPSILON = 1e-3f;

  float travelled = 0.f;
  for (int crossing = 0; crossing < MAX_CROSSINGS; ++crossing)
  {
    ray_hit_result_t hit = {};
    const linalg::vec3 at = origin + direction * travelled;
    if (!bvh_intersect_ray(bvh, at, direction, hit)) break;
    if (!hit.hit || hit.t >= travel - travelled) break;

    if (hit.t > STEP_EPSILON && alpha_test_is_solid_at(surfaces, hit, at + direction * hit.t))
    {
      out_hit = hit;
      // The caller measures from ITS origin, not from where the march resumed.
      out_hit.t += travelled;
      out_hit.t_exit += travelled;
      return true;
    }

    travelled += std::max(hit.t_exit, hit.t) + STEP_EPSILON;
  }
  return false;
}

bool trace_nearest_surface(const shadow_scene_t &scene, const linalg::vec3 &origin,
                           const linalg::vec3 &direction, ray_hit_result_t &out_hit)
{
  if (!scene.occluders) return false;

  ray_hit_result_t opaque = {};
  const bool hit_opaque = bvh_intersect_ray(*scene.occluders, origin, direction, opaque) &&
                          opaque.hit && opaque.t > 0.f;

  if (!scene.alpha_tested || !scene.surfaces)
  {
    out_hit = opaque;
    return hit_opaque;
  }

  // Only as far as the opaque hit: a fence behind a wall is not what this ray
  // lands on, and marching past one is work with no answer in it.
  const float travel = hit_opaque ? opaque.t : std::numeric_limits<float>::max();
  ray_hit_result_t fence = {};
  if (!nearest_solid_alpha_tested_hit(*scene.surfaces, *scene.alpha_tested, origin, direction,
                                      travel, fence))
  {
    out_hit = opaque;
    return hit_opaque;
  }

  out_hit = fence;
  return true;
}

linalg::vec3 segment_transmittance(const shadow_scene_t &scene, const linalg::vec3 &origin,
                                   const linalg::vec3 &direction, float travel)
{
  if (!scene.surfaces || !scene.transmissive) return {1.f, 1.f, 1.f};
  return transmittance_along(*scene.surfaces, *scene.transmissive, origin, direction, travel);
}

linalg::vec3 shadow_ray_transmittance(const shadow_scene_t &scene,
                                      const linalg::vec3 &surface_position,
                                      const linalg::vec3 &surface_normal,
                                      const linalg::vec3 &direction, float distance,
                                      float shadow_ray_bias)
{
  if (!scene.occluders) return {1.f, 1.f, 1.f};

  const linalg::vec3 origin = surface_position + surface_normal * shadow_ray_bias;
  const float travel = distance - shadow_ray_bias;

  if (!shadow_ray_is_unoccluded(*scene.occluders, origin, direction, travel))
    return {0.f, 0.f, 0.f};

  if (!scene.surfaces) return {1.f, 1.f, 1.f};

  if (scene.alpha_tested &&
      !alpha_test_lets_the_ray_through(*scene.surfaces, *scene.alpha_tested, origin, direction,
                                       travel))
    return {0.f, 0.f, 0.f};

  return segment_transmittance(scene, origin, direction, travel);
}

uint32_t sample_hash(int atlas_x, int atlas_y, int page, int sample_index)
{
  uint32_t hash = 2166136261u;
  hash = hash_mix(hash, (uint32_t)atlas_x);
  hash = hash_mix(hash, (uint32_t)atlas_y);
  hash = hash_mix(hash, (uint32_t)page);
  hash = hash_mix(hash, (uint32_t)sample_index);
  return hash;
}

float luminance_of(const linalg::vec3 &linear_rgb)
{
  return 0.2126f * linear_rgb.x + 0.7152f * linear_rgb.y + 0.0722f * linear_rgb.z;
}

std::vector<light_reach_on_face_t> probe_light_reach(
    const map_t &map, const baked_light_t &light, const lightmap_bake_settings_t &settings,
    float shadow_ray_bias, float directional_shadow_distance, int max_samples_per_axis)
{
  // Charts built fresh rather than read off the map's bake: a face is asked about
  // as it is NOW, which after a move is not what the last bake saw.
  const std::vector<lightmap_chart_t> charts = build_lightmap_charts(map, settings);
  const Bounding_Volume_Hierarchy bvh = build_occluder_bvh(map);
  const Bounding_Volume_Hierarchy alpha_tested = build_alpha_tested_bvh(map);
  const Bounding_Volume_Hierarchy transmissive = build_transmissive_bvh(map);
  const traced_scene_t glass = build_traced_scene(map, bvh, &alpha_tested, &transmissive);
  const shadow_scene_t shadow = shadow_scene_of(glass);
  const linalg::vec3 axis = light.light.kind == light_kind_t::Point
                                ? linalg::vec3{0.f, 0.f, 0.f}
                                : linalg::normalize(light.light.forward);

  std::vector<light_reach_on_face_t> report;
  report.reserve(charts.size());

  for (const lightmap_chart_t &chart : charts)
  {
    light_reach_on_face_t face;
    face.object_uid = chart.object_uid;
    face.normal = chart.plane.normal;
    face.nearest_distance = std::numeric_limits<float>::infinity();

    const int width = chart_covered_width(chart, settings);
    const int height = chart_covered_height(chart, settings);
    const int stride_x = std::max(1, width / std::max(max_samples_per_axis, 1));
    const int stride_y = std::max(1, height / std::max(max_samples_per_axis, 1));

    for (int texel_y = 0; texel_y < height; texel_y += stride_y)
      for (int texel_x = 0; texel_x < width; texel_x += stride_x)
      {
        const texel_sample_t sample = sample_texel(chart, texel_x, texel_y);
        if (!sample.on_surface) continue;
        ++face.sampled;

        const linalg::vec3 position = sample.position;

        if (light.light.kind != light_kind_t::Directional)
        {
          const linalg::vec3 to_light = light.light.position - position;
          const float distance = std::sqrt(linalg::dot(to_light, to_light));
          face.nearest_distance = std::min(face.nearest_distance, distance);
          if (light.light.kind == light_kind_t::Spot && distance > 1e-4f)
            face.best_cone_cos = std::max(
                face.best_cone_cos, linalg::dot(to_light * (-1.f / distance), axis));
        }
        else
        {
          face.nearest_distance = 0.f;
        }

        const light_arrival_t arrival = arrival_at(light.light, position, sample.normal,
                                                   directional_shadow_distance);
        if (!arrival.arrives) continue;
        ++face.arrives;
        if (arrival.reaches) ++face.reaches;

        // One hard ray: the probe asks whether ANYTHING gets through, and a
        // penumbra sample count is not what separates lit from black.
        if (luminance_of(light_visibility(shadow, position, sample.normal, arrival,
                                          shadow_ray_bias, 1,
                                          sample_hash(texel_x, texel_y, 0, 0))) > 0.f)
          ++face.visible;
      }

    report.push_back(face);
  }

  return report;
}


// ONE ray toward the point of the emitter's disc at (`radius`, `angle`) -- the
// primitive under both estimators below, so neither can pick its target
// differently from the other. The emitter is a sphere and the disc is its
// silhouette from the surface: the half of it the surface cannot see is the half
// that emits nothing toward it.
static linalg::vec3 shadow_ray_transmittance_to_disc_point(
    const shadow_scene_t &scene, const linalg::vec3 &surface_position,
    const linalg::vec3 &surface_normal, const light_arrival_t &arrival, float radius,
    float angle, float shadow_ray_bias)
{
  linalg::vec3 tangent_u;
  linalg::vec3 tangent_v;
  brush_face_grid_tangents(arrival.direction, tangent_u, tangent_v);

  const linalg::vec3 centre = surface_position + arrival.direction * arrival.distance;
  const linalg::vec3 target = centre + tangent_u * (std::cos(angle) * radius) +
                              tangent_v * (std::sin(angle) * radius);

  const linalg::vec3 to_target = target - surface_position;
  const float distance = std::sqrt(linalg::dot(to_target, to_target));
  if (distance < 1e-4f) return {0.f, 0.f, 0.f};

  return shadow_ray_transmittance(scene, surface_position, surface_normal,
                                  to_target * (1.f / distance), distance, shadow_ray_bias);
}

linalg::vec3 light_visibility(const shadow_scene_t &scene,
                              const linalg::vec3 &surface_position,
                              const linalg::vec3 &surface_normal,
                              const light_arrival_t &arrival, float shadow_ray_bias,
                              int soft_shadow_samples, uint32_t hash)
{
  const int sample_count = shadow_ray_count(arrival, soft_shadow_samples);

  if (sample_count == 1)
    return shadow_ray_transmittance(scene, surface_position, surface_normal, arrival.direction,
                                    arrival.distance, shadow_ray_bias);

  // The golden angle: consecutive samples land as far from each other in rotation
  // as an irrational turn allows, so a handful of them cover the disc evenly
  // instead of clumping the way independent random angles do.
  constexpr float GOLDEN_ANGLE = 2.39996323f;
  constexpr float TWO_PI = 6.28318531f;

  linalg::vec3 reached{0.f, 0.f, 0.f};
  for (int sample = 0; sample < sample_count; ++sample)
  {
    const uint32_t sample_bits = hash_mix(hash, (uint32_t)sample);

    // sqrt of the stratum, because a disc's area grows with the square of the
    // radius -- sampling the radius uniformly crowds every sample into the middle
    // and gives a penumbra a hard rim.
    const float radius_jitter = (float)(sample_bits & 0xffffu) * (1.f / 65536.f);
    const float angle_jitter = (float)((sample_bits >> 16) & 0xffffu) * (1.f / 65536.f);

    const float radius =
        arrival.shadow_disc_radius *
        std::sqrt(((float)sample + radius_jitter) / (float)sample_count);
    const float angle = (float)sample * GOLDEN_ANGLE + angle_jitter * TWO_PI;

    reached = reached + shadow_ray_transmittance_to_disc_point(
                            scene, surface_position, surface_normal, arrival, radius, angle,
                            shadow_ray_bias);
  }

  return reached * (1.f / (float)sample_count);
}

linalg::vec3 light_visibility_single_ray(const shadow_scene_t &scene,
                                         const linalg::vec3 &surface_position,
                                         const linalg::vec3 &surface_normal,
                                         const light_arrival_t &arrival, float shadow_ray_bias,
                                         uint32_t hash)
{
  if (arrival.shadow_disc_radius <= 0.f)
    return shadow_ray_transmittance(scene, surface_position, surface_normal, arrival.direction,
                                    arrival.distance, shadow_ray_bias);

  // Uniform over the disc's AREA: sqrt on the radius for the reason the spiral
  // takes it, and a full random turn where the spiral had a golden-angle step,
  // since there is no sequence here to spread.
  constexpr float TWO_PI = 6.28318531f;
  const float radius = arrival.shadow_disc_radius * std::sqrt(unit_float_from(hash));
  const float angle = TWO_PI * unit_float_from(hash_mix(hash, 0x68bc21ebu));

  return shadow_ray_transmittance_to_disc_point(scene, surface_position, surface_normal,
                                               arrival, radius, angle, shadow_ray_bias);
}

} // namespace shared
