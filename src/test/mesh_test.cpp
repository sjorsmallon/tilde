// The mesh value and its two invariants (geometry_def.md §11.3): a box and an
// L-shape are closed planar meshes; an open mesh, a bent face, a face wound the
// wrong way and a stray index are each refused by name. The coplanar epsilon
// gets the same guard brush_test gave it: a lift inside it is accepted, one
// outside it is not.

#include "shared/map/mesh/mesh.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using shared::mesh_defect_kind_t;
using shared::mesh_defect_t;
using shared::mesh_geometry_t;

namespace
{

bool nearly(const linalg::vec3 &a, const linalg::vec3 &b)
{
  return linalg::length(a - b) < 1e-4f;
}

void assert_defect(const mesh_geometry_t &mesh, mesh_defect_kind_t kind)
{
  const std::optional<mesh_defect_t> defect = shared::find_mesh_defect(mesh);
  assert(defect.has_value());
  assert(defect->kind == kind);
  std::cout << "  refused: " << shared::format_mesh_defect(*defect) << "\n";
}

void test_a_box_is_a_closed_planar_mesh()
{
  const mesh_geometry_t box = shared::make_box_mesh({10.f, 20.f, 30.f}, {32.f, 64.f, 128.f});
  assert(box.vertices.size() == 8);
  assert(box.faces.size() == 6);
  assert(!shared::find_mesh_defect(box).has_value());
  assert(shared::mesh_is_closed(box));
  assert(nearly(box.position, {10.f, 20.f, 30.f}));

  for (uint32_t face_index = 0; face_index < 6; ++face_index)
  {
    const Plane plane = shared::compute_mesh_face_plane(box, face_index);
    assert(std::fabs(linalg::length(plane.normal) - 1.f) < 1e-5f);
    assert(linalg::dot(plane.normal, plane.point) > 0.f);
    const float reach = std::fabs(linalg::dot(plane.normal, plane.point));
    assert(std::fabs(reach - 32.f) < 1e-4f || std::fabs(reach - 64.f) < 1e-4f ||
           std::fabs(reach - 128.f) < 1e-4f);
  }
}

void test_an_l_shape_is_a_closed_planar_mesh()
{
  const std::vector<linalg::vec3> footprint = {
      {0.f, 0.f, 0.f}, {128.f, 0.f, 0.f}, {128.f, 64.f, 0.f},
      {64.f, 64.f, 0.f}, {64.f, 128.f, 0.f}, {0.f, 128.f, 0.f},
  };
  const mesh_geometry_t l_shape = shared::make_extruded_mesh(footprint, {0.f, 0.f, 1.f}, 96.f);
  assert(l_shape.vertices.size() == 12);
  assert(l_shape.faces.size() == 8);
  assert(!shared::find_mesh_defect(l_shape).has_value());
  assert(shared::mesh_is_closed(l_shape));

  assert(nearly(shared::compute_mesh_face_plane(l_shape, 0).normal, {0.f, 0.f, -1.f}));
  assert(nearly(shared::compute_mesh_face_plane(l_shape, 1).normal, {0.f, 0.f, 1.f}));
  assert(nearly(shared::compute_mesh_face_plane(l_shape, 2).normal, {0.f, -1.f, 0.f}));
  assert(nearly(shared::compute_mesh_face_plane(l_shape, 5).normal, {1.f, 0.f, 0.f}));
  assert(nearly(shared::compute_mesh_face_plane(l_shape, 6).normal, {0.f, 1.f, 0.f}));
  assert(nearly(shared::compute_mesh_face_plane(l_shape, 7).normal, {-1.f, 0.f, 0.f}));
}

void test_an_open_mesh_is_refused()
{
  mesh_geometry_t box = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  box.faces.pop_back();
  assert(!shared::mesh_is_closed(box));
  assert_defect(box, mesh_defect_kind_t::Edge_Is_Unpaired);
}

void test_a_bent_face_is_refused_and_the_epsilon_holds()
{
  mesh_geometry_t box = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  box.vertices[7].x += 1.f;
  assert_defect(box, mesh_defect_kind_t::Face_Is_Bent);

  mesh_geometry_t nudged = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  nudged.vertices[7].x += 0.01f;
  assert(!shared::find_mesh_defect(nudged).has_value());
}

void test_a_face_wound_the_wrong_way_is_refused()
{
  mesh_geometry_t box = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  std::vector<uint32_t> &ring = box.faces[2].vertex_indices;
  std::reverse(ring.begin(), ring.end());
  assert_defect(box, mesh_defect_kind_t::Edge_Is_Doubled);
}

void test_structural_defects_are_refused()
{
  mesh_geometry_t box = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  box.faces[0].vertex_indices[1] = 99;
  assert_defect(box, mesh_defect_kind_t::Vertex_Index_Out_Of_Range);

  mesh_geometry_t repeated = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  repeated.faces[0].vertex_indices[1] = repeated.faces[0].vertex_indices[0];
  assert_defect(repeated, mesh_defect_kind_t::Face_Repeats_A_Vertex);

  mesh_geometry_t short_ring = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  short_ring.faces[0].vertex_indices.resize(2);
  assert_defect(short_ring, mesh_defect_kind_t::Face_Has_Fewer_Than_Three_Vertices);

  mesh_geometry_t flat = shared::make_box_mesh({0.f, 0.f, 0.f}, {32.f, 32.f, 32.f});
  flat.vertices.push_back({0.f, 0.f, 0.f});
  flat.vertices.push_back({1.f, 0.f, 0.f});
  flat.vertices.push_back({2.f, 0.f, 0.f});
  flat.faces.push_back({{8, 9, 10}, {}});
  assert_defect(flat, mesh_defect_kind_t::Face_Is_Degenerate);
}

} // namespace

int main()
{
  test_a_box_is_a_closed_planar_mesh();
  test_an_l_shape_is_a_closed_planar_mesh();
  test_an_open_mesh_is_refused();
  test_a_bent_face_is_refused_and_the_epsilon_holds();
  test_a_face_wound_the_wrong_way_is_refused();
  test_structural_defects_are_refused();

  std::cout << "all mesh tests passed" << std::endl;
  return 0;
}
