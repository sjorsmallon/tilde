#include "lightmap_trace.hpp"

#include "brush.hpp"
#include "map_geometry.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

namespace
{

constexpr float PI = 3.14159265f;
constexpr float TWO_PI = 6.28318531f;

linalg::vec3 multiply_channels(const linalg::vec3 &left, const linalg::vec3 &right)
{
  return {left.x * right.x, left.y * right.y, left.z * right.z};
}

// Cosine-weighted, which is what makes the bounce weight `weight *= albedo` and
// nothing else: the cos and the 1/pi of a diffuse BRDF cancel against the
// sampling density. Not a shortcut -- it is why everyone samples this way, and it
// is what keeps lighting_def.md ss9's "the 1/pi is the shader's" true for the
// bounce as well.
linalg::vec3 cosine_hemisphere_direction(const linalg::vec3 &normal, uint32_t bits)
{
  linalg::vec3 tangent_u;
  linalg::vec3 tangent_v;
  brush_face_grid_tangents(normal, tangent_u, tangent_v);

  const float radius_stratum = unit_float_from(bits);
  const float angle = TWO_PI * unit_float_from(hash_mix(bits, 0x68bc21ebu));

  const float radius = std::sqrt(radius_stratum);
  const float height = std::sqrt(std::max(0.f, 1.f - radius_stratum));

  return linalg::normalize(tangent_u * (radius * std::cos(angle)) +
                           tangent_v * (radius * std::sin(angle)) + normal * height);
}

// The FIRST leg only, and the difference from the sampler above is the whole of
// the projection's variance story. A chain's continuation is cosine-weighted
// because that is what makes the bounce weight `weight *= albedo` and nothing
// else; the first leg is what a coefficient is weighted by, and cosine sampling
// there would have to be divided back out by 1/cos -- unbounded at a grazing
// ray, which is a firefly in the tangential L1 components rather than merely
// noise. Both are unbiased (lighting_def.md gate 2 says so); this one has a
// bounded weight.
linalg::vec3 uniform_hemisphere_direction(const linalg::vec3 &normal, uint32_t bits)
{
  linalg::vec3 tangent_u;
  linalg::vec3 tangent_v;
  brush_face_grid_tangents(normal, tangent_u, tangent_v);

  const float height = unit_float_from(bits);
  const float radius = std::sqrt(std::max(0.f, 1.f - height * height));
  const float angle = TWO_PI * unit_float_from(hash_mix(bits, 0x68bc21ebu));

  return linalg::normalize(tangent_u * (radius * std::cos(angle)) +
                           tangent_v * (radius * std::sin(angle)) + normal * height);
}

// The whole sphere, for a probe: a point in space has no hemisphere. Uniform, for
// the reason the first-leg sampler above is.
linalg::vec3 uniform_sphere_direction(uint32_t bits)
{
  const float height = 2.f * unit_float_from(bits) - 1.f;
  const float radius = std::sqrt(std::max(0.f, 1.f - height * height));
  const float angle = TWO_PI * unit_float_from(hash_mix(bits, 0x68bc21ebu));

  return linalg::normalize(
      linalg::vec3{radius * std::cos(angle), height, radius * std::sin(angle)});
}

const brush_geometry_t *find_brush(const traced_scene_t &scene, entity_uid_t uid)
{
  const auto at = std::lower_bound(
      scene.brushes.begin(), scene.brushes.end(), uid,
      [](const std::pair<entity_uid_t, const brush_geometry_t *> &entry,
         entity_uid_t key) { return entry.first < key; });

  if (at == scene.brushes.end() || at->first != uid) return nullptr;
  return at->second;
}

// The fallback is the CALLER's, because the two maps that come through here
// mean opposite things when they are unusable: an albedo falls back to the
// untextured grey, an emissive to black. A shared default would light a room
// off a broken texture.
// The direct light at a bounce vertex, shadowed: next-event estimation, and it is
// the same expression the direct solve sums into a texel. One lighting model.
linalg::vec3 direct_irradiance_at(const traced_scene_t &scene,
                                  Span<const baked_light_t> lights,
                                  const linalg::vec3 &position,
                                  const linalg::vec3 &normal,
                                  const indirect_trace_settings_t &settings,
                                  uint32_t bits)
{
  linalg::vec3 irradiance{0.f, 0.f, 0.f};

  for (uint32_t slot = 0; slot < lights.size(); ++slot)
  {
    const scene_light_t &light = lights[slot].light;
    const light_arrival_t arrival =
        arrival_at(light, position, normal, settings.directional_shadow_distance);
    if (!arrival.reaches) continue;

    // ONE ray toward a random point on the emitter, never the texel's spiral:
    // the chain count and every vertex of every chain average it out, and the
    // spiral here cost soft_shadow_samples rays per light per vertex for a
    // penumbra the estimate already converges to. lightmap_gpu_plan.md step 0.
    const float visibility = light_visibility_single_ray(
        *scene.bvh, position, normal, arrival, settings.shadow_ray_bias, hash_mix(bits, slot));
    if (visibility <= 0.f) continue;

    irradiance = irradiance + light.radiance * (arrival.attenuation *
                                                arrival.normal_dot_light * visibility);
  }

  return irradiance;
}

// What the chain collects along `first_leg`, which is PI TIMES the radiance
// arriving from that direction: the estimator for the hemisphere integral at
// every vertex past the first is cosine-sampled, so each level contributes
// albedo * E and the pi of the diffuse BRDF is what is left over. The projection
// below divides it back out.
linalg::vec3 trace_one_chain(const traced_scene_t &scene, Span<const baked_light_t> lights,
                             const linalg::vec3 &position, const linalg::vec3 &normal,
                             const linalg::vec3 &first_leg,
                             const indirect_trace_settings_t &settings, uint32_t bits)
{
  linalg::vec3 collected{0.f, 0.f, 0.f};
  linalg::vec3 throughput{1.f, 1.f, 1.f};
  linalg::vec3 from = position;
  linalg::vec3 from_normal = normal;

  for (int bounce = 0; bounce < std::max(settings.max_bounces, 1); ++bounce)
  {
    bits = hash_mix(bits, (uint32_t)bounce);

    const linalg::vec3 direction =
        bounce == 0 ? first_leg : cosine_hemisphere_direction(from_normal, bits);
    const linalg::vec3 origin = from + from_normal * settings.ray_bias;

    // A ray that leaves the level collects the SKY, and every map is interior --
    // so it collects nothing and the chain ends. lighting_def.md ss13 is where a
    // sky becomes one line here rather than a light kind in the solve.
    ray_hit_result_t hit = {};
    if (!bvh_intersect_ray(*scene.bvh, origin, direction, hit)) break;
    if (!hit.hit || hit.t <= 0.f) break;

    const linalg::vec3 hit_position = origin + direction * hit.t;

    // The entered face's outward normal, which faces the ray by construction --
    // except for a ray that started inside a solid, where it does not and the
    // hemisphere below would fire back into the wall.
    const linalg::vec3 hit_normal =
        linalg::dot(hit.normal, direction) < 0.f ? hit.normal : hit.normal * -1.f;

    const traced_surface_t surface = surface_at(scene, hit, hit_position);

    // Gate 4, and it is the whole of it. A surface does not reflect its own
    // emission, so this lands on the throughput the chain ARRIVED with, before
    // this hit's albedo joins it -- and it carries a PI because what this
    // function returns is PI times the radiance along the first leg, which the
    // projection divides back out. Every other term here is an irradiance and
    // does not.
    collected = collected + multiply_channels(throughput, surface.emission * PI);

    throughput = multiply_channels(throughput, surface.albedo);

    collected = collected +
                multiply_channels(throughput,
                                  direct_irradiance_at(scene, lights, hit_position,
                                                       hit_normal, settings, bits));

    // A chain ends at RANDOM, never at a fixed depth alone: a cap biases every
    // long path dark, and re-weighting the survivor is what keeps the estimator
    // unbiased.
    if (bounce + 1 >= settings.bounces_before_roulette)
    {
      const float survival = std::clamp(luminance_of(throughput), 0.f, 1.f);
      if (unit_float_from(hash_mix(bits, 0x9e3779b9u)) >= survival) break;
      throughput = throughput * (1.f / survival);
    }

    from = hit_position;
    from_normal = hit_normal;
  }

  return collected;
}

} // namespace

