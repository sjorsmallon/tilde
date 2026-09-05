// Gate 9, the shadow-map maths that lives outside the renderer
// (shared/lighting.hpp). spot_shadow_projection: a spot light's frustum IS its
// cone, so the pin is geometric -- the cone's edge lands on the map's edge, the
// range lands on depth 1, and the texel size is the cone's width over the
// resolution. directional_shadow_cascades: every slice corner lands inside its
// cascade's map, a caster toward the sun is inside its depth range, a camera
// turn changes no box's size and a camera move shifts it by whole texels. The
// point_shadow_faces: a point light's six faces cover the six major-axis
// regions with a guard past each seam, the face pick agrees with the shader's,
// and a face the camera cannot see is culled. The receiver-side sampling reads
// the same matrices back out of the scene block, so a matrix that passes this
// is one the shader agrees with.

#include "../shared/lighting.hpp"
#include "../shared/shader_math.hpp"

#include <cmath>
#include <cstdio>

using linalg::vec3f;
using linalg::vec4;

static int failures = 0;

static void check(bool condition, const char* what)
{
  if (!condition)
  {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static vec3f project(const linalg::mat4f& matrix, const vec3f& point)
{
  const vec4 clip = matrix * vec4{point.x, point.y, point.z, 1.f};
  return {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
}

static bool near(float a, float b, float tolerance = 1e-3f)
{
  return std::abs(a - b) <= tolerance;
}

int main()
{
  constexpr float OUTER_DEGREES = 35.f;
  constexpr uint32_t RESOLUTION = 1024;

  shared::scene_light_t light;
  light.kind      = shared::light_kind_t::Spot;
  light.position  = {10.f, 50.f, -20.f};
  light.forward   = linalg::normalize(vec3f{1.f, -1.f, 0.f});
  light.range     = 512.f;
  light.cos_inner = std::cos(linalg::to_radians(20.f));
  light.cos_outer = std::cos(linalg::to_radians(OUTER_DEGREES));

  const shared::shadow_projection_t projection = shared::spot_shadow_projection(light, RESOLUTION);
  const linalg::mat4f&              matrix     = projection.view_projection;

  // Along the axis: the range is the far plane, the near plane is depth 0.
  const vec3f at_range = project(matrix, light.position + light.forward * light.range);
  check(near(at_range.x, 0.f) && near(at_range.y, 0.f), "the cone axis projects to the map centre");
  check(near(at_range.z, 1.f, 1e-4f), "the range projects to depth 1");

  const vec3f at_near = project(matrix, light.position + light.forward * shared::SHADOW_NEAR_PLANE);
  check(near(at_near.z, 0.f, 1e-4f), "the near plane projects to depth 0");

  // Across the axis: a point exactly on the cone's edge lands on the map's edge,
  // whichever world direction the light's frame put it in.
  const vec3f up    = {0.f, 1.f, 0.f};
  const vec3f right = linalg::normalize(linalg::cross(light.forward, up));
  const float half  = linalg::to_radians(OUTER_DEGREES);
  const vec3f edge_direction = light.forward * std::cos(half) + right * std::sin(half);
  const vec3f at_edge        = project(matrix, light.position + edge_direction * 200.f);
  check(near(std::abs(at_edge.x), 1.f) && near(at_edge.y, 0.f),
        "the cone's edge projects to the map's edge");
  check(at_edge.z > 0.f && at_edge.z < 1.f, "a point inside the range has a depth inside (0, 1)");

  const vec3f inside_direction = light.forward * std::cos(half * 0.5f) + right * std::sin(half * 0.5f);
  const vec3f at_inside        = project(matrix, light.position + inside_direction * 200.f);
  check(std::abs(at_inside.x) < 1.f, "a point inside the cone projects inside the map");

  const vec3f outside_direction = light.forward * std::cos(half * 1.2f) + right * std::sin(half * 1.2f);
  const vec3f at_outside        = project(matrix, light.position + outside_direction * 200.f);
  check(std::abs(at_outside.x) > 1.f, "a point outside the cone projects outside the map");

  // The texel size: the map spans 2 * tan(half) at unit distance.
  check(near(projection.texel_size_at_unit_distance, 2.f * std::tan(half) / (float)RESOLUTION, 1e-6f),
        "texel size is the cone's width at unit distance over the resolution");

  // PCSS (gate 9 step 5): a stored depth undoes to the distance it was
  // written from, through the same matrix, for a perspective map.
  check(near(projection.near_plane, shared::SHADOW_NEAR_PLANE) && near(projection.far_plane, light.range),
        "a spot map's near and far planes are the ones its matrix was built from");
  {
    bool round_trips = true;
    for (float distance : {1.5f, 10.f, 77.f, 300.f, 511.f})
    {
      const vec3f ndc    = project(matrix, light.position + light.forward * distance);
      const float linear = shared::shader_math::shadow_linear_depth(ndc.z, projection.near_plane,
                                                                    projection.far_plane, false);
      round_trips = round_trips && near(linear, distance, distance * 1e-3f);
    }
    check(round_trips, "shadow_linear_depth undoes a perspective map's depth warp");
  }

  // The penumbra: zero for a punctual light, zero for a blocker at the
  // receiver, and the similar-triangles width in texels otherwise.
  {
    const float t = projection.texel_size_at_unit_distance;
    check(near(shared::shader_math::shadow_penumbra_texels(0.f, 100.f, 50.f, t, false), 0.f),
          "a punctual light has no penumbra");
    check(near(shared::shader_math::shadow_penumbra_texels(4.f, 100.f, 100.f, t, false), 0.f),
          "a blocker touching the receiver casts no penumbra");
    // R = 4, receiver 100, blocker 50: 4 * 50 / 50 = 4 world units at the
    // receiver, over the texel there (t * 100).
    check(near(shared::shader_math::shadow_penumbra_texels(4.f, 100.f, 50.f, t, false),
               4.f / (t * 100.f), 1e-2f),
          "a perspective penumbra is R(z-b)/b over the receiver's texel size");
    // Orthographic: the radius is per unit distance and the texel is constant.
    check(near(shared::shader_math::shadow_penumbra_texels(0.01f, 300.f, 100.f, 2.f, true), 1.f),
          "an orthographic penumbra is R(z-b) over the texel");
    check(shared::shader_math::shadow_penumbra_texels(4.f, 100.f, shared::SHADOW_NEAR_PLANE, t, false) >
              shared::shader_math::shadow_penumbra_texels(4.f, 100.f, 50.f, t, false),
          "the search radius (blocker at the near plane) bounds every penumbra");
  }

  // A cone wider than a perspective map can hold is clamped, not exploded.
  light.cos_outer = std::cos(linalg::to_radians(120.f));
  const shared::shadow_projection_t clamped = shared::spot_shadow_projection(light, RESOLUTION);
  check(std::isfinite(clamped.texel_size_at_unit_distance) &&
            clamped.texel_size_at_unit_distance < 2.f * std::tan(linalg::to_radians(86.f)) / (float)RESOLUTION,
        "a hemisphere cone clamps to the widest map a perspective can hold");

  // A light aimed straight down, where world up is parallel to the cone axis,
  // still gets a frame.
  light.cos_outer = std::cos(linalg::to_radians(OUTER_DEGREES));
  light.forward   = {0.f, -1.f, 0.f};
  const shared::shadow_projection_t down    = shared::spot_shadow_projection(light, RESOLUTION);
  const vec3f                       at_down = project(down.view_projection, light.position + light.forward * 100.f);
  check(near(at_down.x, 0.f) && near(at_down.y, 0.f) && std::isfinite(at_down.z),
        "a straight-down spot light projects its axis to the map centre");

  // --- The sun's cascades (gate 9 step 2) ---

  shared::scene_light_t sun;
  sun.kind    = shared::light_kind_t::Directional;
  sun.forward = linalg::normalize(vec3f{0.3f, -1.f, 0.2f});

  shared::shadow_view_t view;
  view.position       = {100.f, 64.f, -40.f};
  view.forward        = linalg::normalize(vec3f{1.f, -0.2f, 0.5f});
  view.right          = linalg::normalize(linalg::cross(view.forward, vec3f{0.f, 1.f, 0.f}));
  view.up             = linalg::cross(view.right, view.forward);
  view.tan_half_fov_y = std::tan(linalg::to_radians(45.f));
  view.aspect         = 16.f / 9.f;
  view.near_plane     = 1.f;

  shared::cascade_settings_t settings;
  settings.count         = 3;
  settings.lambda        = 0.7f;
  settings.far_distance  = 4096.f;
  settings.caster_extent = 8192.f;

  check(near(shared::cascade_split_depth(view, settings, 0), 1.f), "split 0 is the near plane");
  check(near(shared::cascade_split_depth(view, settings, 3), 4096.f), "the last split is the far distance");
  check(shared::cascade_split_depth(view, settings, 1) < shared::cascade_split_depth(view, settings, 2) &&
            shared::cascade_split_depth(view, settings, 2) < shared::cascade_split_depth(view, settings, 3),
        "splits ascend");
  {
    shared::cascade_settings_t uniform = settings;
    uniform.lambda                     = 0.f;
    check(near(shared::cascade_split_depth(view, uniform, 1), 1.f + 4095.f / 3.f, 1e-2f),
          "lambda 0 splits uniformly in depth");
    shared::cascade_settings_t logarithmic = settings;
    logarithmic.lambda                     = 1.f;
    check(near(shared::cascade_split_depth(view, logarithmic, 1), 16.f, 1e-2f),
          "lambda 1 splits logarithmically (4096^(1/3) = 16)");
  }

  const shared::shadow_cascades_t cascades =
      shared::directional_shadow_cascades(sun, view, settings, RESOLUTION);
  check(cascades.count == 3, "three cascades were fit");

  for (uint32_t index = 0; index < cascades.count; ++index)
  {
    const shared::shadow_cascade_t &cascade = cascades.cascades[index];
    const linalg::mat4f            &matrix  = cascade.projection.view_projection;

    bool corners_inside = true;
    for (const vec3f &corner : cascade.slice_corners)
    {
      const vec3f ndc = project(matrix, corner);
      corners_inside  = corners_inside && std::abs(ndc.x) <= 1.f + 1e-3f &&
                       std::abs(ndc.y) <= 1.f + 1e-3f && ndc.z >= -1e-3f && ndc.z <= 1.f + 1e-3f;
    }
    check(corners_inside, "every corner of a frustum slice projects inside its cascade's map");

    const vec3f toward_sun = cascade.sphere_center - sun.forward * (cascade.sphere_radius + settings.caster_extent * 0.5f);
    const vec3f at_caster  = project(matrix, toward_sun);
    check(at_caster.z > 0.f && at_caster.z < 1.f, "a caster between the sun and the slice is inside the depth range");

    check(near(cascade.projection.texel_size_at_unit_distance,
               2.f * cascade.sphere_radius / (float)RESOLUTION, 1e-6f),
          "a cascade's texel is its sphere's diameter over the resolution");

    check(cascade.projection.near_plane == 0.f && cascade.projection.far_plane > 0.f,
          "a cascade's map is orthographic: near plane 0, a positive depth range");
    {
      const vec3f box_eye  = cascade.box_corners[0] * 0.5f + cascade.box_corners[2] * 0.5f;
      const float along    = cascade.projection.far_plane * 0.37f;
      const vec3f sample   = box_eye + sun.forward * along;
      const vec3f ndc      = project(matrix, sample);
      const float linear   = shared::shader_math::shadow_linear_depth(ndc.z, cascade.projection.near_plane,
                                                                      cascade.projection.far_plane, true);
      check(near(linear, along, along * 1e-3f), "shadow_linear_depth reads an orthographic depth back linearly");
    }

    if (index + 1 < cascades.count)
      check(near(cascade.far_depth, cascades.cascades[index + 1].near_depth), "cascades tile the depth range");
  }
  check(near(cascades.cascades[0].near_depth, 1.f) && near(cascades.cascades[2].far_depth, 4096.f),
        "the cascades span near to far");

  // A camera TURN leaves every box's size alone: the fit is to the slice's
  // bounding sphere, which does not know which way the frustum points.
  {
    shared::shadow_view_t turned = view;
    turned.forward               = linalg::normalize(vec3f{-0.4f, 0.1f, 1.f});
    turned.right                 = linalg::normalize(linalg::cross(turned.forward, vec3f{0.f, 1.f, 0.f}));
    turned.up                    = linalg::cross(turned.right, turned.forward);
    const shared::shadow_cascades_t after_turn =
        shared::directional_shadow_cascades(sun, turned, settings, RESOLUTION);
    bool same_size = true;
    for (uint32_t index = 0; index < cascades.count; ++index)
      same_size = same_size && near(after_turn.cascades[index].sphere_radius,
                                    cascades.cascades[index].sphere_radius, 1e-2f);
    check(same_size, "turning the camera changes no cascade's size");
  }

  // A camera MOVE shifts the map by whole texels: a fixed world point lands a
  // whole number of texels from where it was, so the texel grid stays put.
  {
    shared::shadow_view_t moved = view;
    moved.position              = view.position + view.forward * 0.37f + view.right * 0.61f;
    const shared::shadow_cascades_t after_move =
        shared::directional_shadow_cascades(sun, moved, settings, RESOLUTION);
    bool whole_texels = true;
    for (uint32_t index = 0; index < cascades.count; ++index)
    {
      const vec3f fixed_point = {12.f, 3.f, -7.f};
      const vec3f before      = project(cascades.cascades[index].projection.view_projection, fixed_point);
      const vec3f after       = project(after_move.cascades[index].projection.view_projection, fixed_point);
      const float shift_x     = (after.x - before.x) * 0.5f * (float)RESOLUTION;
      const float shift_y     = (after.y - before.y) * 0.5f * (float)RESOLUTION;
      whole_texels = whole_texels && near(shift_x, std::round(shift_x), 2e-2f) &&
                     near(shift_y, std::round(shift_y), 2e-2f);
    }
    check(whole_texels, "moving the camera shifts a cascade's map by whole texels");
  }

  // --- The point-light cube (gate 9 step 3) ---
  {
    shared::scene_light_t point;
    point.kind     = shared::light_kind_t::Point;
    point.position = {0.f, 0.f, 0.f};
    point.range    = 100.f;

    shared::shadow_view_t view;
    view.position       = {300.f, 0.f, 0.f};
    view.forward        = {-1.f, 0.f, 0.f};
    view.right          = {0.f, 0.f, -1.f};
    view.up             = {0.f, 1.f, 0.f};
    view.tan_half_fov_y = std::tan(linalg::to_radians(30.f));
    view.aspect         = 16.f / 9.f;

    const shared::point_shadow_faces_t faces = shared::point_shadow_faces(point, view, RESOLUTION);

    // The face order is the shader's: +X, -X, +Y, -Y, +Z, -Z by major axis.
    check(shared::point_shadow_face_of({5.f, 1.f, -2.f}) == 0, "+X is face 0");
    check(shared::point_shadow_face_of({-5.f, 4.f, 4.f}) == 1, "-X is face 1");
    check(shared::point_shadow_face_of({1.f, 9.f, -2.f}) == 2, "+Y is face 2");
    check(shared::point_shadow_face_of({0.f, -3.f, 2.f}) == 3, "-Y is face 3");
    check(shared::point_shadow_face_of({1.f, 1.f, 7.f}) == 4, "+Z is face 4");
    check(shared::point_shadow_face_of({2.f, -2.f, -3.f}) == 5, "-Z is face 5");

    // Each face's axis projects to its map centre and the range to depth 1.
    const vec3f axes[6] = {{1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
                           {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, -1.f}};
    bool axis_centred = true;
    for (uint32_t face = 0; face < 6; ++face)
    {
      const vec3f at_range =
          project(faces.faces[face].projection.view_projection, axes[face] * point.range);
      axis_centred = axis_centred && near(at_range.x, 0.f) && near(at_range.y, 0.f) &&
                     near(at_range.z, 1.f, 1e-4f);
      check(shared::point_shadow_face_of(axes[face]) == face, "a face's axis picks that face");
    }
    check(axis_centred, "every face's axis projects to its map centre at depth 1");
    check(near(faces.faces[0].projection.near_plane, shared::SHADOW_NEAR_PLANE) &&
              near(faces.faces[0].projection.far_plane, point.range),
          "a point face's near and far planes are the ones its matrix was built from");

    // A point on the seam between +X and +Y is inside BOTH maps, by the guard:
    // the kernel at a seam never reads outside the face it was picked into.
    {
      const vec3f seam = {50.f, 50.f, 10.f};
      const vec3f in_x = project(faces.faces[0].projection.view_projection, seam);
      const vec3f in_y = project(faces.faces[2].projection.view_projection, seam);
      const float guard_ndc = 2.f * shared::POINT_SHADOW_FACE_GUARD_TEXELS / (float)RESOLUTION;
      check(std::abs(in_x.x) < 1.f && std::abs(in_x.y) < 1.f, "a seam point is inside the +X map");
      check(std::abs(in_y.x) < 1.f && std::abs(in_y.y) < 1.f, "a seam point is inside the +Y map");
      check(near(std::max(std::abs(in_x.x), std::abs(in_x.y)), 1.f - guard_ndc, 2e-2f),
            "the seam sits the guard's width inside the map edge");
      check(near(faces.faces[0].projection.texel_size_at_unit_distance,
                 2.f * (1.f + guard_ndc) / (float)RESOLUTION, 1e-6f),
            "a face texel one unit out is the guarded width over the resolution");
    }

    // Looking at the light from +X: the +X face is in view.
    check(faces.faces[0].visible, "the face turned toward the camera is visible");

    // Looking AWAY from the light: every face is behind the camera and culled.
    {
      shared::shadow_view_t away = view;
      away.forward              = {1.f, 0.f, 0.f};
      away.right                = {0.f, 0.f, 1.f};
      const shared::point_shadow_faces_t culled = shared::point_shadow_faces(point, away, RESOLUTION);
      bool all_culled = true;
      for (uint32_t face = 0; face < 6; ++face)
        all_culled = all_culled && !culled.faces[face].visible;
      check(all_culled, "looking away from a point light culls all six faces");
    }

    // A narrow camera looking PAST the light, none of whose frustum reaches the
    // light's range, culls every face too -- the near plane is not the only test.
    {
      shared::shadow_view_t past = view;
      past.forward              = {0.f, 1.f, 0.f};
      past.right                = {0.f, 0.f, -1.f};
      past.up                   = {-1.f, 0.f, 0.f};
      past.tan_half_fov_y       = std::tan(linalg::to_radians(10.f));
      const shared::point_shadow_faces_t culled = shared::point_shadow_faces(point, past, RESOLUTION);
      bool all_culled = true;
      for (uint32_t face = 0; face < 6; ++face)
        all_culled = all_culled && !culled.faces[face].visible;
      check(all_culled, "a narrow camera looking past a point light culls all six faces");
    }
  }

  if (failures == 0)
    std::printf("shadow_test: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
