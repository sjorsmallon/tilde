// A projector's frustum IS its frame (projector_plan.md ss2b): the frame's four corners at its range land on
// the map's four corners and on depth 1, rolled with the light, so the shadow layer's matrix is also the
// picture's uv. The frame planes hold the corners and refuse a point beside the frame; a shadow light
// built from a projector cuts the pyramid it throws to those planes and refuses a piece outside them.

#define ENTITIES_WANT_INCLUDES
#include "../shared/entities/generated/entities/projector_entity_generated.hpp"
#include "../shared/lighting/lighting.hpp"
#include "../shared/lighting/shadow_volume.hpp"
#include "../shared/map/map_geometry.hpp"
#include "../shared/map/mesh/mesh.hpp"

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

static bool inside_every_plane(const Array<Plane, 4>& planes, const vec3f& point)
{
  for (const Plane& plane : planes)
    if (linalg::dot(point - plane.point, plane.normal) > 1e-3f)
      return false;
  return true;
}

int main()
{
  constexpr uint32_t RESOLUTION = 1024;

  entities::Projector_Entity projector;
  projector.position           = {10.f, 50.f, -20.f};
  projector.orientation        = linalg::from_view_angles(35.f, -20.f);
  projector.range              = 400.f;
  projector.half_angle_degrees = 20.f;
  projector.aspect             = 1.7778f;

  const shared::path_pose_t pose{.position = projector.position, .orientation = projector.orientation};
  const std::optional<shared::scene_light_t> folded = shared::try_convert_light_entity_to_scene_light(projector, pose);
  check(folded.has_value(), "a projector folds into a scene light");
  if (!folded)
    return 1;
  const shared::scene_light_t& light = *folded;
  check(light.kind == shared::light_kind_t::Projector, "as the Projector kind");
  check(light.mode == entities::Light_Mode::Dynamic, "and is Dynamic whatever its Light says");
  check(near(light.aspect, 1.7778f), "carrying its aspect");
  check(near(linalg::dot(light.up, light.forward), 0.f), "its up square to its forward");

  const shared::shadow_projection_t projection = shared::projector_shadow_projection(light, RESOLUTION);
  const linalg::mat4f&              matrix     = projection.view_projection;

  const vec3f at_range = project(matrix, light.position + light.forward * light.range);
  check(near(at_range.x, 0.f) && near(at_range.y, 0.f), "the axis projects to the map centre");
  check(near(at_range.z, 1.f, 1e-4f), "the range projects to depth 1");

  // The four corners at the range: uv {0,1}^2, which is ndc +-1, and depth 1.
  const Array<vec3f, 4> corners = shared::compute_projector_frame_corners(light, light.range);
  const float expected_x[4] = {-1.f, 1.f, 1.f, -1.f};
  const float expected_y[4] = {1.f, 1.f, -1.f, -1.f}; // Vulkan clip y is down: the bottom corners are +1
  for (uint32_t corner = 0; corner < 4; ++corner)
  {
    const vec3f ndc = project(matrix, corners[corner]);
    check(near(ndc.x, expected_x[corner], 2e-3f), "a frame corner lands on the map's x edge");
    check(near(std::abs(ndc.y), 1.f, 2e-3f), "a frame corner lands on the map's y edge");
    check(near(ndc.z, 1.f, 1e-4f), "a frame corner at the range lands on depth 1");
    check(inside_every_plane(shared::compute_projector_frame_planes(light), corners[corner]),
          "a frame corner is inside every frame plane");
  }
  (void)expected_y;

  // The frame is rolled with the light: its top edge is along the light's up.
  const vec3f top_middle = (corners[2] + corners[3]) * 0.5f;
  const vec3f axis_point = light.position + light.forward * light.range;
  check(linalg::dot(linalg::normalize(top_middle - axis_point), light.up) > 0.999f,
        "the frame's top edge is along the light's up");
  const float half_height = light.range * std::tan(linalg::to_radians(20.f));
  check(near(linalg::length(top_middle - axis_point), half_height, 1e-2f), "at the half angle's height");
  const vec3f right_middle = (corners[1] + corners[2]) * 0.5f;
  check(near(linalg::length(right_middle - axis_point), half_height * 1.7778f, 1e-2f),
        "and the width is aspect times the height");

  // A point beside the frame is outside one of its planes.
  const vec3f beside = axis_point + (right_middle - axis_point) * 1.5f;
  check(!inside_every_plane(shared::compute_projector_frame_planes(light), beside),
        "a point past the frame's right edge is outside a frame plane");
  check(inside_every_plane(shared::compute_projector_frame_planes(light), axis_point), "the axis is inside");

  // The texel size is the frame's HEIGHT over the resolution, measured one unit out.
  check(near(projection.texel_size_at_unit_distance, 2.f * std::tan(linalg::to_radians(20.f)) / RESOLUTION, 1e-6f),
        "texel size is the frame's height at unit distance over the resolution");

  // As a shadow light: a switched-on projector with solid shadows throws volumes cut to its frame, and a crate
  // beside the frame casts nothing into it.
  projector.light.solid_shadows = entities::Solid_Shadows::Shadow_Volumes_Only;
  projector.switch_state.value      = true;
  projector.orientation             = linalg::from_view_angles(0.f, -90.f);
  projector.position                = {0.f, 300.f, 0.f};
  const std::optional<shared::shadow_light_t> shadow_light = shared::try_shadow_light_from_entity(
      projector, shared::path_pose_t{.position = projector.position, .orientation = projector.orientation});
  check(shadow_light.has_value(), "a projector whose shadows are solid is a shadow light");
  if (shadow_light)
  {
    check(shadow_light->frame_plane_count == 4, "with four frame planes");
    check(shadow_light->cosine_of_outer_angle <= -1.f, "and no cone");
    check(shadow_light->keeps_every_volume, "keeping every volume, since each is a mover");

    const auto cast_crate = [&](const vec3f& center)
    {
      const std::vector<shared::collision_piece_t> pieces =
          shared::get_collision_pieces(shared::make_box_mesh(center, {16.f, 16.f, 16.f}), 1);
      return shared::cast_shadow_volume(*shadow_light, pieces[0].planes, pieces[0].face_polygons, 7);
    };
    const shared::shadow_cast_t under = cast_crate({0.f, 100.f, 0.f});
    check(under.refusal == shared::shadow_cast_refusal_t::None, "a crate under the projector casts");
    check(under.volume.has_far_cap, "its volume is capped at the range");
    // The frame 200 units down, as the light's own axes place it: its right edge's middle is where a crate
    // sits half in, and three times that offset is clear of the frame.
    const std::optional<shared::scene_light_t> aimed_down = shared::try_convert_light_entity_to_scene_light(
        projector, shared::path_pose_t{.position = projector.position, .orientation = projector.orientation});
    const Array<vec3f, 4> frame_at_200 = shared::compute_projector_frame_corners(*aimed_down, 200.f);
    const vec3f           right_middle_at_200 = (frame_at_200[1] + frame_at_200[2]) * 0.5f;
    const vec3f           axis_at_200         = projector.position + aimed_down->forward * 200.f;
    const vec3f           right_offset        = right_middle_at_200 - axis_at_200;
    const shared::shadow_cast_t beside_the_frame = cast_crate(axis_at_200 + right_offset * 3.f);
    check(beside_the_frame.refusal == shared::shadow_cast_refusal_t::Outside_Spot_Beam,
          "a crate beside the frame casts nothing into it");
    // Half in: the frame's plane becomes a side of the volume.
    const shared::shadow_cast_t at_the_edge = cast_crate(right_middle_at_200);
    check(at_the_edge.refusal == shared::shadow_cast_refusal_t::None, "a crate across the frame's edge casts");
    check(at_the_edge.volume.side_plane_count > under.volume.side_plane_count,
          "and the frame's plane cuts its volume as a side");
  }

  if (failures == 0)
    std::printf("projector_test: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