float srgb_byte_to_linear(uint8_t encoded)
{
  const float value = (float)encoded * (1.f / 255.f);
  if (value <= 0.04045f) return value * (1.f / 12.92f);
  return std::pow((value + 0.055f) * (1.f / 1.055f), 2.4f);
}

linalg::vec3 sample_texture(const assets::texture_asset_t &texture, const linalg::vec2 &uv,
                            const linalg::vec3 &fallback)
{
  if (texture.width <= 0 || texture.height <= 0 || texture.channels < 3)
    return fallback;

  // Nearest and wrapped. A bounce integrates hundreds of samples over a surface,
  // so the filtering a single fetch would buy is averaged away anyway.
  const auto wrap = [](float coordinate, int size) {
    int texel = (int)std::floor(coordinate * (float)size);
    texel %= size;
    if (texel < 0) texel += size;
    return texel;
  };

  const int x = wrap(uv.x, texture.width);
  const int y = wrap(uv.y, texture.height);
  const size_t at =
      ((size_t)y * (size_t)texture.width + (size_t)x) * (size_t)texture.channels;
  if (at + 2 >= texture.pixels.size()) return fallback;

  return {srgb_byte_to_linear(texture.pixels[at]),
          srgb_byte_to_linear(texture.pixels[at + 1]),
          srgb_byte_to_linear(texture.pixels[at + 2])};
}

