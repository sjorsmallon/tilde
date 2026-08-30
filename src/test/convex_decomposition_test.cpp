// Track C's guard: a brush may be any closed polyhedron, and what reaches the
// BVH is N convex pieces whose union is that brush. Three properties, from
// geometry_def.md §9 — every piece is convex, the union bounds match the source,
// and a brush that cannot be decomposed fails LOUDLY rather than quietly
// ceasing to collide.
//
// Nothing in the editor can author a concave brush yet (Track D's subdivided
// faces are the first producer), so the concave inputs here are built as
// brush_polyhedron_t values directly. That is the point of landing the machinery
// first: the day something produces one, this test already says what must happen
// to it.

#include "../shared/convex_decomposition.hpp"
#include "../shared/game_session.hpp"
#include "../shared/map.hpp"
#include "../shared/map_geometry.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{

using linalg::vec3;

// An extruded polygon, in the winding the caller gives it, swept along +z. The
// side faces come out counter-clockwise seen from outside when the footprint is
// counter-clockwise seen from +z.
shared::brush_polyhedron_t extrude_footprint(const std::vector<vec3> &footprint,
                                             float bottom, float top)
{
  shared::brush_polyhedron_t polyhedron;
  const uint32_t             count = (uint32_t)footprint.size();

  for (const vec3 &point : footprint)
    polyhedron.vertices.push_back({point.x, point.y, bottom});
  for (const vec3 &point : footprint)
    polyhedron.vertices.push_back({point.x, point.y, top});

  for (uint32_t index = 0; index < count; ++index)
  {
    const uint32_t next = (index + 1) % count;

    const vec3 edge   = polyhedron.vertices[next] - polyhedron.vertices[index];
    const vec3 normal = linalg::normalize(vec3{edge.y, -edge.x, 0.f});

    polyhedron.faces.push_back({Plane{polyhedron.vertices[index], normal},
                                {index, next, next + count, index + count}});
  }

  std::vector<uint32_t> bottom_loop;
  std::vector<uint32_t> top_loop;
  for (uint32_t index = 0; index < count; ++index)
  {
    bottom_loop.push_back(count - 1 - index);
    top_loop.push_back(count + index);
  }

  polyhedron.faces.push_back(
      {Plane{polyhedron.vertices[0], {0, 0, -1}}, std::move(bottom_loop)});
  polyhedron.faces.push_back(
      {Plane{polyhedron.vertices[count], {0, 0, 1}}, std::move(top_loop)});

  return polyhedron;
}

shared::aabb_bounds_t union_bounds(const std::vector<shared::brush_polyhedron_t> &pieces)
{
  shared::aabb_bounds_t bounds{{FLT_MAX, FLT_MAX, FLT_MAX},
                               {-FLT_MAX, -FLT_MAX, -FLT_MAX}};

  for (const shared::brush_polyhedron_t &piece : pieces)
  {
    const shared::aabb_bounds_t piece_bounds = shared::compute_brush_bounds(piece.vertices);
    bounds.min.x = std::min(bounds.min.x, piece_bounds.min.x);
    bounds.min.y = std::min(bounds.min.y, piece_bounds.min.y);
    bounds.min.z = std::min(bounds.min.z, piece_bounds.min.z);
    bounds.max.x = std::max(bounds.max.x, piece_bounds.max.x);
    bounds.max.y = std::max(bounds.max.y, piece_bounds.max.y);
    bounds.max.z = std::max(bounds.max.z, piece_bounds.max.z);
  }

  return bounds;
}

bool bounds_match(const shared::aabb_bounds_t &lhs, const shared::aabb_bounds_t &rhs,
                  float tolerance)
{
  return std::abs(lhs.min.x - rhs.min.x) <= tolerance &&
         std::abs(lhs.min.y - rhs.min.y) <= tolerance &&
         std::abs(lhs.min.z - rhs.min.z) <= tolerance &&
         std::abs(lhs.max.x - rhs.max.x) <= tolerance &&
         std::abs(lhs.max.y - rhs.max.y) <= tolerance &&
         std::abs(lhs.max.z - rhs.max.z) <= tolerance;
}

bool point_is_in_any_piece(const std::vector<shared::brush_polyhedron_t> &pieces,
                           const vec3 &point)
{
  for (const shared::brush_polyhedron_t &piece : pieces)
  {
    bool inside = true;
    for (const shared::brush_face_t &face : piece.faces)
    {
      if (linalg::dot(face.plane.normal, point - face.plane.point) > 0.01f)
      {
        inside = false;
        break;
      }
    }
    if (inside)
      return true;
  }

  return false;
}

// ---------------------------------------------------------------------------

void test_a_convex_brush_is_one_piece_unchanged()
{
  const std::vector<vec3> square = {{0, 0, 0}, {64, 0, 0}, {64, 64, 0}, {0, 64, 0}};
  const shared::brush_polyhedron_t box = extrude_footprint(square, 0.f, 32.f);

  assert(shared::polyhedron_is_convex(box));

  const std::optional<std::vector<shared::brush_polyhedron_t>> pieces =
      shared::try_decompose_into_convex_pieces(box);
  assert(pieces);
  assert(pieces->size() == 1);

  // Not merely "one piece": the SAME piece. Every map on disk today is convex,
  // so a decomposition that rebuilt them would move collision by a float
  // everywhere for no reason.
  assert((*pieces)[0].vertices.size() == box.vertices.size());
  assert((*pieces)[0].faces.size() == box.faces.size());

  std::cout << "test_a_convex_brush_is_one_piece_unchanged passed" << std::endl;
}

