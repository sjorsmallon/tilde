// The bake's SHADOW RAYS, in GLSL: lightmap_lights.cpp's shadow_ray_transmittance
// and light_visibility, hash for hash, so a kernel casts the same ray the CPU
// casts and reads the same texel of the same glass.
//
// TWO traversals in a fixed order, which is the whole of transparency_plan.md
// step 7. The opaque set decides whether the ray gets through at all; only a ray
// that got through looks at the transmissive set, because a blocked ray delivers
// nothing and what stands in front of the wall it hit cannot matter. That order
// is also what makes the second traversal safe to run with no early out: there
// is no committed hit for a candidate to be hiding behind, so the arbitrary
// order rayQueryProceedEXT reports candidates in does not matter -- a product
// commutes.
//
// Expects, declared BEFORE this include: lightmap_bake.glsl (gpu_sample_t, the
// hash, face_tangents, bake_arrival_t), lightmap_scene.glsl (the two structs and
// the masks), and the kernel's own bindings -- `scene`, `push` carrying
// shadow_ray_bias / soft_shadow_samples / directional_shadow_distance and
// transmissive_triangle_count, plus `triangles`, `materials` and `textures`.

// --- Reading a hit ---------------------------------------------------------------

int wrap_texel(float coordinate, int size)
{
  int texel = int(floor(coordinate * float(size)));
  texel %= size;
  if (texel < 0) texel += size;
  return texel;
}

// lightmap_trace.cpp's srgb_byte_to_linear, on a RAW byte.
float srgb_byte_to_linear(uint encoded)
{
  const float value = float(encoded) * (1.0 / 255.0);
  return value <= 0.04045 ? value * (1.0 / 12.92) : pow((value + 0.055) / 1.055, 2.4);
}

// sample_texture's wrap and sRGB decode verbatim, so a hit reads the texel the
// CPU reads and decodes it with the same arithmetic.
vec3 fetch_texture(uint index, vec2 uv, vec3 fallback)
{
  if (index == NO_TEXTURE) return fallback;
  const ivec2 size = textureSize(textures[nonuniformEXT(index)], 0);
  const ivec2 texel = ivec2(wrap_texel(uv.x, size.x), wrap_texel(uv.y, size.y));
  const uvec4 bytes = texelFetch(textures[nonuniformEXT(index)], texel, 0);
  return vec3(srgb_byte_to_linear(bytes.r), srgb_byte_to_linear(bytes.g),
              srgb_byte_to_linear(bytes.b));
}

// sample_texture_alpha's: the fourth channel RAW, never sRGB-decoded, because
// alpha is a coverage and not a colour.
float fetch_texture_alpha(uint index, vec2 uv)
{
  if (index == NO_TEXTURE) return 1.0;
  const ivec2 size = textureSize(textures[nonuniformEXT(index)], 0);
  const ivec2 texel = ivec2(wrap_texel(uv.x, size.x), wrap_texel(uv.y, size.y));
  return float(texelFetch(textures[nonuniformEXT(index)], texel, 0).a) * (1.0 / 255.0);
}

vec2 triangle_uv_at(uint triangle_index, vec2 barycentrics)
{
  const gpu_triangle_t triangle = triangles[triangle_index];
  const float weight_0 = 1.0 - barycentrics.x - barycentrics.y;
  return triangle.uv0 * weight_0 + triangle.uv1 * barycentrics.x +
         triangle.uv2 * barycentrics.y;
}

// lightmap_trace.cpp's transmittance_at: what survives ONE crossing of a
// transmissive surface, `albedo * (1 - alpha)`. A face that is not `blend`
// transmits nothing -- the transmissive set holds whole brushes, so a brush that
// got in there for one glass face still has its opaque faces in it.
vec3 transmittance_at(uint triangle_index, vec2 barycentrics)
{
  const gpu_material_t material = materials[triangles[triangle_index].material];
  if (material.alpha_mode != ALPHA_MODE_BLEND) return vec3(0.0);

  const vec2 uv = triangle_uv_at(triangle_index, barycentrics);
  const float alpha = fetch_texture_alpha(material.albedo_texture, uv);
  return fetch_texture(material.albedo_texture, uv, vec3(UNTEXTURED_BOUNCE_ALBEDO)) *
         (1.0 - alpha);
}