traced_scene_t build_traced_scene(const map_t &map, const Bounding_Volume_Hierarchy &bvh)
{
  traced_scene_t scene;
  scene.bvh = &bvh;

  scene.brushes.reserve(map.geometry.size());
  for (const map_geometry_t &entry : map.geometry)
    if (const brush_geometry_t *brush = std::get_if<brush_geometry_t>(&entry.value))
      scene.brushes.push_back({entry.uid, brush});

  std::sort(scene.brushes.begin(), scene.brushes.end(),
            [](const std::pair<entity_uid_t, const brush_geometry_t *> &left,
               const std::pair<entity_uid_t, const brush_geometry_t *> &right) {
              return left.first < right.first;
            });

  // Handles first and pointers after, single-threaded, because a resolve LOADS:
  // the workers must find every texture already in the pool.
  std::vector<assets::material_maps_t> resolved;
  resolved.reserve(map.materials.size());
  for (const std::string &material : map.materials)
    resolved.push_back(resolve_material_maps(material));

  // An invalid handle is a material with no texture, and asking the pool about
  // one is not merely pointless: a headless bake has no asset state to ask.
  const auto pixels_of = [](const assets::asset_handle_t<assets::texture_asset_t> &handle) {
    return handle.valid() ? assets::get(handle) : nullptr;
  };

  scene.materials.reserve(resolved.size());
  for (const assets::material_maps_t &maps : resolved)
    scene.materials.push_back({pixels_of(maps.albedo), pixels_of(maps.emissive)});

  return scene;
}

traced_surface_t surface_at(const traced_scene_t &scene, const ray_hit_result_t &hit,
                            const linalg::vec3 &hit_position)
{
  constexpr linalg::vec3 untextured{UNTEXTURED_BOUNCE_ALBEDO, UNTEXTURED_BOUNCE_ALBEDO,
                                    UNTEXTURED_BOUNCE_ALBEDO};

  const brush_geometry_t *brush = find_brush(scene, hit.id.index);
  if (!brush) return {untextured, {}};

  // A brush whose face_surfaces are empty is every brush authored before faces
  // existed, and its default IS this value -- material 0, the map default.
  const face_surface_t brush_default;
  const face_surface_t *matched =
      find_face_surface(*brush, Plane{hit_position, hit.normal});
  const face_surface_t &face = matched ? *matched : brush_default;

  if (face.material >= scene.materials.size()) return {untextured, {}};
  const traced_scene_t::material_t &material = scene.materials[face.material];

  const linalg::vec2 uv = face_uv_at(face.uv, hit_position, hit.normal);

  const linalg::vec3 albedo =
      material.albedo ? sample_texture(*material.albedo, uv, untextured) : untextured;

  // No emissive.png is no glow, and it is the ONLY thing that says so.
  const linalg::vec3 emission =
      material.emissive ? sample_texture(*material.emissive, uv, {0.f, 0.f, 0.f})
                        : linalg::vec3{0.f, 0.f, 0.f};

  return {albedo, emission};
}

indirect_sh_l1_t trace_indirect_light(const traced_scene_t &scene,
                                      Span<const baked_light_t> lights,
                                      const linalg::vec3 &position,
                                      const linalg::vec3 &normal,
                                      const indirect_trace_settings_t &settings,
                                      uint32_t hash)
{
  indirect_sh_l1_t projected;
  if (settings.rays_per_sample <= 0 || !scene.bvh) return projected;

  // c_i = integral over the hemisphere of L(d) Y_i(d), estimated with a uniform
  // pdf of 1/(2pi). The chain hands back pi * L(d), so the two constants fold to
  // a flat 2 and the whole projection is a multiply and an add.
  //
  // Y is {0.282095, 0.488603 * d.x, 0.488603 * d.y, 0.488603 * d.z} -- four
  // multiplies and no trig, because the first four spherical harmonics are the
  // polynomials {1, x, y, z} restricted to the sphere.
  const float weight = 2.f / (float)settings.rays_per_sample;

  for (int ray = 0; ray < settings.rays_per_sample; ++ray)
  {
    const uint32_t bits = hash_mix(hash, (uint32_t)ray);
    const linalg::vec3 first_leg = uniform_hemisphere_direction(normal, bits);
    const linalg::vec3 collected =
        trace_one_chain(scene, lights, position, normal, first_leg, settings, bits);

    projected.l0 = projected.l0 + collected * (weight * SH_L1_Y0);
    projected.l1[0] = projected.l1[0] + collected * (weight * SH_L1_Y1 * first_leg.x);
    projected.l1[1] = projected.l1[1] + collected * (weight * SH_L1_Y1 * first_leg.y);
    projected.l1[2] = projected.l1[2] + collected * (weight * SH_L1_Y1 * first_leg.z);
  }

  return projected;
}

