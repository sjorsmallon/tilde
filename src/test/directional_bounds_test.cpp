// The directional bound of an octahedron IS the octahedron: its eight faces are the corner-diagonal
// planes, and the axis and edge planes the bound also makes are redundant and contribute no face. The
// box over the same points has every corner empty, which is the whole reason for the bound.
#include "geometry/convex_decomposition.hpp"
#include "geometry/convex_hull.hpp"
#include "geometry/directional_bounds.hpp"

#include <iostream>
#include <optional>
#include <vector>

namespace
{

int failures = 0;

void check(bool condition, const char* what)
{
  if (!condition)
  {
    std::cout << "FAIL: " << what << "\n";
    ++failures;
  }
}

bool inside_every_plane(Span<const Plane> planes, const linalg::vec3& point)
{
  for (const Plane& plane : planes)
    if (linalg::dot(point - plane.point, plane.normal) > 1e-4f)
      return false;
  return true;
}

} // namespace

int main()
{
  const std::vector<linalg::vec3> octahedron = {{1.f, 0.f, 0.f},  {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
                                                {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f},  {0.f, 0.f, -1.f}};
  const Array<Plane, shared::DIRECTIONAL_BOUND_PLANES> bound = shared::compute_directional_bound_planes(octahedron);
  std::vector<Plane> planes;
  for (uint32_t index = 0; index < bound.size(); ++index)
    planes.push_back(bound[index]);
  check(planes.size() == 26, "thirteen directions give twenty-six planes");

  for (const linalg::vec3& corner : octahedron)
    check(inside_every_plane(planes, corner), "every point is inside its own bound");
  check(inside_every_plane(planes, {0.3f, 0.3f, 0.3f}), "the centre's neighbourhood is inside");
  check(!inside_every_plane(planes, {0.9f, 0.9f, 0.9f}), "a box corner the points leave empty is outside the bound");
  check(!inside_every_plane(planes, {0.9f, 0.9f, 0.f}), "a box edge the points leave empty is outside the bound");

  const std::optional<shared::convex_polyhedron_t> body =
      shared::try_build_convex_from_planes(planes, {0.f, 0.f, 0.f}, 4.f);
  check(body.has_value(), "the planes close a solid");
  if (body)
  {
    check(body->vertices.size() == 6, "the bound of an octahedron has its six corners");
    check(body->faces.size() == 8, "the bound of an octahedron has its eight faces: the redundant planes contribute none");
  }

  const std::vector<linalg::vec3> single = {{2.f, 3.f, 4.f}};
  const Array<Plane, shared::DIRECTIONAL_BOUND_PLANES> point_bound = shared::compute_directional_bound_planes(single);
  check(linalg::dot(point_bound[0].point, point_bound[0].normal) == 2.f &&
            linalg::dot(point_bound[1].point, point_bound[1].normal) == -2.f,
        "a direction's two planes are the furthest and the nearest point along it");

  std::cout << (failures == 0 ? "directional_bounds_test: ok\n" : "directional_bounds_test: FAILED\n");
  return failures == 0 ? 0 : 1;
}
