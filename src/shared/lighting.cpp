#define ENTITIES_WANT_INCLUDES
#include "lighting.hpp"

#include "entities/entity_reflection.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

std::optional<scene_light_t> try_light_of(const entities::Entity &entity)
{
  if (const entities::Point_Light_Entity* point =
          entities::entity_as<entities::Point_Light_Entity>(&entity))
  {
    scene_light_t light;
    light.kind = light_kind_t::Point;
    light.mode = point->light.mode;
    light.position = point->position;
    light.radiance = radiance_of(point->light, light_kind_t::Point);
    light.range = point->range;
    light.source_radius = std::max(point->light.source_radius, 0.f);
    light.casts_shadows = point->light.casts_shadows;
    return light;
  }

  if (const entities::Spot_Light_Entity* spot =
          entities::entity_as<entities::Spot_Light_Entity>(&entity))
  {
    scene_light_t light;
    light.kind = light_kind_t::Spot;
    light.mode = spot->light.mode;
    light.position = spot->position;
    light.forward = linalg::basis_from(spot->orientation).forward;
    light.radiance = radiance_of(spot->light, light_kind_t::Spot);
    light.range = spot->range;
    light.cos_inner = std::cos(linalg::to_radians(spot->inner_degrees));
    light.cos_outer = std::cos(linalg::to_radians(spot->outer_degrees));
    light.source_radius = std::max(spot->light.source_radius, 0.f);
    light.casts_shadows = spot->light.casts_shadows;
    return light;
  }

  if (const entities::Directional_Light_Entity* directional =
          entities::entity_as<entities::Directional_Light_Entity>(&entity))
  {
    scene_light_t light;
    light.kind = light_kind_t::Directional;
    light.mode = directional->light.mode;
    light.forward  = linalg::basis_from(directional->orientation).forward;
    light.radiance = radiance_of(directional->light, light_kind_t::Directional);

    // The sun's softness is an ANGLE, so it converts to the one radius everything
    // downstream takes by being measured at unit distance -- which is exactly the
    // distance a directional arrival reports. Clamped below a right angle because
    // tan runs away there, and a half-angle at 90 degrees is not a sun, it is an
    // ambient term with a direction attached.
    const float angular_radius_degrees =
        std::clamp(directional->angular_diameter_degrees * 0.5f, 0.f, 89.f);
    light.source_radius = std::tan(linalg::to_radians(angular_radius_degrees));
    light.casts_shadows = directional->light.casts_shadows;
    return light;
  }

  return {};
}

shadow_projection_t spot_shadow_projection(const scene_light_t &light, uint32_t resolution)
{
  const float cos_outer  = std::clamp(light.cos_outer, std::cos(linalg::to_radians(85.f)),
                                      std::cos(linalg::to_radians(1.f)));
  const float half_angle = std::acos(cos_outer);

  const linalg::vec3 forward = linalg::normalize(light.forward);
  const linalg::vec3 up      = std::abs(forward.y) < 0.99f ? linalg::vec3{0.f, 1.f, 0.f}
                                                           : linalg::vec3{1.f, 0.f, 0.f};
  const float far_plane = std::max(light.range, SHADOW_NEAR_PLANE * 2.f);

  shadow_projection_t projection;
  projection.view_projection =
      linalg::perspective(half_angle * 2.f, 1.f, SHADOW_NEAR_PLANE, far_plane) *
      linalg::look_at(light.position, light.position + forward, up);
  projection.texel_size_at_unit_distance =
      2.f * std::tan(half_angle) / (float)std::max<uint32_t>(resolution, 1u);
  projection.near_plane = SHADOW_NEAR_PLANE;
  projection.far_plane  = far_plane;
  return projection;
}

float cascade_split_depth(const shadow_view_t &view, const cascade_settings_t &settings,
                          uint32_t index)
{
  const uint32_t count = std::clamp<uint32_t>(settings.count, 1u, MAX_SHADOW_CASCADES);
  const float    near  = std::max(view.near_plane, 0.01f);
  const float    far   = std::max(settings.far_distance, near * 2.f);
  if (index == 0) return near;
  if (index >= count) return far;

  const float fraction    = (float)index / (float)count;
  const float logarithmic = near * std::pow(far / near, fraction);
  const float uniform     = near + (far - near) * fraction;
  const float lambda      = std::clamp(settings.lambda, 0.f, 1.f);
  return lambda * logarithmic + (1.f - lambda) * uniform;
}

