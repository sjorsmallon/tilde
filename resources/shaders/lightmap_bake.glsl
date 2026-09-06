// The text the bake's kernels SHARE: the sample record, the hash the CPU
// derives every random number from, the tangent basis every sampler builds in,
// the shadow rays and light_visibility's golden-angle disc -- lightmap_lights.cpp
// in GLSL, hash for hash, so a kernel casts the SAME ray the CPU casts. One
// text, because lightmap_direct.comp and lightmap_indirect.comp each carried a
// copy and the probe half (lightmap_gpu_plan.md step 7) wanted a third.
//
// Expects, declared BEFORE this include: `scene`, the acceleration structure,
// and `push`, carrying shadow_ray_bias, soft_shadow_samples and
// directional_shadow_distance.

#include "light_arrival.glsl"

// gpu_sample_t, lightmap_gpu.hpp, std430.
struct gpu_sample_t
{
  vec3 position;
  uint chart_index;
  vec3 normal;
  uint seed;
};

const float PI = 3.14159265;
const float TWO_PI = 6.28318531;
const float GOLDEN_ANGLE = 2.39996323;

// lightmap_lights.cpp's hash_mix / unit_float_from / luminance_of, verbatim.
uint hash_mix(uint hash, uint value)
{
  for (int byte = 0; byte < 4; ++byte)
  {
    hash ^= (value >> (byte * 8)) & 0xffu;
    hash *= 16777619u;
  }
  return hash;
}

float unit_float_from(uint bits)
{
  return float(bits >> 8) * (1.0 / 16777216.0);
}

float luminance_of(vec3 linear_rgb)
{
  return 0.2126 * linear_rgb.x + 0.7152 * linear_rgb.y + 0.0722 * linear_rgb.z;
}

// brush.cpp's brush_face_grid_tangents, which every sampler builds its basis
// from -- the same basis as the CPU, or the same random numbers would name
// different directions.
void face_tangents(vec3 normal, out vec3 tangent_u, out vec3 tangent_v)
{
  const float absolute_x = abs(normal.x);
  const float absolute_y = abs(normal.y);
  const float absolute_z = abs(normal.z);

  if (absolute_x > 0.999)
  {
    tangent_u = vec3(0, 0, 1);
    tangent_v = vec3(0, 1, 0);
    return;
  }
  if (absolute_y > 0.999)
  {
    tangent_u = vec3(1, 0, 0);
    tangent_v = vec3(0, 0, 1);
    return;
  }
  if (absolute_z > 0.999)
  {
    tangent_u = vec3(1, 0, 0);
    tangent_v = vec3(0, 1, 0);
    return;
  }

  const vec3 reference = absolute_x < 0.9 ? vec3(1, 0, 0) : vec3(0, 1, 0);
  tangent_u = normalize(cross(normal, reference));
  tangent_v = cross(normal, tangent_u);
}

// --- Rays --------------------------------------------------------------------

bool ray_is_clear(vec3 origin, vec3 direction, float max_distance)
{
  // A ray asked to travel nowhere hits nothing: the CPU's `t < distance - bias`
  // admits no hit there, and a query with tMax below tMin is undefined.
  if (max_distance <= 0.0) return true;

  rayQueryEXT query;
  rayQueryInitializeEXT(query, scene, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                        0xff, origin, 0.0, direction, max_distance);
  while (rayQueryProceedEXT(query)) {}
  return rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT;
}

bool shadow_ray_reaches(vec3 surface_position, vec3 surface_normal, vec3 direction,
                        float distance)
{
  return ray_is_clear(surface_position + surface_normal * push.shadow_ray_bias, direction,
                      distance - push.shadow_ray_bias);
}

// --- Arrival, on top of the shared text ----------------------------------------

// lightmap_lights.cpp's light_arrival_t: light_arrival() from light_arrival.glsl
// for the direction and the attenuation, plus the two gates and the shadow disc
// the CPU adds around it. A directional light's distance is the settings' shadow
// distance here, not the unit distance the runtime measures its radius at.
struct bake_arrival_t
{
  bool arrives;
  bool reaches;
  vec3 direction;
  float distance;
  float attenuation;
  float normal_dot_light;
  float shadow_disc_radius;
};