// lightmap_trace.cpp's alpha_test_is_solid_at: is this hit on the SOLID part of
// an alpha-tested surface -- a bar rather than the gap beside it? TRUE stops the
// ray, which is why it is what a candidate is CONFIRMED on. A face that is not `cutout` is solid, so a brush that got
// into the tested set for its other faces still stops a ray through this one.
bool alpha_test_is_solid_at(uint triangle_index, vec2 barycentrics)
{
  const gpu_material_t material = materials[triangles[triangle_index].material];
  if (material.alpha_mode != ALPHA_MODE_CUTOUT) return true;

  return fetch_texture_alpha(material.albedo_texture,
                             triangle_uv_at(triangle_index, barycentrics)) >=
         material.alpha_cutoff;
}

// --- Rays --------------------------------------------------------------------

// Did anything STOP this ray? The opaque set and the fences together, because
// they answer the same question. The opaque geometry keeps its OPAQUE flag, so
// the driver commits it without ever entering the loop below -- which is what
// makes a map with no fence in it the plain traversal this has always been, and
// the loop body unreachable.
bool ray_is_clear(vec3 origin, vec3 direction, float max_distance)
{
  // A ray asked to travel nowhere hits nothing: the CPU's `t < distance - bias`
  // admits no hit there, and a query with tMax below tMin is undefined.
  if (max_distance <= 0.0) return true;

  rayQueryEXT query;
  rayQueryInitializeEXT(query, scene, gl_RayFlagsTerminateOnFirstHitEXT,
                        OPAQUE_INSTANCE_MASK | ALPHA_TESTED_INSTANCE_MASK, origin, 0.0,
                        direction, max_distance);

  while (rayQueryProceedEXT(query))
  {
    if (rayQueryGetIntersectionTypeEXT(query, false) !=
        gl_RayQueryCandidateIntersectionTriangleEXT)
      continue;

    const uint triangle_index = uint(rayQueryGetIntersectionInstanceCustomIndexEXT(query, false)) +
                                rayQueryGetIntersectionPrimitiveIndexEXT(query, false);
    if (alpha_test_is_solid_at(triangle_index,
                             rayQueryGetIntersectionBarycentricsEXT(query, false)))
      rayQueryConfirmIntersectionEXT(query);
  }

  return rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT;
}

// The transmissive half: the product of what the ray crossed on its way to the
// light. ONE traversal with nothing committed -- every candidate is a crossing --
// where the CPU marches entry to entry through its own BVH, because the two
// structures answer the same question in the shapes they have. A brush is a
// closed solid, so the driver reports its ENTRY and EXIT triangles both; the
// exit one faces away from the ray and is what the `facing` test drops, which is
// the GPU's spelling of the CPU's step past `t_exit`.
vec3 transmittance_along(vec3 origin, vec3 direction, float max_distance)
{
  vec3 transmittance = vec3(1.0);
  // No glass in the map is no second instance and no second ray, which is what
  // makes every bake that has none cost exactly what it always did.
  if (max_distance <= 0.0 || push.transmissive_triangle_count == 0u) return transmittance;

  rayQueryEXT query;
  rayQueryInitializeEXT(query, scene, gl_RayFlagsNoOpaqueEXT, TRANSMISSIVE_INSTANCE_MASK,
                        origin, 0.0, direction, max_distance);

  while (rayQueryProceedEXT(query))
  {
    if (rayQueryGetIntersectionTypeEXT(query, false) !=
        gl_RayQueryCandidateIntersectionTriangleEXT)
      continue;

    // The instance's custom index is where its range of the shared triangle
    // buffer begins, so a per-BLAS primitive index becomes a scene one.
    const uint triangle_index = uint(rayQueryGetIntersectionInstanceCustomIndexEXT(query, false)) +
                                rayQueryGetIntersectionPrimitiveIndexEXT(query, false);

    // ONE tint per PIECE crossed, taken from the face the ray ENTERED through.
    // Without this a pane is filtered twice -- once going in and once coming out
    // -- which is the same double count the CPU's step past t_exit avoids.
    if (!rayQueryGetIntersectionFrontFaceEXT(query, false)) continue;

    transmittance *=
        transmittance_at(triangle_index, rayQueryGetIntersectionBarycentricsEXT(query, false));
    if (transmittance == vec3(0.0)) break;
  }

  return transmittance;
}

