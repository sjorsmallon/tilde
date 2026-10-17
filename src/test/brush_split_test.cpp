// A brush cut by a plane is two brushes whose volumes sum to the original.
// Every face of a half keeps the surface of the original face on its plane,
// the cap takes the surface it was given, and a plane crossing a subdivided
// face is refused. geometry_def.md Track G.

#include "shared/map/brush/brush.hpp"
#include "shared/map/brush/brush_split.hpp"
#include "shared/map/map.hpp"
#include "shared/map/map_geometry.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <optional>
#include <vector>

using shared::brush_geometry_t;
using shared::brush_polyhedron_t;
using shared::brush_split_t;
using shared::face_surface_t;

namespace
{

const Plane TOP{{0.f, 64.f, 0.f}, {0.f, 1.f, 0.f}};
const Plane LEFT{{-64.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}};
const Plane CUT_X{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}};
const uint16_t TOP_MATERIAL = 7;
const uint16_t CAP_MATERIAL = 3;

face_surface_t make_cap_surface()
{
  face_surface_t cap;
  cap.material = CAP_MATERIAL;
  return cap;
}

brush_geometry_t make_box_with_top_material()
{
  brush_geometry_t brush = shared::make_box_brush({0, 0, 0}, {64, 64, 64});
  shared::sync_face_surfaces(brush);
  shared::face_surface_for(brush, TOP).material = TOP_MATERIAL;
  return brush;
}

brush_polyhedron_t hull_of(const brush_geometry_t &brush)
{
  const std::optional<brush_polyhedron_t> hull =
      shared::try_build_brush_polyhedron(brush.hull_points);
  assert(hull);
  return *hull;
}

float compute_volume(const brush_polyhedron_t &hull)
{
  float six_volume = 0.f;
  for (const shared::brush_face_t &face : hull.faces)
  {
    const linalg::vec3 &origin = hull.vertices[face.vertex_indices[0]];
    for (size_t i = 1; i + 1 < face.vertex_indices.size(); ++i)
    {
      const linalg::vec3 &b = hull.vertices[face.vertex_indices[i]];
      const linalg::vec3 &c = hull.vertices[face.vertex_indices[i + 1]];
      six_volume += linalg::dot(origin, linalg::cross(b, c));
    }
  }
  return six_volume / 6.f;
}

bool nearly(float a, float b) { return std::abs(a - b) < 0.5f; }

const face_surface_t &surface_on(const brush_geometry_t &brush, const Plane &plane)
{
  const face_surface_t *surface = shared::find_face_surface(brush, plane);
  assert(surface);
  assert(linalg::dot(surface->key_normal, plane.normal) > 0.999f);
  assert(std::abs(surface->key_distance - plane_distance(plane)) < 0.05f);
  return *surface;
}

void test_an_axis_cut_makes_two_boxes_whose_volumes_sum()
{
  const brush_geometry_t box = make_box_with_top_material();
  const std::optional<brush_split_t> split =
      shared::try_split_brush_by_plane(box, CUT_X, make_cap_surface());
  assert(split);

  const brush_polyhedron_t front = hull_of(split->front);
  const brush_polyhedron_t back = hull_of(split->back);
  assert(front.vertices.size() == 8 && front.faces.size() == 6);
  assert(back.vertices.size() == 8 && back.faces.size() == 6);
  for (const linalg::vec3 &vertex : front.vertices)
    assert(vertex.x >= -0.01f);
  for (const linalg::vec3 &vertex : back.vertices)
    assert(vertex.x <= 0.01f);
  assert(nearly(compute_volume(front) + compute_volume(back), compute_volume(hull_of(box))));
  assert(nearly(compute_volume(front), compute_volume(back)));
}

void test_an_oblique_cut_makes_two_prisms_whose_volumes_sum()
{
  const brush_geometry_t box = make_box_with_top_material();
  const Plane diagonal{{0.f, 0.f, 0.f}, linalg::normalize(linalg::vec3{1.f, 1.f, 0.f})};
  const std::optional<brush_split_t> split =
      shared::try_split_brush_by_plane(box, diagonal, make_cap_surface());
  assert(split);

  const brush_polyhedron_t front = hull_of(split->front);
  const brush_polyhedron_t back = hull_of(split->back);
  assert(front.faces.size() == 5);
  assert(back.faces.size() == 5);
  assert(nearly(compute_volume(front) + compute_volume(back), compute_volume(hull_of(box))));
}