linalg::vec3 trace_capture_direction(const traced_scene_t &scene,
                                     Span<const baked_light_t> lights,
                                     const linalg::vec3 &position, const linalg::vec3 &direction,
                                     const indirect_trace_settings_t &settings, uint32_t hash)
{
  if (!scene.bvh || settings.rays_per_sample <= 0) return {0.f, 0.f, 0.f};

  linalg::vec3 sum{0.f, 0.f, 0.f};
  for (int ray = 0; ray < settings.rays_per_sample; ++ray)
  {
    const uint32_t bits = hash_mix(hash, (uint32_t)ray);
    sum = sum + trace_one_chain(scene, lights, position, direction, direction, settings, bits);
  }
  return sum * (1.f / (PI * (float)settings.rays_per_sample));
}

probe_trace_t trace_probe_light(const traced_scene_t &scene, Span<const baked_light_t> lights,
                                const probe_visibility_slots_t &visibility_slots,
                                const linalg::vec3 &position,
                                const indirect_trace_settings_t &settings, uint32_t hash)
{
  probe_trace_t traced;
  indirect_sh_l1_t &projected = traced.light;
  if (!scene.bvh) return traced;

  const auto add_from_direction = [&](const linalg::vec3 &irradiance,
                                      const linalg::vec3 &direction) {
    projected.l0 = projected.l0 + irradiance * SH_L1_Y0;
    projected.l1[0] = projected.l1[0] + irradiance * (SH_L1_Y1 * direction.x);
    projected.l1[1] = projected.l1[1] + irradiance * (SH_L1_Y1 * direction.y);
    projected.l1[2] = projected.l1[2] + irradiance * (SH_L1_Y1 * direction.z);
  };

  // Direct for a Baked light, the visibility alone for a Mixed one. There is no
  // surface here, so the "normal" handed to the arrival and the shadow ray is
  // the direction to the light itself: N.L is then 1, `reaches` collapses to
  // `arrives`, and the bias steps toward the light rather than off a face that
  // does not exist.
  for (uint32_t slot = 0; slot < lights.size(); ++slot)
  {
    const scene_light_t &light = lights[slot].light;

    int channel = -1;
    if (light_is_analytic(light.mode))
    {
      for (uint32_t at = 0; at < PROBE_VISIBILITY_CHANNELS; ++at)
        if (visibility_slots[at] == (int16_t)slot) channel = (int)at;
      if (channel < 0) continue;
    }

    const light_arrival_t probe =
        arrival_at(light, position, {0.f, 1.f, 0.f}, settings.directional_shadow_distance);
    if (!probe.arrives) continue;

    const light_arrival_t arrival =
        arrival_at(light, position, probe.direction, settings.directional_shadow_distance);
    const float visibility =
        light_visibility(*scene.bvh, position, arrival.direction, arrival,
                         settings.shadow_ray_bias, settings.soft_shadow_samples,
                         hash_mix(hash, 0x7f4a7c15u + slot));

    if (channel >= 0)
    {
      traced.visibility[(uint32_t)channel] = visibility;
      continue;
    }
    if (visibility <= 0.f) continue;

    add_from_direction(light.radiance * (arrival.attenuation * visibility), arrival.direction);
  }

  if (settings.rays_per_sample <= 0) return traced;

  // The sphere's uniform pdf is 1/(4pi) where the hemisphere's was 1/(2pi), and
  // the chain still hands back pi * L(d): the flat 2 becomes a flat 4. The chain
  // is given the first leg as its "normal" so its origin bias steps along the
  // ray; nothing else in it reads the normal on the first bounce.
  const float weight = 4.f / (float)settings.rays_per_sample;

  for (int ray = 0; ray < settings.rays_per_sample; ++ray)
  {
    const uint32_t bits = hash_mix(hash, (uint32_t)ray);
    const linalg::vec3 first_leg = uniform_sphere_direction(bits);
    const linalg::vec3 collected =
        trace_one_chain(scene, lights, position, first_leg, first_leg, settings, bits);

    add_from_direction(collected * weight, first_leg);
  }

  return traced;
}

} // namespace shared