// The four corners of the view frustum's cross-section at one depth.
static Array<linalg::vec3, 4> frustum_quad_at_depth(const shadow_view_t &view, float depth)
{
  const float half_height =
      view.tan_half_fov_y > 0.f ? view.tan_half_fov_y * depth : view.ortho_half_height;
  const float        half_width = half_height * view.aspect;
  const linalg::vec3 center     = view.position + view.forward * depth;
  const linalg::vec3 right      = view.right * half_width;
  const linalg::vec3 up         = view.up * half_height;
  return {{center - right - up, center + right - up, center + right + up, center - right + up}};
}

shadow_cascades_t directional_shadow_cascades(const scene_light_t &light, const shadow_view_t &view,
                                              const cascade_settings_t &settings,
                                              uint32_t                  resolution)
{
  shadow_cascades_t result;
  result.count           = std::clamp<uint32_t>(settings.count, 1u, MAX_SHADOW_CASCADES);
  result.camera_position = view.position;

  const linalg::vec3 light_forward = linalg::normalize(light.forward);
  const linalg::vec3 up_hint       = std::abs(light_forward.y) < 0.99f ? linalg::vec3{0.f, 1.f, 0.f}
                                                                       : linalg::vec3{1.f, 0.f, 0.f};
  // The same frame look_at derives below, so a snap measured here is a snap
  // along the map's own axes.
  const linalg::vec3 light_right = linalg::normalize(linalg::cross(light_forward, up_hint));
  const linalg::vec3 light_up    = linalg::cross(light_right, light_forward);
  result.light_direction         = light_forward;

  const float texel_count   = (float)std::max<uint32_t>(resolution, 1u);
  const float caster_extent = std::max(settings.caster_extent, 0.f);

  for (uint32_t index = 0; index < result.count; ++index)
  {
    shadow_cascade_t &cascade = result.cascades[index];
    cascade.near_depth        = cascade_split_depth(view, settings, index);
    cascade.far_depth         = cascade_split_depth(view, settings, index + 1);

    const Array<linalg::vec3, 4> near_quad = frustum_quad_at_depth(view, cascade.near_depth);
    const Array<linalg::vec3, 4> far_quad  = frustum_quad_at_depth(view, cascade.far_depth);
    for (uint32_t corner = 0; corner < 4; ++corner)
    {
      cascade.slice_corners[corner]     = near_quad[corner];
      cascade.slice_corners[corner + 4] = far_quad[corner];
    }

    // The slice's bounding sphere sits on the view axis where the near corners
    // and the far corners are equidistant: |c-n|^2 + rn^2 = |f-c|^2 + rf^2.
    const float        n               = cascade.near_depth;
    const float        f               = cascade.far_depth;
    const linalg::vec3 near_axis       = view.position + view.forward * n;
    const linalg::vec3 far_axis        = view.position + view.forward * f;
    const float        near_radius_sq  = linalg::dot(near_quad[0] - near_axis, near_quad[0] - near_axis);
    const float        far_radius_sq   = linalg::dot(far_quad[0] - far_axis, far_quad[0] - far_axis);
    float center_depth = (f * f - n * n + far_radius_sq - near_radius_sq) / (2.f * (f - n));
    center_depth       = std::clamp(center_depth, n, f);
    const float radius = std::max(std::sqrt((f - center_depth) * (f - center_depth) + far_radius_sq),
                                  std::sqrt((center_depth - n) * (center_depth - n) + near_radius_sq));
    cascade.sphere_center = view.position + view.forward * center_depth;
    cascade.sphere_radius = radius;

    // Snap the box's origin to a whole texel in the light's frame: the sphere
    // keeps the box's SIZE constant across a camera turn, this keeps its texel
    // GRID still across a camera move, and the two together are what stops a
    // shadow edge crawling as you walk.
    const float texel = 2.f * radius / texel_count;
    const float x     = std::round(linalg::dot(cascade.sphere_center, light_right) / texel) * texel;
    const float y     = std::round(linalg::dot(cascade.sphere_center, light_up) / texel) * texel;
    const float z     = linalg::dot(cascade.sphere_center, light_forward);
    const linalg::vec3 snapped_center = light_right * x + light_up * y + light_forward * z;

    const float        depth_range = 2.f * radius + caster_extent;
    const linalg::vec3 eye         = snapped_center - light_forward * (radius + caster_extent);
    cascade.projection.view_projection =
        linalg::orthographic(-radius, radius, -radius, radius, 0.f, depth_range) *
        linalg::look_at(eye, snapped_center, up_hint);
    cascade.projection.texel_size_at_unit_distance = texel;
    cascade.projection.near_plane                  = 0.f;
    cascade.projection.far_plane                   = depth_range;

    const linalg::vec3 box_right = light_right * radius;
    const linalg::vec3 box_up    = light_up * radius;
    const linalg::vec3 box_far   = eye + light_forward * depth_range;
    cascade.box_corners[0]       = eye - box_right - box_up;
    cascade.box_corners[1]       = eye + box_right - box_up;
    cascade.box_corners[2]       = eye + box_right + box_up;
    cascade.box_corners[3]       = eye - box_right + box_up;
    cascade.box_corners[4]       = box_far - box_right - box_up;
    cascade.box_corners[5]       = box_far + box_right - box_up;
    cascade.box_corners[6]       = box_far + box_right + box_up;
    cascade.box_corners[7]       = box_far - box_right + box_up;
  }

  return result;
}

