// The boolean of geometry_def.md §11.8 increment 13: a convex cutter out of
// any closed mesh. A box hole through a slab is a closed, planar, non-convex
// mesh of sixteen faces whose four faces on the cutter carry its material and
// whose volume is the slab less the overlap; a sixteen-sided hole has sixteen
// such faces; a niche keeps its floor; a cutter that misses answers the wall
// untouched; one that swallows answers no faces; two cuts compose; a trench
// leaves two shells; a non-convex wall and a posed wall cut the same; a
// non-convex cutter and an empty one are refused.

#include "shared/map/mesh/mesh.hpp"
#include "shared/map/mesh/mesh_boolean.hpp"
#include "shared/map/mesh/mesh_decomposition.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <numbers>
#include <optional>
#include <vector>

using linalg::vec3;
using shared::mesh_geometry_t;
using shared::mesh_subtraction_t;

namespace
{

constexpr float SLAB_HALF_WIDTH = 128.f;
constexpr float SLAB_HALF_THICKNESS = 8.f;
constexpr float SLAB_VOLUME = 256.f * 16.f * 256.f;
constexpr uint16_t CUTTER_MATERIAL = 3;

bool nearly(float a, float b) { return std::fabs(a - b) < 0.5f + std::fabs(b) * 1e-4f; }

mesh_geometry_t make_slab()
{
  return shared::make_box_mesh({0.f, 0.f, 0.f},
                               {SLAB_HALF_WIDTH, SLAB_HALF_THICKNESS, SLAB_HALF_WIDTH});
}

mesh_geometry_t with_cutter_material(mesh_geometry_t mesh)
{
  for (shared::mesh_face_t &face : mesh.faces)
    face.surface.material = CUTTER_MATERIAL;
  return mesh;
}

mesh_geometry_t make_box_cutter(const vec3 &center, const vec3 &half_extents)
{
  return with_cutter_material(shared::make_box_mesh(center, half_extents));
}

// A vertical prism of `sides` about `center`, radius `radius`, height `height`.
mesh_geometry_t make_prism_cutter(const vec3 &center, float radius, float height, uint32_t sides)
{
  std::vector<vec3> footprint;
  for (uint32_t i = 0; i < sides; ++i)
  {
    const float angle = 2.f * std::numbers::pi_v<float> * (float)i / (float)sides;
    footprint.push_back({radius * std::cos(angle), -height * 0.5f, radius * std::sin(angle)});
  }
  mesh_geometry_t prism = shared::make_extruded_mesh(footprint, {0.f, 1.f, 0.f}, height);
  if (shared::compute_mesh_volume(prism) < 0.f)
  {
    std::vector<vec3> reversed(footprint.rbegin(), footprint.rend());
    prism = shared::make_extruded_mesh(reversed, {0.f, 1.f, 0.f}, height);
  }
  prism.position = center;
  return with_cutter_material(prism);
}

float polygon_area(float radius, uint32_t sides)
{
  return 0.5f * (float)sides * radius * radius *
         std::sin(2.f * std::numbers::pi_v<float> / (float)sides);
}

// Every face tagged with a cutter face carries the cutter's material, and every
// other face does not; a wall that was cut before carries the earlier cutter's
// material on faces this cut tags as the wall's, so a composed cut skips that half.
uint32_t count_cutter_faces(const mesh_subtraction_t &cut, bool wall_faces_are_plain = true)
{
  uint32_t count = 0;
  for (size_t face_index = 0; face_index < cut.result.faces.size(); ++face_index)
  {
    if (cut.cutter_faces[face_index] == shared::MESH_SUBTRACTION_WALL_FACE)
    {
      assert(!wall_faces_are_plain ||
             cut.result.faces[face_index].surface.material != CUTTER_MATERIAL);
      continue;
    }
    assert(cut.result.faces[face_index].surface.material == CUTTER_MATERIAL);
    ++count;
  }
  return count;
}

void assert_solid(const mesh_subtraction_t &cut)
{
  assert(cut.cutter_faces.size() == cut.result.faces.size());
  const std::optional<shared::mesh_defect_t> defect = shared::find_mesh_defect(cut.result);
  if (defect)
    std::cout << "  defect: " << shared::format_mesh_defect(*defect) << "\n";
  assert(!defect);
}

void test_a_box_hole_through_a_slab()
{
  const mesh_geometry_t slab = make_slab();
  const mesh_geometry_t cutter = make_box_cutter({0.f, 0.f, 0.f}, {16.f, 32.f, 16.f});

  const std::optional<mesh_subtraction_t> cut = shared::try_subtract_convex_mesh(slab, cutter);
  assert(cut);
  assert(!cut->missed);
  assert_solid(*cut);
  assert(!shared::mesh_is_convex(cut->result));
  assert(cut->result.faces.size() == 16);
  assert(count_cutter_faces(*cut) == 4);
  assert(nearly(shared::compute_mesh_volume(cut->result), SLAB_VOLUME - 32.f * 16.f * 32.f));
  for (const vec3 &vertex : cut->result.vertices)
    assert(std::fabs(vertex.x) > 15.9f || std::fabs(vertex.z) > 15.9f);

  const std::optional<std::vector<mesh_geometry_t>> pieces =
      shared::try_decompose_mesh_into_convex_pieces(cut->result);
  assert(pieces);
  assert(pieces->size() >= 4);
}

void test_a_sixteen_sided_hole_has_sixteen_faces_on_the_cutter()
{
  const mesh_geometry_t slab = make_slab();
  const mesh_geometry_t cutter = make_prism_cutter({32.f, 0.f, -16.f}, 24.f, 64.f, 16);
  assert(cutter.faces.size() == 18);
  assert(shared::mesh_is_convex(cutter));

  const std::optional<mesh_subtraction_t> cut = shared::try_subtract_convex_mesh(slab, cutter);
  assert(cut);
  assert_solid(*cut);
  assert(count_cutter_faces(*cut) == 16);
  assert(nearly(shared::compute_mesh_volume(cut->result),
                SLAB_VOLUME - polygon_area(24.f, 16) * 16.f));
}

void test_a_niche_keeps_its_floor()
{
  const mesh_geometry_t slab = make_slab();
  const mesh_geometry_t cutter =
      make_box_cutter({0.f, SLAB_HALF_THICKNESS, 0.f}, {16.f, 8.f, 16.f});

  const std::optional<mesh_subtraction_t> cut = shared::try_subtract_convex_mesh(slab, cutter);
  assert(cut);
  assert_solid(*cut);
  assert(count_cutter_faces(*cut) == 5);
  assert(nearly(shared::compute_mesh_volume(cut->result), SLAB_VOLUME - 32.f * 8.f * 32.f));

  bool floor_found = false;
  for (size_t face_index = 0; face_index < cut->result.faces.size(); ++face_index)
  {
    if (cut->cutter_faces[face_index] == shared::MESH_SUBTRACTION_WALL_FACE)
      continue;
    const Plane plane = shared::compute_mesh_face_plane(cut->result, face_index);
    if (plane.normal.y > 0.99f && std::fabs(plane.point.y) < 0.01f)
      floor_found = true;
  }
  assert(floor_found);
}

void test_a_cutter_that_misses_leaves_the_wall_untouched()
{
  const mesh_geometry_t slab = make_slab();
  const mesh_geometry_t cutter = make_box_cutter({0.f, 64.f, 0.f}, {16.f, 16.f, 16.f});

  const std::optional<mesh_subtraction_t> cut = shared::try_subtract_convex_mesh(slab, cutter);
  assert(cut);
  assert(cut->missed);
  assert(cut->result.faces.size() == 6);
  assert(count_cutter_faces(*cut) == 0);
  assert(nearly(shared::compute_mesh_volume(cut->result), SLAB_VOLUME));

  const mesh_geometry_t touching = make_box_cutter({0.f, SLAB_HALF_THICKNESS + 16.f, 0.f},
                                                   {16.f, 16.f, 16.f});
  const std::optional<mesh_subtraction_t> touch = shared::try_subtract_convex_mesh(slab, touching);
  assert(touch);
  assert(touch->missed);
}

void test_a_cutter_that_swallows_leaves_nothing()
{
  const mesh_geometry_t slab = make_slab();
  const mesh_geometry_t cutter = make_box_cutter({0.f, 0.f, 0.f}, {256.f, 256.f, 256.f});

  const std::optional<mesh_subtraction_t> cut = shared::try_subtract_convex_mesh(slab, cutter);
  assert(cut);
  assert(!cut->missed);
  assert(cut->result.faces.empty());
}

void test_two_cuts_compose()
{
  const mesh_geometry_t slab = make_slab();
  const mesh_geometry_t first = make_box_cutter({-48.f, 0.f, 0.f}, {16.f, 32.f, 16.f});
  const mesh_geometry_t second = make_prism_cutter({48.f, 0.f, 0.f}, 20.f, 64.f, 8);

  const std::optional<mesh_subtraction_t> once = shared::try_subtract_convex_mesh(slab, first);
  assert(once);
  const std::optional<mesh_subtraction_t> twice =
      shared::try_subtract_convex_mesh(once->result, second);
  assert(twice);
  assert_solid(*twice);
  assert(count_cutter_faces(*twice, false) == 8);
  assert(nearly(shared::compute_mesh_volume(twice->result),
                SLAB_VOLUME - 32.f * 16.f * 32.f - polygon_area(20.f, 8) * 16.f));

  // The first hole's faces are still there with their material: the second
  // cut's fan planes split them where they cross, and lose nothing.
  float first_hole_area = 0.f;
  for (size_t face_index = 0; face_index < twice->result.faces.size(); ++face_index)
  {
    const Plane plane = shared::compute_mesh_face_plane(twice->result, face_index);
    if (std::fabs(plane.normal.y) < 0.01f && std::fabs(plane.point.x + 48.f) < 16.01f &&
        std::fabs(plane.point.z) < 16.01f)
    {
      assert(twice->result.faces[face_index].surface.material == CUTTER_MATERIAL);
      first_hole_area += shared::compute_mesh_face_area(twice->result, face_index);
    }
  }
  assert(nearly(first_hole_area, 4.f * 32.f * 16.f));
}

void test_a_trench_leaves_two_shells()
{
  const mesh_geometry_t slab = make_slab();
  const mesh_geometry_t cutter = make_box_cutter({0.f, 0.f, 0.f}, {16.f, 32.f, 200.f});

  const std::optional<mesh_subtraction_t> cut = shared::try_subtract_convex_mesh(slab, cutter);
  assert(cut);
  assert_solid(*cut);
  assert(shared::separate_mesh_shells(cut->result).size() == 2);
  assert(nearly(shared::compute_mesh_volume(cut->result), SLAB_VOLUME - 32.f * 16.f * 256.f));
}

void test_a_non_convex_wall_is_cut()
{
  const std::vector<vec3> l_footprint{{0.f, 0.f, 0.f},   {128.f, 0.f, 0.f}, {128.f, 64.f, 0.f},
                                      {64.f, 64.f, 0.f}, {64.f, 128.f, 0.f}, {0.f, 128.f, 0.f}};
  const mesh_geometry_t l_shape = shared::make_extruded_mesh(l_footprint, {0.f, 0.f, 1.f}, 96.f);
  assert(!shared::find_mesh_defect(l_shape));
  const float l_volume = (128.f * 64.f + 64.f * 64.f) * 96.f;
  assert(nearly(shared::compute_mesh_volume(l_shape), l_volume));

  const mesh_geometry_t cutter = make_box_cutter({32.f, 96.f, 48.f}, {8.f, 8.f, 100.f});
  const std::optional<mesh_subtraction_t> cut = shared::try_subtract_convex_mesh(l_shape, cutter);
  assert(cut);
  assert_solid(*cut);
  assert(count_cutter_faces(*cut) == 4);
  assert(nearly(shared::compute_mesh_volume(cut->result), l_volume - 16.f * 16.f * 96.f));
}

void test_a_posed_wall_keeps_its_pose()
{
  mesh_geometry_t slab = make_slab();
  slab.position = {300.f, 50.f, -200.f};
  slab.orientation = linalg::from_axis_angle({0.f, 1.f, 0.f}, 90.f);
  mesh_geometry_t cutter = make_box_cutter({300.f, 50.f, -200.f}, {16.f, 32.f, 16.f});
  cutter.orientation = linalg::from_axis_angle({0.f, 1.f, 0.f}, 45.f);

  const std::optional<mesh_subtraction_t> cut = shared::try_subtract_convex_mesh(slab, cutter);
  assert(cut);
  assert_solid(*cut);
  assert(count_cutter_faces(*cut) == 4);
  assert(linalg::length(cut->result.position - slab.position) < 1e-4f);
  assert(std::fabs(linalg::dot(cut->result.orientation, slab.orientation)) > 0.9999f);
  assert(nearly(shared::compute_mesh_volume(cut->result), SLAB_VOLUME - 32.f * 16.f * 32.f));

  const shared::aabb_bounds_t bounds = shared::compute_mesh_local_bounds(cut->result);
  assert(nearly(bounds.max.x - bounds.min.x, 2.f * SLAB_HALF_WIDTH));
  assert(nearly(bounds.max.y - bounds.min.y, 2.f * SLAB_HALF_THICKNESS));
}

void test_refusals()
{
  const mesh_geometry_t slab = make_slab();

  const std::vector<vec3> l_footprint{{0.f, 0.f, 0.f},   {32.f, 0.f, 0.f},  {32.f, 16.f, 0.f},
                                      {16.f, 16.f, 0.f}, {16.f, 32.f, 0.f}, {0.f, 32.f, 0.f}};
  const mesh_geometry_t l_cutter = shared::make_extruded_mesh(l_footprint, {0.f, 0.f, 1.f}, 32.f);
  assert(!shared::try_subtract_convex_mesh(slab, l_cutter));

  mesh_geometry_t empty;
  empty.source = shared::asset_mesh_source_t{"models/anything", {1.f, 1.f, 1.f}};
  assert(!shared::try_subtract_convex_mesh(slab, empty));

  mesh_geometry_t open = make_box_cutter({0.f, 0.f, 0.f}, {16.f, 32.f, 16.f});
  open.faces.pop_back();
  assert(!shared::try_subtract_convex_mesh(slab, open));
}

} // namespace

int main()
{
  test_a_box_hole_through_a_slab();
  test_a_sixteen_sided_hole_has_sixteen_faces_on_the_cutter();
  test_a_niche_keeps_its_floor();
  test_a_cutter_that_misses_leaves_the_wall_untouched();
  test_a_cutter_that_swallows_leaves_nothing();
  test_two_cuts_compose();
  test_a_trench_leaves_two_shells();
  test_a_non_convex_wall_is_cut();
  test_a_posed_wall_keeps_its_pose();
  test_refusals();
  std::cout << "mesh_boolean_test passed" << std::endl;
  return 0;
}