void test_a_face_keeps_its_surface_in_the_half_that_kept_its_plane()
{
  const brush_geometry_t box = make_box_with_top_material();
  const std::optional<brush_split_t> split =
      shared::try_split_brush_by_plane(box, CUT_X, make_cap_surface());
  assert(split);

  assert(split->front.face_surfaces.size() == 6);
  assert(split->back.face_surfaces.size() == 6);
  assert(surface_on(split->front, TOP).material == TOP_MATERIAL);
  assert(surface_on(split->back, TOP).material == TOP_MATERIAL);
  assert(surface_on(split->back, LEFT).material == 0);
  assert(surface_on(split->front, Plane{{0.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}}).material ==
         CAP_MATERIAL);
  assert(surface_on(split->back, Plane{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}}).material ==
         CAP_MATERIAL);
}

void test_a_cap_parallel_to_a_face_takes_the_cap_surface_not_that_face()
{
  const brush_geometry_t box = make_box_with_top_material();
  const Plane below_top{{0.f, 20.f, 0.f}, {0.f, 1.f, 0.f}};
  const std::optional<brush_split_t> split =
      shared::try_split_brush_by_plane(box, below_top, make_cap_surface());
  assert(split);

  assert(surface_on(split->front, TOP).material == TOP_MATERIAL);
  assert(surface_on(split->back, below_top).material == CAP_MATERIAL);
  assert(surface_on(split->front, Plane{{0.f, 20.f, 0.f}, {0.f, -1.f, 0.f}}).material ==
         CAP_MATERIAL);
}

void test_a_cut_through_a_subdivided_face_is_refused()
{
  brush_geometry_t box = make_box_with_top_material();
  shared::resize_face_grid(shared::face_surface_for(box, TOP), 4);
  assert(!shared::try_split_brush_by_plane(box, CUT_X, make_cap_surface()));
}

void test_a_subdivided_face_landing_whole_keeps_its_grid()
{
  brush_geometry_t slab = shared::make_box_brush({64, 0, 0}, {64, 64, 64});
  shared::sync_face_surfaces(slab);
  const Plane slab_left{{0.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}};
  face_surface_t &left = shared::face_surface_for(slab, slab_left);
  shared::resize_face_grid(left, 3);
  for (linalg::vec3 &offset : left.offsets)
    offset = {-5.f, 0.f, 0.f};

  const Plane middle{{64.f, 0.f, 0.f}, {1.f, 0.f, 0.f}};
  const std::optional<brush_split_t> split =
      shared::try_split_brush_by_plane(slab, middle, make_cap_surface());
  assert(split);

  const face_surface_t &kept = surface_on(split->back, slab_left);
  assert(kept.subdivision_level == 3);
  assert(kept.offsets.size() == (size_t)shared::face_grid_vertex_count(3));
  assert(kept.offsets[5].x == -5.f);
  assert(surface_on(split->back, middle).subdivision_level == 0);
  assert(surface_on(split->front, Plane{{128.f, 0.f, 0.f}, {1.f, 0.f, 0.f}}).subdivision_level ==
         0);
}

void test_a_plane_missing_the_brush_is_refused()
{
  const brush_geometry_t box = make_box_with_top_material();
  assert(!shared::try_split_brush_by_plane(box, Plane{{200.f, 0.f, 0.f}, {1.f, 0.f, 0.f}},
                                           make_cap_surface()));
  assert(!shared::try_split_brush_by_plane(box, TOP, make_cap_surface()));
}

} // namespace

int main()
{
  test_an_axis_cut_makes_two_boxes_whose_volumes_sum();
  test_an_oblique_cut_makes_two_prisms_whose_volumes_sum();
  test_a_face_keeps_its_surface_in_the_half_that_kept_its_plane();
  test_a_cap_parallel_to_a_face_takes_the_cap_surface_not_that_face();
  test_a_cut_through_a_subdivided_face_is_refused();
  test_a_subdivided_face_landing_whole_keeps_its_grid();
  test_a_plane_missing_the_brush_is_refused();
  std::cout << "brush_split_test passed" << std::endl;
  return 0;
}