bake_arrival_t arrival_at(Light light, vec3 surface_position, vec3 surface_normal)
{
  bake_arrival_t arrival;
  arrival.arrives = false;
  arrival.reaches = false;
  arrival.direction = vec3(0.0);
  arrival.distance = 0.0;
  arrival.attenuation = 0.0;
  arrival.normal_dot_light = 0.0;
  arrival.shadow_disc_radius = 0.0;

  const int light_type = int(light.spot_params.w);
  if (light_type != LIGHT_TYPE_DIRECTIONAL)
  {
    const vec3 to_light = light.position.xyz - surface_position;
    const float distance = sqrt(dot(to_light, to_light));
    if (distance > light.spot_params.z || distance < 1e-4) return arrival;
  }

  const Light_Arrival shared_arrival = light_arrival(light, surface_position);
  arrival.direction = shared_arrival.direction;
  arrival.attenuation = shared_arrival.attenuation;
  if (light_type == LIGHT_TYPE_DIRECTIONAL)
  {
    arrival.distance = push.directional_shadow_distance;
    arrival.shadow_disc_radius = light.direction.w * arrival.distance;
  }
  else
  {
    arrival.distance = shared_arrival.distance;
    arrival.shadow_disc_radius = light.direction.w;
  }

  if (arrival.attenuation <= 0.0) return arrival;
  arrival.arrives = true;

  arrival.normal_dot_light = dot(surface_normal, arrival.direction);
  if (arrival.normal_dot_light <= 0.0) return arrival;

  arrival.reaches = true;
  return arrival;
}

bool shadow_ray_reaches_disc_point(vec3 surface_position, vec3 surface_normal,
                                   bake_arrival_t arrival, float radius, float angle)
{
  vec3 tangent_u;
  vec3 tangent_v;
  face_tangents(arrival.direction, tangent_u, tangent_v);

  const vec3 centre = surface_position + arrival.direction * arrival.distance;
  const vec3 target =
      centre + tangent_u * (cos(angle) * radius) + tangent_v * (sin(angle) * radius);

  const vec3 to_target = target - surface_position;
  const float distance = sqrt(dot(to_target, to_target));
  if (distance < 1e-4) return false;

  return shadow_ray_reaches(surface_position, surface_normal, to_target * (1.0 / distance),
                            distance);
}

// lightmap_lights.cpp's shadow_ray_count: one ray for a punctual light,
// soft_shadow_samples for one with a disc.
int shadow_ray_count(bake_arrival_t arrival)
{
  return arrival.shadow_disc_radius > 0.0 ? max(push.soft_shadow_samples, 1) : 1;
}

// light_visibility: the fraction of the emitter this point sees, over the
// golden-angle spiral with the CPU's 16-bit jitters cut from the same hash.
float light_visibility(vec3 surface_position, vec3 surface_normal, bake_arrival_t arrival,
                       uint hash)
{
  const int sample_count = shadow_ray_count(arrival);

  if (sample_count == 1)
    return shadow_ray_reaches(surface_position, surface_normal, arrival.direction,
                              arrival.distance)
               ? 1.0
               : 0.0;

  int reached = 0;
  for (int sample_index = 0; sample_index < sample_count; ++sample_index)
  {
    const uint sample_bits = hash_mix(hash, uint(sample_index));

    const float radius_jitter = float(sample_bits & 0xffffu) * (1.0 / 65536.0);
    const float angle_jitter = float((sample_bits >> 16) & 0xffffu) * (1.0 / 65536.0);

    const float radius = arrival.shadow_disc_radius *
                         sqrt((float(sample_index) + radius_jitter) / float(sample_count));
    const float angle = float(sample_index) * GOLDEN_ANGLE + angle_jitter * TWO_PI;

    if (shadow_ray_reaches_disc_point(surface_position, surface_normal, arrival, radius, angle))
      ++reached;
  }

  return float(reached) / float(sample_count);
}

// light_visibility_single_ray: the chain's next-event estimation spends ONE ray
// per light per vertex, toward a random point of the disc.
float light_visibility_single_ray(vec3 surface_position, vec3 surface_normal,
                                  bake_arrival_t arrival, uint hash)
{
  if (arrival.shadow_disc_radius <= 0.0)
    return shadow_ray_reaches(surface_position, surface_normal, arrival.direction,
                              arrival.distance)
               ? 1.0
               : 0.0;

  const float radius = arrival.shadow_disc_radius * sqrt(unit_float_from(hash));
  const float angle = TWO_PI * unit_float_from(hash_mix(hash, 0x68bc21ebu));

  return shadow_ray_reaches_disc_point(surface_position, surface_normal, arrival, radius, angle)
             ? 1.0
             : 0.0;
}

// A uint64_t on the CPU IS the uvec2 here, low word first: one bit per light
// slot of the resolve table.
bool mask_admits(uvec2 mask, uint slot)
{
  if (slot >= 64u) return false;
  const uint word = slot < 32u ? mask.x : mask.y;
  return ((word >> (slot & 31u)) & 1u) != 0u;
}