void test_an_l_shape_decomposes()
{
  // The reflex corner is at (32,32). Nothing convex contains both arms.
  const std::vector<vec3> l_footprint = {{0, 0, 0},   {96, 0, 0},  {96, 32, 0},
                                         {32, 32, 0}, {32, 96, 0}, {0, 96, 0}};
  const shared::brush_polyhedron_t l_shape = extrude_footprint(l_footprint, 0.f, 32.f);

  assert(!shared::polyhedron_is_convex(l_shape));

  const std::optional<std::vector<shared::brush_polyhedron_t>> pieces =
      shared::try_decompose_into_convex_pieces(l_shape);
  assert(pieces);
  // The exact count, not a bound. A decomposition that still produced convex
  // pieces covering the same solid but twelve of them would pass every other
  // assertion here and quietly cost the BVH an order of magnitude; the split
  // heuristic is the thing worth pinning.
  assert(pieces->size() == 2);

  for (const shared::brush_polyhedron_t &piece : *pieces)
  {
    assert(shared::polyhedron_is_convex(piece));
    assert(piece.faces.size() >= 4);
    assert(piece.vertices.size() >= 4);
  }

  assert(bounds_match(union_bounds(*pieces), shared::compute_brush_bounds(l_shape.vertices),
                      0.5f));

  // Both arms are covered, and the notch is NOT — which is the whole reason a
  // hull is the wrong answer: it would fill (64,64) with a wall nothing draws.
  assert(point_is_in_any_piece(*pieces, {80.f, 16.f, 16.f}));
  assert(point_is_in_any_piece(*pieces, {16.f, 80.f, 16.f}));
  assert(point_is_in_any_piece(*pieces, {16.f, 16.f, 16.f}));
  assert(!point_is_in_any_piece(*pieces, {64.f, 64.f, 16.f}));

  std::cout << "test_an_l_shape_decomposes passed" << std::endl;
}

void test_a_u_shape_decomposes()
{
  // Two reflex corners and a notch open on one side: the case where a single
  // split does not finish the job.
  const std::vector<vec3> u_footprint = {{0, 0, 0},   {96, 0, 0},  {96, 96, 0},
                                         {64, 96, 0}, {64, 32, 0}, {32, 32, 0},
                                         {32, 96, 0}, {0, 96, 0}};
  const shared::brush_polyhedron_t u_shape = extrude_footprint(u_footprint, 0.f, 32.f);

  assert(!shared::polyhedron_is_convex(u_shape));

  const std::optional<std::vector<shared::brush_polyhedron_t>> pieces =
      shared::try_decompose_into_convex_pieces(u_shape);
  assert(pieces);
  assert(pieces->size() == 3);

  for (const shared::brush_polyhedron_t &piece : *pieces)
    assert(shared::polyhedron_is_convex(piece));

  assert(bounds_match(union_bounds(*pieces), shared::compute_brush_bounds(u_shape.vertices),
                      0.5f));

  assert(point_is_in_any_piece(*pieces, {16.f, 64.f, 16.f}));  // left prong
  assert(point_is_in_any_piece(*pieces, {80.f, 64.f, 16.f}));  // right prong
  assert(point_is_in_any_piece(*pieces, {48.f, 16.f, 16.f}));  // the base
  assert(!point_is_in_any_piece(*pieces, {48.f, 64.f, 16.f})); // the gap

  std::cout << "test_a_u_shape_decomposes passed" << std::endl;
}

void test_a_degenerate_polyhedron_fails_rather_than_approximating()
{
  // Not a solid at all. The contract is an empty optional, never a plausible
  // piece the runtime would go on to collide against.
  shared::brush_polyhedron_t flat;
  flat.vertices = {{0, 0, 0}, {64, 0, 0}, {64, 64, 0}, {0, 64, 0}};
  flat.faces.push_back({Plane{flat.vertices[0], {0, 0, 1}}, {0, 1, 2, 3}});

  assert(!shared::try_decompose_into_convex_pieces(flat));

  std::cout << "test_a_degenerate_polyhedron_fails_rather_than_approximating passed"
            << std::endl;
}

// ---------------------------------------------------------------------------

void test_one_brush_becomes_n_bvh_leaves_under_one_id()
{
  shared::map_t map;
  map.name = "decomposition";

  const shared::entity_uid_t uid = map.add_geometry(
      shared::make_box_brush({0.f, 0.f, 0.f}, {64.f, 16.f, 64.f}));

  const shared::game_session_t session = shared::build_session(map);
  assert(session.geometry.size() == 1);

  // A convex brush is still exactly one leaf, so nothing on disk pays for this.
  assert(session.bvh.primitives.size() == 1);
  assert(session.bvh.primitives[0].id.type == Collision_Id::Type::Static_Geometry);
  assert(session.bvh.primitives[0].id.index == 0);
  assert(session.bvh.primitives[0].collision_planes.size() ==
         session.bvh.primitives[0].face_polygons.size());

  const shared::map_geometry_t *entry = map.find_geometry_by_uid(uid);
  assert(entry != nullptr);

  const std::vector<shared::collision_piece_t> pieces =
      shared::get_collision_pieces(entry->value, entry->uid);
  assert(pieces.size() == 1);
  assert(pieces[0].planes.size() == 6);

  std::cout << "test_one_brush_becomes_n_bvh_leaves_under_one_id passed" << std::endl;
}

} // namespace

int main()
{
  test_a_convex_brush_is_one_piece_unchanged();
  test_an_l_shape_decomposes();
  test_a_u_shape_decomposes();
  test_a_degenerate_polyhedron_fails_rather_than_approximating();
  test_one_brush_becomes_n_bvh_leaves_under_one_id();

  std::cout << "convex_decomposition_test passed" << std::endl;
  return 0;
}