// The nearest SURFACE along a ray: the opaque set and the fences together, a
// fence counting only where its texel is solid. What a BOUNCE lands on, and one
// query rather than two -- the opaque geometry keeps its OPAQUE flag, so the
// driver commits it without entering the loop and only a fence candidate is ever
// asked about.
//
// Glass is deliberately NOT in the mask: a chain passes through a window and is
// TINTED by it (transmittance_along), rather than landing on it.
bool trace_nearest_surface(vec3 origin, vec3 direction, float max_distance, out float out_t,
                           out uint out_triangle, out vec2 out_barycentrics)
{
  out_t = 0.0;
  out_triangle = 0u;
  out_barycentrics = vec2(0.0);

  rayQueryEXT query;
  rayQueryInitializeEXT(query, scene, gl_RayFlagsNoneEXT,
                        OPAQUE_INSTANCE_MASK | ALPHA_TESTED_INSTANCE_MASK, origin, 0.0,
                        direction, max_distance);

  while (rayQueryProceedEXT(query))
  {
    if (rayQueryGetIntersectionTypeEXT(query, false) !=
        gl_RayQueryCandidateIntersectionTriangleEXT)
      continue;

    const uint triangle_index = uint(rayQueryGetIntersectionInstanceCustomIndexEXT(query, false)) +
                                rayQueryGetIntersectionPrimitiveIndexEXT(query, false);
    if (alpha_test_is_solid_at(triangle_index,
                             rayQueryGetIntersectionBarycentricsEXT(query, false)))
      rayQueryConfirmIntersectionEXT(query);
  }

  if (rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionTriangleEXT)
    return false;

  out_t = rayQueryGetIntersectionTEXT(query, true);
  out_triangle = uint(rayQueryGetIntersectionInstanceCustomIndexEXT(query, true)) +
                 rayQueryGetIntersectionPrimitiveIndexEXT(query, true);
  out_barycentrics = rayQueryGetIntersectionBarycentricsEXT(query, true);
  return true;
}

// ONE ray, and what it DELIVERS: black where something opaque stopped it, white
// where it arrived through nothing, the product of what it crossed otherwise.
vec3 shadow_ray_transmittance(vec3 surface_position, vec3 surface_normal, vec3 direction,
                              float distance)
{
  const vec3 origin = surface_position + surface_normal * push.shadow_ray_bias;
  const float travel = distance - push.shadow_ray_bias;

  if (!ray_is_clear(origin, direction, travel)) return vec3(0.0);
  return transmittance_along(origin, direction, travel);
}

vec3 shadow_ray_transmittance_to_disc_point(vec3 surface_position, vec3 surface_normal,
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
  if (distance < 1e-4) return vec3(0.0);

  return shadow_ray_transmittance(surface_position, surface_normal,
                                  to_target * (1.0 / distance), distance);
}

// lightmap_lights.cpp's shadow_ray_count: one ray for a punctual light,
// soft_shadow_samples for one with a disc.
int shadow_ray_count(bake_arrival_t arrival)
{
  return arrival.shadow_disc_radius > 0.0 ? max(push.soft_shadow_samples, 1) : 1;
}

// light_visibility: the fraction of the emitter this point sees, over the
// golden-angle spiral with the CPU's 16-bit jitters cut from the same hash. Per
// COLOUR CHANNEL, since a ray through stained glass arrives coloured.
vec3 light_visibility(vec3 surface_position, vec3 surface_normal, bake_arrival_t arrival,
                      uint hash)
{
  const int sample_count = shadow_ray_count(arrival);

  if (sample_count == 1)
    return shadow_ray_transmittance(surface_position, surface_normal, arrival.direction,
                                    arrival.distance);

  vec3 reached = vec3(0.0);
  for (int sample_index = 0; sample_index < sample_count; ++sample_index)
  {
    const uint sample_bits = hash_mix(hash, uint(sample_index));

    const float radius_jitter = float(sample_bits & 0xffffu) * (1.0 / 65536.0);
    const float angle_jitter = float((sample_bits >> 16) & 0xffffu) * (1.0 / 65536.0);

    const float radius = arrival.shadow_disc_radius *
                         sqrt((float(sample_index) + radius_jitter) / float(sample_count));
    const float angle = float(sample_index) * GOLDEN_ANGLE + angle_jitter * TWO_PI;

    reached += shadow_ray_transmittance_to_disc_point(surface_position, surface_normal, arrival,
                                                      radius, angle);
  }

  return reached / float(sample_count);
}

// light_visibility_single_ray: the chain's next-event estimation spends ONE ray
// per light per vertex, toward a random point of the disc.
vec3 light_visibility_single_ray(vec3 surface_position, vec3 surface_normal,
                                 bake_arrival_t arrival, uint hash)
{
  if (arrival.shadow_disc_radius <= 0.0)
    return shadow_ray_transmittance(surface_position, surface_normal, arrival.direction,
                                    arrival.distance);

  const float radius = arrival.shadow_disc_radius * sqrt(unit_float_from(hash));
  const float angle = TWO_PI * unit_float_from(hash_mix(hash, 0x68bc21ebu));

  return shadow_ray_transmittance_to_disc_point(surface_position, surface_normal, arrival,
                                                radius, angle);
}