uint32_t point_shadow_face_of(const linalg::vec3 &light_to_point)
{
  const linalg::vec3 magnitude = {std::abs(light_to_point.x), std::abs(light_to_point.y),
                                  std::abs(light_to_point.z)};
  if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
    return light_to_point.x >= 0.f ? 0u : 1u;
  if (magnitude.y >= magnitude.z)
    return light_to_point.y >= 0.f ? 2u : 3u;
  return light_to_point.z >= 0.f ? 4u : 5u;
}

// A plane through three points, oriented so `inside` is on its negative side.
struct oriented_plane_t
{
  linalg::vec3 normal;
  linalg::vec3 point;
};

static oriented_plane_t plane_through(const linalg::vec3 &a, const linalg::vec3 &b,
                                      const linalg::vec3 &c, const linalg::vec3 &inside)
{
  linalg::vec3 normal = linalg::cross(b - a, c - a);
  const float  length = linalg::length(normal);
  normal = length > 1e-12f ? normal * (1.f / length) : linalg::vec3{0.f, 0.f, 0.f};
  if (linalg::dot(inside - a, normal) > 0.f)
    normal = normal * -1.f;
  return {normal, a};
}

static bool every_point_outside(const oriented_plane_t &plane, Span<const linalg::vec3> points)
{
  for (const linalg::vec3 &point : points)
    if (linalg::dot(point - plane.point, plane.normal) <= 0.f)
      return false;
  return true;
}

static linalg::vec3 centroid_of(Span<const linalg::vec3> points)
{
  linalg::vec3 sum{0.f, 0.f, 0.f};
  for (const linalg::vec3 &point : points)
    sum = sum + point;
  return sum * (1.f / (float)points.size());
}

