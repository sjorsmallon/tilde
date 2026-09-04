#include "lightmap_lights.hpp"

#include "brush.hpp"
#include "entities/generated/entities_generated.hpp"
#include "lightmap_bake.hpp"
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

    if (!light_is_baked(light->mode)) continue;

    lights.push_back({entry.uid, *light});
  }

  return lights;
}

Bounding_Volume_Hierarchy build_occluder_bvh(const map_t &map)
{
  std::vector<BVH_Input> inputs;
  inputs.reserve(map.geometry.size());

  for (const map_geometry_t &entry : map.geometry)
  {
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
      if (triangles.empty())
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
    if (pieces.empty())
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

bool shadow_ray_reaches(const Bounding_Volume_Hierarchy &bvh,
                        const linalg::vec3 &surface_position,
                        const linalg::vec3 &surface_normal,
                        const linalg::vec3 &direction, float distance,
                        float shadow_ray_bias)
{
  const linalg::vec3 origin = surface_position + surface_normal * shadow_ray_bias;

  ray_hit_result_t hit = {};
  if (!bvh_intersect_ray(bvh, origin, direction, hit)) return true;

  return !(hit.hit && hit.t > 0.f && hit.t < distance - shadow_ray_bias);
}

uint32_t hash_mix(uint32_t hash, uint32_t value)
{
  for (int byte = 0; byte < 4; ++byte)
  {
    hash ^= (value >> (byte * 8)) & 0xffu;
    hash *= 16777619u;
  }
  return hash;
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
        if (light_visibility(bvh, position, sample.normal, arrival, shadow_ray_bias, 1,
                             sample_hash(texel_x, texel_y, 0, 0)) > 0.f)
          ++face.visible;
      }

    report.push_back(face);
  }

  return report;
}


float light_visibility(const Bounding_Volume_Hierarchy &bvh,
                       const linalg::vec3 &surface_position,
                       const linalg::vec3 &surface_normal,
                       const light_arrival_t &arrival, float shadow_ray_bias,
                       int soft_shadow_samples, uint32_t hash)
{
  const int sample_count =
      arrival.shadow_disc_radius > 0.f ? std::max(soft_shadow_samples, 1) : 1;

  if (sample_count == 1)
    return shadow_ray_reaches(bvh, surface_position, surface_normal, arrival.direction,
                              arrival.distance, shadow_ray_bias)
               ? 1.f
               : 0.f;

  // The emitter is a sphere and this samples the DISC facing the surface, which
  // is the sphere's silhouette from here -- the half of it the surface cannot see
  // is the half that emits nothing toward it.
  linalg::vec3 tangent_u;
  linalg::vec3 tangent_v;
  brush_face_grid_tangents(arrival.direction, tangent_u, tangent_v);

  const linalg::vec3 centre = surface_position + arrival.direction * arrival.distance;

  // The golden angle: consecutive samples land as far from each other in rotation
  // as an irrational turn allows, so a handful of them cover the disc evenly
  // instead of clumping the way independent random angles do.
  constexpr float GOLDEN_ANGLE = 2.39996323f;
  constexpr float TWO_PI = 6.28318531f;

  int reached = 0;
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

    const linalg::vec3 target = centre + tangent_u * (std::cos(angle) * radius) +
                                tangent_v * (std::sin(angle) * radius);

    const linalg::vec3 to_target = target - surface_position;
    const float distance = std::sqrt(linalg::dot(to_target, to_target));
    if (distance < 1e-4f) continue;

    if (shadow_ray_reaches(bvh, surface_position, surface_normal,
                           to_target * (1.f / distance), distance, shadow_ray_bias))
      ++reached;
  }

  return (float)reached / (float)sample_count;
}

} // namespace shared
