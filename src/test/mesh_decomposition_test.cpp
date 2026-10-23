// The reflex-edge decomposition of geometry_def.md §11.3: a convex mesh is one
// piece untouched, the L-shape is two, a U is three, a 16-segment arch is its
// 16 segments; every piece is closed, planar and convex, and the pieces'
// volumes sum to the source with the union bounds matching. The split under
// it: a plane that misses or only touches is refused, a cut through the middle
// of a box yields two closed halves whose caps carry the cap surface, a cut
// through vertices keeps both halves closed, and a cut that leaves a half in
// two arms comes back as two shells with no ring threaded between them.

#include "shared/map/mesh/mesh.hpp"
#include "shared/map/mesh/mesh_decomposition.hpp"
#include "shared/map/mesh/mesh_split.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

using linalg::vec3;
using shared::mesh_geometry_t;

namespace
{

constexpr float DEPTH = 96.f;

mesh_geometry_t extrude(const std::vector<vec3> &footprint)
{
  return shared::make_extruded_mesh(footprint, {0.f, 0.f, 1.f}, DEPTH);
}

mesh_geometry_t make_l_shape()
{
  return extrude({{0.f, 0.f, 0.f}, {128.f, 0.f, 0.f}, {128.f, 64.f, 0.f},
                  {64.f, 64.f, 0.f}, {64.f, 128.f, 0.f}, {0.f, 128.f, 0.f}});
}

mesh_geometry_t make_u_shape()
{
  return extrude({{0.f, 0.f, 0.f}, {192.f, 0.f, 0.f}, {192.f, 128.f, 0.f},
                  {128.f, 128.f, 0.f}, {128.f, 64.f, 0.f}, {64.f, 64.f, 0.f},
                  {64.f, 128.f, 0.f}, {0.f, 128.f, 0.f}});
}

// Half an annulus standing on y = 0: the outer arc counter-clockwise about the
// origin, the inner arc back, every rib radial so the segment boundaries meet
// the outer ring at a vertex.
mesh_geometry_t make_arch(uint32_t segments, float inner_radius, float outer_radius)
{
  std::vector<vec3> footprint;
  for (uint32_t i = 0; i <= segments; ++i)
  {
    const float angle = std::numbers::pi_v<float> * (float)i / (float)segments;
    footprint.push_back({outer_radius * std::cos(angle), outer_radius * std::sin(angle), 0.f});
  }
  for (uint32_t i = segments + 1; i-- > 0;)
  {
    const float angle = std::numbers::pi_v<float> * (float)i / (float)segments;
    footprint.push_back({inner_radius * std::cos(angle), inner_radius * std::sin(angle), 0.f});
  }
  return extrude(footprint);
}

bool nearly(float a, float b, float tolerance)
{
  return std::fabs(a - b) <= tolerance;
}

bool bounds_match(const shared::aabb_bounds_t &lhs, const shared::aabb_bounds_t &rhs)
{
  return linalg::length(lhs.min - rhs.min) < 1e-3f && linalg::length(lhs.max - rhs.max) < 1e-3f;
}

bool point_is_inside(const mesh_geometry_t &convex, const vec3 &point)
{
  for (uint32_t face_index = 0; face_index < convex.faces.size(); ++face_index)
  {
    const Plane plane = shared::compute_mesh_face_plane(convex, face_index);
    if (linalg::dot(plane.normal, point - plane.point) > 0.01f)
      return false;
  }
  return true;
}

bool point_is_in_any_piece(const std::vector<mesh_geometry_t> &pieces, const vec3 &point)
{
  for (const mesh_geometry_t &piece : pieces)
    if (point_is_inside(piece, point))
      return true;
  return false;
}

// What every decomposition must satisfy, whatever the shape.
std::vector<mesh_geometry_t> decompose_and_check(const mesh_geometry_t &source,
                                                 size_t expected_piece_count)
{
  const std::optional<std::vector<mesh_geometry_t>> pieces =
      shared::try_decompose_mesh_into_convex_pieces(source);
  assert(pieces.has_value());
  std::cout << "  " << pieces->size() << " pieces\n";
  assert(pieces->size() == expected_piece_count);

  shared::aabb_bounds_t union_bounds = shared::compute_mesh_local_bounds(pieces->front());
  float volume_sum = 0.f;
  for (const mesh_geometry_t &piece : *pieces)
  {
    assert(!shared::find_mesh_defect(piece).has_value());
    assert(shared::mesh_is_convex(piece));
    assert(!shared::find_reflex_edge(piece).has_value());
    assert(linalg::length(piece.position - source.position) == 0.f);
    volume_sum += shared::compute_mesh_volume(piece);
    union_bounds = shared::union_aabb(union_bounds, shared::compute_mesh_local_bounds(piece));
  }
  const float source_volume = shared::compute_mesh_volume(source);
  assert(source_volume > 0.f);
  assert(nearly(volume_sum, source_volume, source_volume * 1e-4f));
  assert(bounds_match(union_bounds, shared::compute_mesh_local_bounds(source)));
  return *pieces;
}

// ---------------------------------------------------------------------------

void test_a_convex_mesh_is_one_piece_untouched()
{
  const mesh_geometry_t box = shared::make_box_mesh({10.f, 20.f, 30.f}, {32.f, 64.f, 128.f});
  assert(!shared::find_reflex_edge(box).has_value());
  assert(shared::mesh_is_convex(box));
  assert(nearly(shared::compute_mesh_volume(box), 8.f * 32.f * 64.f * 128.f, 1.f));

  const std::vector<mesh_geometry_t> pieces = decompose_and_check(box, 1);
  assert(pieces[0].faces.size() == 6);
  assert(pieces[0].vertices.size() == 8);

  const shared::collision_piece_t piece = shared::collision_piece_from_convex_mesh(pieces[0]);
  assert(piece.planes.size() == 6);
  assert(piece.face_polygons.size() == 6);
  assert(bounds_match(piece.bounds, {{-22.f, -44.f, -98.f}, {42.f, 84.f, 158.f}}));
}

void test_the_l_shape_is_two_pieces()
{
  const mesh_geometry_t l_shape = make_l_shape();
  const std::optional<shared::reflex_edge_t> reflex = shared::find_reflex_edge(l_shape);
  assert(reflex.has_value());
  assert(reflex->face_index == 4 && reflex->other_face_index == 5);
  assert(!shared::mesh_is_convex(l_shape));

  const std::vector<mesh_geometry_t> pieces = decompose_and_check(l_shape, 2);
  assert(!point_is_in_any_piece(pieces, {96.f, 96.f, 48.f}));
  assert(point_is_in_any_piece(pieces, {32.f, 96.f, 48.f}));
  assert(point_is_in_any_piece(pieces, {96.f, 32.f, 48.f}));
}

void test_the_u_shape_is_three_pieces()
{
  const std::vector<mesh_geometry_t> pieces = decompose_and_check(make_u_shape(), 3);
  assert(!point_is_in_any_piece(pieces, {96.f, 96.f, 48.f}));
  assert(point_is_in_any_piece(pieces, {32.f, 96.f, 48.f}));
  assert(point_is_in_any_piece(pieces, {160.f, 96.f, 48.f}));
  assert(point_is_in_any_piece(pieces, {96.f, 32.f, 48.f}));
}

void test_an_arch_is_its_segments()
{
  const mesh_geometry_t arch = make_arch(16, 64.f, 96.f);
  assert(!shared::find_mesh_defect(arch).has_value());
  const std::vector<mesh_geometry_t> pieces = decompose_and_check(arch, 16);
  for (const mesh_geometry_t &piece : pieces)
    assert(piece.faces.size() == 6 && piece.vertices.size() == 8);
  assert(!point_is_in_any_piece(pieces, {0.f, 32.f, 48.f}));
  assert(point_is_in_any_piece(pieces, {0.f, 80.f, 48.f}));
}

void test_a_plane_that_misses_or_touches_is_refused()
{
  const mesh_geometry_t box = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  assert(!shared::try_split_mesh_by_plane(box, {{64.f, 0.f, 0.f}, {1.f, 0.f, 0.f}}, {}));
  assert(!shared::try_split_mesh_by_plane(box, {{32.f, 0.f, 0.f}, {1.f, 0.f, 0.f}}, {}));
}

void test_a_cut_through_a_box_yields_two_capped_halves()
{
  const mesh_geometry_t box = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  shared::face_surface_t cap_surface;
  cap_surface.material = 7;
  const std::optional<shared::mesh_split_t> split =
      shared::try_split_mesh_by_plane(box, {{8.f, 0.f, 0.f}, {1.f, 0.f, 0.f}}, cap_surface);
  assert(split.has_value());
  assert(split->cap_face_count == 1);
  for (const mesh_geometry_t *half : {&split->front, &split->back})
  {
    assert(!shared::find_mesh_defect(*half).has_value());
    assert(half->faces.size() == 6 && half->vertices.size() == 8);
    assert(half->faces.back().surface.material == 7);
    assert(shared::mesh_is_convex(*half));
  }
  assert(nearly(shared::compute_mesh_volume(split->front), 24.f * 64.f * 64.f, 1.f));
  assert(nearly(shared::compute_mesh_volume(split->back), 40.f * 64.f * 64.f, 1.f));
  assert(nearly(shared::compute_mesh_face_area(split->back, 5), 64.f * 64.f, 1e-2f));
  assert(nearly(shared::compute_mesh_face_plane(split->back, 5).normal.x, 1.f, 1e-5f));
  assert(nearly(shared::compute_mesh_face_plane(split->front, 5).normal.x, -1.f, 1e-5f));
}

// A grid is a patch over the quad it was sculpted on; the part of that quad
// on one side of the cut is a different face, so it comes out flat, and the
// faces the plane never crosses keep theirs.
void test_a_cut_through_a_subdivided_face_comes_out_flat()
{
  mesh_geometry_t box = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  for (shared::mesh_face_t &face : box.faces)
    shared::resize_face_grid(face.surface, 2);
  const Plane plane{{8.f, 0.f, 0.f}, {1.f, 0.f, 0.f}};
  assert(shared::count_faces_crossed_by_plane(box, plane) == 4);
  assert(shared::count_subdivided_faces_crossed_by_plane(box, plane) == 4);

  const std::optional<shared::mesh_split_t> split = shared::try_split_mesh_by_plane(box, plane, {});
  assert(split.has_value());
  for (const mesh_geometry_t *half : {&split->front, &split->back})
  {
    assert(!shared::find_mesh_defect(*half).has_value());
    uint32_t subdivided = 0;
    for (const shared::mesh_face_t &face : half->faces)
      subdivided += shared::face_is_subdivided(face.surface);
    assert(subdivided == 1);
  }
  std::cout << "  the crossed grids are flat, the two uncrossed faces keep theirs\n";
}

void test_a_cut_through_vertices_keeps_both_halves_closed()
{
  const mesh_geometry_t l_shape = make_l_shape();
  const std::optional<shared::mesh_split_t> split =
      shared::try_split_mesh_by_plane(l_shape, {{64.f, 0.f, 0.f}, {1.f, 0.f, 0.f}}, {});
  assert(split.has_value());
  assert(split->cap_face_count == 1);
  assert(!shared::find_mesh_defect(split->front).has_value());
  assert(!shared::find_mesh_defect(split->back).has_value());
  assert(nearly(shared::compute_mesh_volume(split->front), 64.f * 64.f * DEPTH, 1.f));
  assert(nearly(shared::compute_mesh_volume(split->back), 64.f * 128.f * DEPTH, 1.f));
}

void test_a_cut_that_separates_arms_comes_back_as_shells()
{
  const mesh_geometry_t u_shape = make_u_shape();
  const std::optional<shared::mesh_split_t> split =
      shared::try_split_mesh_by_plane(u_shape, {{0.f, 64.f, 0.f}, {0.f, 1.f, 0.f}}, {});
  assert(split.has_value());
  assert(split->cap_face_count == 2);
  assert(!shared::find_mesh_defect(split->front).has_value());
  const std::vector<mesh_geometry_t> arms = shared::separate_mesh_shells(split->front);
  assert(arms.size() == 2);
  for (const mesh_geometry_t &arm : arms)
  {
    assert(!shared::find_mesh_defect(arm).has_value());
    assert(arm.faces.size() == 6 && arm.vertices.size() == 8);
    assert(nearly(shared::compute_mesh_volume(arm), 64.f * 64.f * DEPTH, 1.f));
  }
  assert(shared::separate_mesh_shells(split->back).size() == 1);
}

void test_an_open_mesh_is_refused()
{
  mesh_geometry_t box = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  box.faces.pop_back();
  assert(!shared::try_decompose_mesh_into_convex_pieces(box).has_value());
}

} // namespace

int main()
{
  test_a_convex_mesh_is_one_piece_untouched();
  test_the_l_shape_is_two_pieces();
  test_the_u_shape_is_three_pieces();
  test_an_arch_is_its_segments();
  test_a_plane_that_misses_or_touches_is_refused();
  test_a_cut_through_a_box_yields_two_capped_halves();
  test_a_cut_through_vertices_keeps_both_halves_closed();
  test_a_cut_through_a_subdivided_face_comes_out_flat();
  test_a_cut_that_separates_arms_comes_back_as_shells();
  test_an_open_mesh_is_refused();

  std::cout << "all mesh decomposition tests passed" << std::endl;
  return 0;
}