point_shadow_faces_t point_shadow_faces(const scene_light_t &light, const shadow_view_t &view,
                                        uint32_t resolution)
{
  point_shadow_faces_t result;
  result.position = light.position;
  result.range    = std::max(light.range, SHADOW_NEAR_PLANE * 2.f);

  // The camera frustum as eight corners and six planes, once for all faces.
  const Array<linalg::vec3, 4> near_quad = frustum_quad_at_depth(view, view.near_plane);
  const Array<linalg::vec3, 4> far_quad =
      frustum_quad_at_depth(view, std::max(view.far_plane, view.near_plane * 2.f));
  Array<linalg::vec3, 8> camera_corners;
  for (uint32_t corner = 0; corner < 4; ++corner)
  {
    camera_corners[corner]     = near_quad[corner];
    camera_corners[corner + 4] = far_quad[corner];
  }
  const linalg::vec3              camera_inside = centroid_of(camera_corners);
  const Array<oriented_plane_t, 6> camera_planes = {{
      plane_through(near_quad[0], near_quad[1], near_quad[2], camera_inside),
      plane_through(far_quad[0], far_quad[1], far_quad[2], camera_inside),
      plane_through(near_quad[0], near_quad[1], far_quad[1], camera_inside), // bottom
      plane_through(near_quad[1], near_quad[2], far_quad[2], camera_inside), // right
      plane_through(near_quad[2], near_quad[3], far_quad[3], camera_inside), // top
      plane_through(near_quad[3], near_quad[0], far_quad[0], camera_inside), // left
  }};

  // Each face's map reaches past the 45-degree edge by the guard, so the
  // half-extent one unit out is 1 plus the guard's share of the map.
  const float texel_count = (float)std::max<uint32_t>(resolution, 1u);
  const float tan_half    = 1.f + 2.f * POINT_SHADOW_FACE_GUARD_TEXELS / texel_count;
  const float fov         = 2.f * std::atan(tan_half);

  static const Array<linalg::vec3, POINT_SHADOW_FACE_COUNT> AXES = {{
      {1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, -1.f},
  }};

  for (uint32_t index = 0; index < POINT_SHADOW_FACE_COUNT; ++index)
  {
    point_shadow_face_t &face    = result.faces[index];
    const linalg::vec3   forward = AXES[index];
    const linalg::vec3   up      = index == 2 || index == 3 ? linalg::vec3{0.f, 0.f, 1.f}
                                                            : linalg::vec3{0.f, 1.f, 0.f};
    const linalg::vec3   right   = linalg::normalize(linalg::cross(forward, up));
    const linalg::vec3   true_up = linalg::cross(right, forward);

    face.projection.view_projection =
        linalg::perspective(fov, 1.f, SHADOW_NEAR_PLANE, result.range) *
        linalg::look_at(light.position, light.position + forward, up);
    face.projection.texel_size_at_unit_distance = 2.f * tan_half / texel_count;
    face.projection.near_plane                  = SHADOW_NEAR_PLANE;
    face.projection.far_plane                   = result.range;

    const linalg::vec3 far_center = light.position + forward * result.range;
    const linalg::vec3 far_right  = right * (result.range * tan_half);
    const linalg::vec3 far_up     = true_up * (result.range * tan_half);
    face.corners[0]               = light.position;
    face.corners[1]               = far_center - far_right - far_up;
    face.corners[2]               = far_center + far_right - far_up;
    face.corners[3]               = far_center + far_right + far_up;
    face.corners[4]               = far_center - far_right + far_up;

    // Separating-plane test in both directions. Either set of planes alone can
    // miss a separation the other sees; neither can invent one.
    const linalg::vec3 face_inside = centroid_of(face.corners);
    const Array<oriented_plane_t, 5> face_planes = {{
        plane_through(face.corners[1], face.corners[2], face.corners[3], face_inside),
        plane_through(face.corners[0], face.corners[1], face.corners[2], face_inside),
        plane_through(face.corners[0], face.corners[2], face.corners[3], face_inside),
        plane_through(face.corners[0], face.corners[3], face.corners[4], face_inside),
        plane_through(face.corners[0], face.corners[4], face.corners[1], face_inside),
    }};

    face.visible = true;
    for (const oriented_plane_t &plane : camera_planes)
      if (every_point_outside(plane, face.corners))
        face.visible = false;
    for (const oriented_plane_t &plane : face_planes)
      if (every_point_outside(plane, camera_corners))
        face.visible = false;
  }

  return result;
}

void begin_frame_lights(frame_lights_t &frame, const lightmap_t &lightmap)
{
  frame.baked_count = (uint32_t)lightmap.light_uids.size();
  frame.entries.assign(frame.baked_count, scene_light_t{});

  // Zero radiance and not the default white: an unclaimed slot has to contribute
  // NOTHING, and radiance is the one field that guarantees it whatever the
  // shader does with the rest.
  for (scene_light_t &light : frame.entries) light.radiance = {0.f, 0.f, 0.f};
}

void add_frame_light(frame_lights_t &frame, const lightmap_t &lightmap,
                     entity_uid_t uid, const entities::Entity &entity)
{
  const std::optional<scene_light_t> gathered = try_light_of(entity);
  if (!gathered) return;

  scene_light_t light = *gathered;
  light.uid           = uid;

  // The resolve the bake's uid table exists for, and the one place it happens:
  // an entity is what the frame has and a slot is what the shader needs.
  light.baked_slot = find_baked_light_slot(lightmap, uid);

  const bool has_slot = light.baked_slot != LIGHTMAP_NO_LIGHT_SLOT &&
                        (uint32_t)light.baked_slot < frame.baked_count;

  // A light the bake DID see goes at its slot whatever its mode, because that
  // entry is what a chart's stored channel resolves to and the visibility beside
  // it is the shadow the runtime cannot compute.
  if (has_slot) frame.entries[(uint32_t)light.baked_slot] = light;

  // ...and the tail carries every ANALYTIC light, including the second copy of a
  // Mixed one. That copy is not redundancy: a surface with no chart has no slots
  // to read and sees only the tail, so this is how one light gets a baked shadow
  // on the wall and none on the player standing in front of it. A lightmapped
  // surface skips any tail entry carrying a slot, having already shaded it.
  //
  // A pure Baked light is deliberately NOT in the tail. It lights what the bake
  // measured and nothing else, which is exactly what it meant before it became
  // analytic -- lighting dynamic objects with it is lighting_def.md ss7's probes,
  // and not this.
  if (light_is_analytic(light.mode)) frame.entries.push_back(light);
}

} // namespace shared
