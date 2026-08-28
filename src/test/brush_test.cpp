// A brush is the convex hull of its points, so almost everything worth checking
// here is a property of that hull rather than of a particular shape: normals
// point out, loops wind counter-clockwise seen from outside, coplanar points
// make ONE face instead of a fan of triangles, and every vertex is behind every
// plane.
//
// The near-coplanar cases matter most. On grid points a vertex is either exactly
// on a face or clearly off it, and BRUSH_COPLANAR_EPSILON never has to decide
// anything. The moment vertices can move freely between grid points it decides
// constantly -- too tight shatters a face into slivers, too loose welds two
// distinct faces into one -- so it gets a guard before anything leans on it.

#include "shared/brush.hpp"
#include "shared/game_session.hpp"
#include "shared/map.hpp"
#include "shared/map_geometry.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

using shared::brush_face_t;
using shared::brush_polyhedron_t;

namespace
{

// Area-weighted normal of a polygon, following the right-hand rule of its
// winding. Agreeing with the face plane normal is what "counter-clockwise seen
// from outside" means.
linalg::vec3 polygon_winding_normal(const std::vector<linalg::vec3> &polygon)
{
  linalg::vec3 normal{0, 0, 0};
  for (size_t i = 0; i < polygon.size(); ++i)
    normal = normal + linalg::cross(polygon[i], polygon[(i + 1) % polygon.size()]);
  return normal;
}

const brush_face_t *find_face_with_normal(const brush_polyhedron_t &polyhedron,
                                          const linalg::vec3 &normal)
{
  for (const brush_face_t &face : polyhedron.faces)
  {
    if (linalg::dot(face.plane.normal, normal) > 0.999f)
      return &face;
  }
  return nullptr;
}

void assert_is_closed_convex_solid(const brush_polyhedron_t &polyhedron)
{
  assert(polyhedron.faces.size() >= 4);

  for (const brush_face_t &face : polyhedron.faces)
  {
    assert(face.vertex_indices.size() >= 3);

    const float distance = linalg::dot(face.plane.normal, face.plane.point);

    // Outward: every vertex of the solid is behind the plane.
    for (const linalg::vec3 &vertex : polyhedron.vertices)
      assert(linalg::dot(face.plane.normal, vertex) - distance <= shared::BRUSH_COPLANAR_EPSILON);

    // Wound counter-clockwise seen from outside.
    std::vector<linalg::vec3> polygon;
    for (uint32_t index : face.vertex_indices)
      polygon.push_back(polyhedron.vertices[index]);
    assert(linalg::dot(polygon_winding_normal(polygon), face.plane.normal) > 0.0f);

    // Every vertex of the loop actually lies on the plane.
    for (const linalg::vec3 &vertex : polygon)
      assert(std::abs(linalg::dot(face.plane.normal, vertex) - distance) <=
             shared::BRUSH_COPLANAR_EPSILON);
  }

  // Every surviving vertex is a real CORNER, on three faces or more. A point on
  // one face is interior to it and threads a self-intersection into that loop;
  // a point on two is mid-edge. Both must have been dropped.
  for (const linalg::vec3 &vertex : polyhedron.vertices)
  {
    size_t faces_touching = 0;
    for (const brush_face_t &face : polyhedron.faces)
    {
      const float distance = linalg::dot(face.plane.normal, face.plane.point);
      if (std::abs(linalg::dot(face.plane.normal, vertex) - distance) <=
          shared::BRUSH_COPLANAR_EPSILON)
        ++faces_touching;
    }
    assert(faces_touching >= 3);
  }
}

void test_cube_hulls_into_six_quads()
{
  const std::vector<linalg::vec3> corners =
      shared::make_box_brush_vertices({0, 0, 0}, {64, 64, 64});

  std::optional<brush_polyhedron_t> polyhedron = shared::try_build_brush_polyhedron(corners);
  assert(polyhedron.has_value());

  assert(polyhedron->vertices.size() == 8);

  // Six QUADS, not twelve triangles. This is the property the plane-enumeration
  // hull was chosen for: coplanar points join one face natively.
  assert(polyhedron->faces.size() == 6);
  for (const brush_face_t &face : polyhedron->faces)
    assert(face.vertex_indices.size() == 4);

  assert_is_closed_convex_solid(*polyhedron);

  const brush_face_t *top = find_face_with_normal(*polyhedron, {0, 1, 0});
  assert(top != nullptr);
  for (uint32_t index : top->vertex_indices)
    assert(std::abs(polyhedron->vertices[index].y - 64.0f) < 1e-4f);

  std::cout << "test_cube_hulls_into_six_quads passed" << std::endl;
}

void test_degenerate_point_sets_are_rejected()
{
  const std::vector<linalg::vec3> three = {{0, 0, 0}, {64, 0, 0}, {0, 64, 0}};
  assert(!shared::try_build_brush_polyhedron(three).has_value());

  // Four points, but flat: a quad is not a solid.
  const std::vector<linalg::vec3> coplanar = {
      {0, 0, 0}, {64, 0, 0}, {64, 0, 64}, {0, 0, 64}};
  assert(!shared::try_build_brush_polyhedron(coplanar).has_value());

  // Collinear.
  const std::vector<linalg::vec3> collinear = {
      {0, 0, 0}, {16, 0, 0}, {32, 0, 0}, {48, 0, 0}};
  assert(!shared::try_build_brush_polyhedron(collinear).has_value());

  // Eight points that weld down to two: below the four-point floor.
  std::vector<linalg::vec3> welds_to_two;
  for (int repeat = 0; repeat < 4; ++repeat)
  {
    welds_to_two.push_back({0, 0, 0});
    welds_to_two.push_back({64, 0, 0});
  }
  assert(!shared::try_build_brush_polyhedron(welds_to_two).has_value());

  std::cout << "test_degenerate_point_sets_are_rejected passed" << std::endl;
}

void test_welding_a_cube_edge_down_makes_a_wedge()
{
  // Both +z top corners dropped onto the bottom plane, where they land exactly on
  // the +z bottom corners. Six survivors, and the hull of six is a wedge -- this
  // is the whole "ramps are free" claim, so it is worth pinning.
  std::vector<linalg::vec3> points =
      shared::make_box_brush_vertices({0, 0, 0}, {64, 64, 64});

  for (linalg::vec3 &point : points)
  {
    if (point.y > 0.0f && point.z > 0.0f)
      point.y = -64.0f;
  }

  std::optional<brush_polyhedron_t> polyhedron = shared::try_build_brush_polyhedron(points);
  assert(polyhedron.has_value());

  assert(polyhedron->vertices.size() == 6);

  // Triangular prism: two triangle ends and three quads.
  assert(polyhedron->faces.size() == 5);

  int triangle_count = 0;
  int quad_count     = 0;
  for (const brush_face_t &face : polyhedron->faces)
  {
    if (face.vertex_indices.size() == 3)
      ++triangle_count;
    if (face.vertex_indices.size() == 4)
      ++quad_count;
  }
  assert(triangle_count == 2);
  assert(quad_count == 3);

  assert_is_closed_convex_solid(*polyhedron);

  // The slope exists: some face normal is neither axis-aligned nor vertical.
  bool found_slope = false;
  for (const brush_face_t &face : polyhedron->faces)
  {
    const linalg::vec3 &normal = face.plane.normal;
    if (std::abs(normal.y) > 0.1f && std::abs(normal.y) < 0.9f)
      found_slope = true;
  }
  assert(found_slope);

  std::cout << "test_welding_a_cube_edge_down_makes_a_wedge passed" << std::endl;
}

void test_near_coplanar_face_holds_together_then_splits()
{
  // Inside the epsilon: the top stays ONE quad, six faces total.
  {
    std::vector<linalg::vec3> points =
        shared::make_box_brush_vertices({0, 0, 0}, {64, 64, 64});
    points[3].y += 0.01f; // some top corner, lifted a hair

    std::optional<brush_polyhedron_t> polyhedron = shared::try_build_brush_polyhedron(points);
    assert(polyhedron.has_value());
    assert(polyhedron->faces.size() == 6);
    assert_is_closed_convex_solid(*polyhedron);
  }

  // Outside it: the top is genuinely non-planar and becomes two triangles.
  {
    std::vector<linalg::vec3> points =
        shared::make_box_brush_vertices({0, 0, 0}, {64, 64, 64});
    points[3].y += 8.0f;

    std::optional<brush_polyhedron_t> polyhedron = shared::try_build_brush_polyhedron(points);
    assert(polyhedron.has_value());
    assert(polyhedron->faces.size() == 7);
    assert_is_closed_convex_solid(*polyhedron);
  }

  std::cout << "test_near_coplanar_face_holds_together_then_splits passed" << std::endl;
}

void test_off_grid_vertices_hull_the_same_as_on_grid_ones()
{
  // No tolerance in the hull reads the editor grid step, so a brush nudged onto
  // irrational coordinates must produce the same topology. This is the guard on
  // rule 2 in brush.hpp -- free vertex movement depends on it.
  std::vector<linalg::vec3> points =
      shared::make_box_brush_vertices({0, 0, 0}, {64, 64, 64});

  for (linalg::vec3 &point : points)
  {
    point = point * 0.9137f;
    point.x += 0.137f;
    point.y += 12.9931f;
    point.z += -7.71f;
  }

  std::optional<brush_polyhedron_t> polyhedron = shared::try_build_brush_polyhedron(points);
  assert(polyhedron.has_value());
  assert(polyhedron->vertices.size() == 8);
  assert(polyhedron->faces.size() == 6);
  for (const brush_face_t &face : polyhedron->faces)
    assert(face.vertex_indices.size() == 4);

  assert_is_closed_convex_solid(*polyhedron);

  std::cout << "test_off_grid_vertices_hull_the_same_as_on_grid_ones passed" << std::endl;
}

void test_extruding_a_concave_footprint_yields_its_hull()
{
  // An L, reflex at (64, 0, 64). The documented behaviour is that the extrusion
  // is the HULL of the picked points -- which is exactly why it stays one convex
  // brush instead of having to split. Asserting the hull, not the L.
  const std::vector<linalg::vec3> footprint = {
      {0, 0, 0}, {128, 0, 0}, {128, 0, 64}, {64, 0, 64}, {64, 0, 128}, {0, 0, 128}};

  const std::vector<linalg::vec3> extruded =
      shared::extrude_brush_hull(footprint, {0, 1, 0}, 64.0f);
  assert(extruded.size() == 12);

  std::optional<brush_polyhedron_t> polyhedron = shared::try_build_brush_polyhedron(extruded);
  assert(polyhedron.has_value());
  assert_is_closed_convex_solid(*polyhedron);

  // The reflex point is swallowed: the bottom face is the 5-point convex hull of
  // the footprint, not the 6-point L.
  const brush_face_t *bottom = find_face_with_normal(*polyhedron, {0, -1, 0});
  assert(bottom != nullptr);
  assert(bottom->vertex_indices.size() == 5);

  const brush_face_t *top = find_face_with_normal(*polyhedron, {0, 1, 0});
  assert(top != nullptr);
  assert(top->vertex_indices.size() == 5);

  std::cout << "test_extruding_a_concave_footprint_yields_its_hull passed" << std::endl;
}

void test_collision_planes_match_the_hull()
{
  const std::vector<linalg::vec3> corners =
      shared::make_box_brush_vertices({32, 16, -48}, {64, 32, 16});

  std::optional<brush_polyhedron_t> polyhedron = shared::try_build_brush_polyhedron(corners);
  assert(polyhedron.has_value());

  const std::vector<Plane> planes = shared::brush_collision_planes(*polyhedron);
  assert(planes.size() == polyhedron->faces.size());

  for (const Plane &plane : planes)
  {
    const float distance = linalg::dot(plane.normal, plane.point);
    for (const linalg::vec3 &vertex : polyhedron->vertices)
      assert(linalg::dot(plane.normal, vertex) - distance <= shared::BRUSH_COPLANAR_EPSILON);
  }

  const std::vector<std::vector<linalg::vec3>> polygons =
      shared::brush_face_polygons(*polyhedron);
  assert(polygons.size() == planes.size());
  for (size_t i = 0; i < polygons.size(); ++i)
    assert(linalg::dot(polygon_winding_normal(polygons[i]), planes[i].normal) > 0.0f);

  const shared::aabb_bounds_t bounds = shared::compute_brush_bounds(corners);
  assert(std::abs(bounds.min.x - (-32.0f)) < 1e-4f);
  assert(std::abs(bounds.max.x - 96.0f) < 1e-4f);
  assert(std::abs(bounds.min.z - (-64.0f)) < 1e-4f);
  assert(std::abs(bounds.max.z - (-32.0f)) < 1e-4f);

  std::cout << "test_collision_planes_match_the_hull passed" << std::endl;
}

void test_mesh_covers_every_face()
{
  const std::vector<linalg::vec3> corners =
      shared::make_box_brush_vertices({0, 0, 0}, {64, 64, 64});

  std::optional<brush_polyhedron_t> polyhedron = shared::try_build_brush_polyhedron(corners);
  assert(polyhedron.has_value());

  const assets::mesh_asset_t mesh = shared::generate_brush_mesh(*polyhedron);

  // Six quads: 24 vertices (per-face, so corners are not shared) and 12 triangles.
  assert(mesh.vertices.size() == 24);
  assert(mesh.indices.size() == 36);

  for (uint32_t index : mesh.indices)
    assert(index < mesh.vertices.size());

  // Every triangle wound so its geometric normal agrees with its shading normal.
  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
  {
    const linalg::vec3 a = mesh.vertices[mesh.indices[i]].position;
    const linalg::vec3 b = mesh.vertices[mesh.indices[i + 1]].position;
    const linalg::vec3 c = mesh.vertices[mesh.indices[i + 2]].position;
    const linalg::vec3 geometric = linalg::cross(b - a, c - a);
    assert(linalg::dot(geometric, mesh.vertices[mesh.indices[i]].normal) > 0.0f);
  }

  std::cout << "test_mesh_covers_every_face passed" << std::endl;
}

void test_text_round_trip_is_bit_exact_and_canonical()
{
  // geometry_values_equal is bit-exact, so a lossy round trip would make a
  // reloaded brush compare unequal to itself and produce phantom undo entries.
  std::vector<linalg::vec3> vertices =
      shared::make_box_brush_vertices({0, 0, 0}, {64, 64, 64});
  vertices[0].x = 1.0f / 3.0f;
  vertices[1].y = -0.1f;
  vertices[2].z = 12345.6789f;

  const std::string text = shared::brush_vertices_to_text(vertices);

  std::optional<std::vector<linalg::vec3>> parsed = shared::try_brush_vertices_from_text(text);
  assert(parsed.has_value());
  assert(parsed->size() == vertices.size());

  // Written sorted, so compare against the sorted spelling rather than input order.
  const std::string reparsed_text = shared::brush_vertices_to_text(*parsed);
  assert(reparsed_text == text);

  for (const linalg::vec3 &vertex : *parsed)
  {
    bool found_exact = false;
    for (const linalg::vec3 &original : vertices)
    {
      if (vertex.x == original.x && vertex.y == original.y && vertex.z == original.z)
        found_exact = true;
    }
    assert(found_exact);
  }

  // One shape, one spelling: edit order must not change the file.
  std::vector<linalg::vec3> reversed(vertices.rbegin(), vertices.rend());
  assert(shared::brush_vertices_to_text(reversed) == text);

  assert(!shared::try_brush_vertices_from_text("").has_value());
  assert(!shared::try_brush_vertices_from_text("0 0 0, 1 1 1").has_value());
  assert(!shared::try_brush_vertices_from_text("0 0 0, 1 1, 2 2 2, 3 3 3").has_value());

  std::cout << "test_text_round_trip_is_bit_exact_and_canonical passed" << std::endl;
}

void test_vertex_cap_is_rejected_not_truncated()
{
  std::vector<linalg::vec3> points;
  for (uint32_t i = 0; i <= shared::MAX_BRUSH_VERTICES; ++i)
  {
    const float angle = (float)i * 0.37f;
    points.push_back({std::cos(angle) * 256.0f, (float)i * 8.0f, std::sin(angle) * 256.0f});
  }
  assert(points.size() == shared::MAX_BRUSH_VERTICES + 1);

  assert(!shared::try_build_brush_polyhedron(points).has_value());

  points.pop_back();
  assert(shared::try_build_brush_polyhedron(points).has_value());

  std::cout << "test_vertex_cap_is_rejected_not_truncated passed" << std::endl;
}

void test_grid_snap_is_an_operation_not_an_invariant()
{
  std::vector<linalg::vec3> vertices = {{1.4f, -13.7f, 100.2f}, {17.0f, 0.0f, -9.9f}};

  shared::snap_brush_vertices_to_grid(vertices, 8.0f);
  assert(vertices[0].x == 0.0f);
  assert(vertices[0].y == -16.0f);
  assert(vertices[0].z == 104.0f);
  assert(vertices[1].x == 16.0f);
  assert(vertices[1].z == -8.0f);

  // A zero or negative step is not a snap of size zero, it is no snap.
  std::vector<linalg::vec3> untouched = {{1.4f, -3.7f, 100.2f}};
  shared::snap_brush_vertices_to_grid(untouched, 0.0f);
  assert(untouched[0].x == 1.4f);

  std::cout << "test_grid_snap_is_an_operation_not_an_invariant passed" << std::endl;
}

void test_geometry_block_round_trip()
{
  shared::brush_geometry_t brush;
  brush.vertices        = shared::make_box_brush_vertices({32, 0, -16}, {64, 32, 96});
  brush.vertices[2].y  += 24.0f; // a corner pulled off the box, so it is not a cube
  brush.surface.color   = {0.25f, 0.5f, 0.75f};
  brush.surface.mesh_path = "";

  const shared::geometry_value_t original = brush;

  std::string                                      keyword;
  std::vector<std::pair<std::string, std::string>> properties;
  shared::serialize_geometry(original, keyword, properties);
  assert(keyword == "brush");

  std::map<std::string, std::string> property_map;
  for (const std::pair<std::string, std::string> &property : properties)
    property_map[property.first] = property.second;
  assert(property_map.count("vertices") == 1);

  shared::geometry_value_t reloaded;
  assert(shared::parse_geometry(keyword, property_map, reloaded));

  // Bit-exact, which is what the value-swap undo flavour is built on.
  assert(shared::geometry_values_equal(original, reloaded));
  assert(shared::get_kind(reloaded) == shared::geometry_kind_t::Brush);

  // The seam the editor drives every kind through.
  assert(shared::get_collision_planes(reloaded).size() ==
         shared::get_face_polygons(reloaded).size());
  assert(shared::get_collision_planes(reloaded).size() >= 4);

  // A brush that is not a solid is refused at load rather than becoming an
  // object with no faces and no collision.
  std::map<std::string, std::string> flat;
  flat["vertices"] = "0 0 0, 64 0 0, 64 0 64, 0 0 64";
  shared::geometry_value_t rejected;
  assert(!shared::parse_geometry("brush", flat, rejected));

  std::map<std::string, std::string> missing;
  assert(!shared::parse_geometry("brush", missing, rejected));

  std::cout << "test_geometry_block_round_trip passed" << std::endl;
}

void test_map_round_trip_and_session_collision()
{
  // What the editor actually does: two brushes into a map, out to canonical
  // text, back in, then into a session so the BVH sees them.
  shared::map_t map;

  shared::brush_geometry_t box_brush;
  box_brush.vertices = shared::make_box_brush_vertices({0, 0, 0}, {64, 64, 64});
  const shared::entity_uid_t box_uid = map.add_geometry(box_brush);

  // A wedge, so the map carries a brush that is NOT box-shaped and the
  // difference between hull collision and bound collision is observable.
  shared::brush_geometry_t wedge;
  wedge.vertices = shared::make_box_brush_vertices({512, 0, 0}, {64, 64, 64});
  for (linalg::vec3 &vertex : wedge.vertices)
  {
    if (vertex.y > 0.0f && vertex.z > 0.0f)
      vertex.y = -64.0f;
  }
  const shared::entity_uid_t wedge_uid = map.add_geometry(wedge);

  const std::string text     = shared::serialize_map_to_string(map);
  const shared::map_t reloaded = shared::parse_map_from_string(text);

  assert(reloaded.geometry.size() == 2);
  assert(shared::serialize_map_to_string(reloaded) == text); // canonical, and stable

  const shared::map_geometry_t *reloaded_box = reloaded.find_geometry_by_uid(box_uid);
  const shared::map_geometry_t *reloaded_wedge = reloaded.find_geometry_by_uid(wedge_uid);
  assert(reloaded_box != nullptr);
  assert(reloaded_wedge != nullptr);
  assert(shared::geometry_values_equal(reloaded_box->value, shared::geometry_value_t{box_brush}));
  assert(shared::geometry_values_equal(reloaded_wedge->value, shared::geometry_value_t{wedge}));

  // The wedge collides as its hull, not its bound: five planes, one of them a
  // slope. A box-shaped collision would report six axis-aligned ones.
  const std::vector<Plane> wedge_planes =
      shared::get_collision_planes(reloaded_wedge->value);
  assert(wedge_planes.size() == 5);

  bool found_sloped_plane = false;
  for (const Plane &plane : wedge_planes)
  {
    const int axis_aligned = (std::abs(plane.normal.x) > 0.99f ? 1 : 0) +
                             (std::abs(plane.normal.y) > 0.99f ? 1 : 0) +
                             (std::abs(plane.normal.z) > 0.99f ? 1 : 0);
    if (axis_aligned == 0)
      found_sloped_plane = true;
  }
  assert(found_sloped_plane);

  const shared::game_session_t session = shared::build_session(reloaded);
  assert(session.geometry.size() == 2);
  assert(session.bvh.primitives.size() == 2);

  // Whatever order the BVH put them in, both leaves carry hull planes paired
  // with their polygons -- what player_move slides against.
  for (const BVH_Primitive &primitive : session.bvh.primitives)
  {
    assert(!primitive.collision_planes.empty());
    assert(primitive.collision_planes.size() == primitive.face_polygons.size());
  }

  std::cout << "test_map_round_trip_and_session_collision passed" << std::endl;
}

shared::aabb_bounds_t bounds_of(const std::vector<linalg::vec3> &points)
{
  return shared::compute_brush_bounds(points);
}

bool bounds_match(const shared::aabb_bounds_t &bounds, const linalg::vec3 &min,
                  const linalg::vec3 &max)
{
  return std::abs(bounds.min.x - min.x) < 1e-3f && std::abs(bounds.min.y - min.y) < 1e-3f &&
         std::abs(bounds.min.z - min.z) < 1e-3f && std::abs(bounds.max.x - max.x) < 1e-3f &&
         std::abs(bounds.max.y - max.y) < 1e-3f && std::abs(bounds.max.z - max.z) < 1e-3f;
}

void test_filled_grid_cells_split_a_concave_footprint()
{
  // The same L as the hull test above, but PICKED rather than outlined: every
  // grid point the two rubber-band drags cover, which is eight of the nine on
  // a 3x3 grid. Read as cells that has exactly one meaning, so it comes back
  // as two rectangles instead of a hull that swallows the reflex corner.
  const float               step = 64.0f;
  std::vector<linalg::vec3> footprint;
  for (int iz = 0; iz <= 2; ++iz)
  {
    for (int ix = 0; ix <= 2; ++ix)
    {
      if (ix == 2 && iz == 2)
        continue; // the corner the L is missing
      footprint.push_back({(float)ix * step, 0.0f, (float)iz * step});
    }
  }
  assert(footprint.size() == 8);

  std::optional<std::vector<std::vector<linalg::vec3>>> rectangles =
      shared::try_decompose_footprint_into_rectangles(footprint, {0, 1, 0}, step);
  assert(rectangles.has_value());
  assert(rectangles->size() == 2);

  for (const std::vector<linalg::vec3> &rectangle : *rectangles)
    assert(rectangle.size() == 4);

  // Wide piece first, tall piece second: the sweep is row-major.
  assert(bounds_match(bounds_of((*rectangles)[0]), {0, 0, 0}, {128, 0, 64}));
  assert(bounds_match(bounds_of((*rectangles)[1]), {0, 0, 64}, {64, 0, 128}));

  // Each piece extrudes into a solid of its own, and the three cells of the L
  // are covered exactly once between them.
  float total_volume = 0.0f;
  for (const std::vector<linalg::vec3> &rectangle : *rectangles)
  {
    const std::vector<linalg::vec3> extruded =
        shared::extrude_brush_hull(rectangle, {0, 1, 0}, 64.0f);
    std::optional<brush_polyhedron_t> solid = shared::try_build_brush_polyhedron(extruded);
    assert(solid.has_value());
    assert(solid->faces.size() == 6);
    assert_is_closed_convex_solid(*solid);

    const shared::aabb_bounds_t bounds = shared::compute_brush_bounds(solid->vertices);
    const linalg::vec3          size   = bounds.max - bounds.min;
    total_volume += size.x * size.y * size.z;
  }
  assert(std::abs(total_volume - 3.0f * 64.0f * 64.0f * 64.0f) < 1.0f);

  std::cout << "test_filled_grid_cells_split_a_concave_footprint passed" << std::endl;
}

void test_a_full_rectangle_stays_one_brush()
{
  // The ordinary case has to come out exactly as it did before: one rectangle,
  // extruding to the same box the hull would have produced.
  const float               step = 64.0f;
  std::vector<linalg::vec3> footprint;
  for (int iz = 0; iz <= 2; ++iz)
    for (int ix = 0; ix <= 2; ++ix)
      footprint.push_back({(float)ix * step, 0.0f, (float)iz * step});

  std::optional<std::vector<std::vector<linalg::vec3>>> rectangles =
      shared::try_decompose_footprint_into_rectangles(footprint, {0, 1, 0}, step);
  assert(rectangles.has_value());
  assert(rectangles->size() == 1);
  assert(bounds_match(bounds_of((*rectangles)[0]), {0, 0, 0}, {128, 0, 128}));

  const std::vector<linalg::vec3> from_cells =
      shared::extrude_brush_hull((*rectangles)[0], {0, 1, 0}, 64.0f);
  const std::vector<linalg::vec3> from_hull =
      shared::extrude_brush_hull(footprint, {0, 1, 0}, 64.0f);

  std::optional<brush_polyhedron_t> cell_solid = shared::try_build_brush_polyhedron(from_cells);
  std::optional<brush_polyhedron_t> hull_solid = shared::try_build_brush_polyhedron(from_hull);
  assert(cell_solid.has_value() && hull_solid.has_value());
  assert(cell_solid->vertices.size() == 8);
  assert(hull_solid->vertices.size() == 8);
  assert(shared::brush_vertices_to_text(cell_solid->vertices) ==
         shared::brush_vertices_to_text(hull_solid->vertices));

  std::cout << "test_a_full_rectangle_stays_one_brush passed" << std::endl;
}

void test_footprints_the_cell_reading_cannot_account_for()
{
  const float step = 64.0f;

  // An OUTLINE of an L, which is what the hull test above feeds. No cell has all
  // four corners, so this falls back rather than inventing a shape.
  const std::vector<linalg::vec3> outline = {
      {0, 0, 0}, {128, 0, 0}, {128, 0, 64}, {64, 0, 64}, {64, 0, 128}, {0, 0, 128}};
  assert(!shared::try_decompose_footprint_into_rectangles(outline, {0, 1, 0}, step));

  // A filled square with one point nudged off the grid -- a real face corner
  // looks like this, and it belongs to no cell.
  std::vector<linalg::vec3> off_grid = {{0, 0, 0}, {64, 0, 0}, {0, 0, 64}, {64, 0, 64}};
  off_grid.push_back({97, 0, 33});
  assert(!shared::try_decompose_footprint_into_rectangles(off_grid, {0, 1, 0}, step));

  // A filled square plus a stray on-grid pick no cell covers. Dropping that
  // silently is the failure this whole path exists to avoid.
  std::vector<linalg::vec3> stray = {{0, 0, 0}, {64, 0, 0}, {0, 0, 64}, {64, 0, 64}};
  stray.push_back({256, 0, 256});
  assert(!shared::try_decompose_footprint_into_rectangles(stray, {0, 1, 0}, step));

  // A single row spans no cell at all.
  const std::vector<linalg::vec3> row = {{0, 0, 0}, {64, 0, 0}, {128, 0, 0}, {192, 0, 0}};
  assert(!shared::try_decompose_footprint_into_rectangles(row, {0, 1, 0}, step));

  // Points off the plane are not a footprint.
  const std::vector<linalg::vec3> not_planar = {
      {0, 0, 0}, {64, 0, 0}, {0, 0, 64}, {64, 32, 64}};
  assert(!shared::try_decompose_footprint_into_rectangles(not_planar, {0, 1, 0}, step));

  std::cout << "test_footprints_the_cell_reading_cannot_account_for passed" << std::endl;
}

void test_cells_on_a_slanted_face_stay_on_its_plane()
{
  // The face you get by cutting a corner off a box. The grid is walked in the
  // same basis there, so a cell is a square in the plane, not in world x/z.
  const linalg::vec3 normal = linalg::normalize(linalg::vec3{1, 0, 1});
  const float        step   = 64.0f;

  linalg::vec3 tangent_u;
  linalg::vec3 tangent_v;
  shared::brush_face_grid_tangents(normal, tangent_u, tangent_v);

  const float plane_distance = 128.0f;

  std::vector<linalg::vec3> footprint;
  for (int iv = 0; iv <= 1; ++iv)
  {
    for (int iu = 0; iu <= 1; ++iu)
    {
      const linalg::vec3 in_plane =
          tangent_u * ((float)iu * step) + tangent_v * ((float)iv * step);
      footprint.push_back(in_plane +
                          normal * (plane_distance - linalg::dot(normal, in_plane)));
    }
  }

  std::optional<std::vector<std::vector<linalg::vec3>>> rectangles =
      shared::try_decompose_footprint_into_rectangles(footprint, normal, step);
  assert(rectangles.has_value());
  assert(rectangles->size() == 1);

  for (const linalg::vec3 &corner : (*rectangles)[0])
    assert(std::abs(linalg::dot(normal, corner) - plane_distance) < 1e-3f);

  const std::vector<linalg::vec3> extruded =
      shared::extrude_brush_hull((*rectangles)[0], normal, 64.0f);
  std::optional<brush_polyhedron_t> solid = shared::try_build_brush_polyhedron(extruded);
  assert(solid.has_value());
  assert(solid->faces.size() == 6);
  assert_is_closed_convex_solid(*solid);

  std::cout << "test_cells_on_a_slanted_face_stay_on_its_plane passed" << std::endl;
}

} // namespace

int main()
{
  test_cube_hulls_into_six_quads();
  test_degenerate_point_sets_are_rejected();
  test_welding_a_cube_edge_down_makes_a_wedge();
  test_near_coplanar_face_holds_together_then_splits();
  test_off_grid_vertices_hull_the_same_as_on_grid_ones();
  test_extruding_a_concave_footprint_yields_its_hull();
  test_filled_grid_cells_split_a_concave_footprint();
  test_a_full_rectangle_stays_one_brush();
  test_footprints_the_cell_reading_cannot_account_for();
  test_cells_on_a_slanted_face_stay_on_its_plane();
  test_collision_planes_match_the_hull();
  test_mesh_covers_every_face();
  test_text_round_trip_is_bit_exact_and_canonical();
  test_vertex_cap_is_rejected_not_truncated();
  test_grid_snap_is_an_operation_not_an_invariant();
  test_geometry_block_round_trip();
  test_map_round_trip_and_session_collision();

  std::cout << "all brush tests passed" << std::endl;
  return 0;
}
