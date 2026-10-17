// A subdivided face keeps its boundary. A boundary grid vertex welds with the
// face across its edge only when that face is subdivided to the SAME level;
// every other boundary vertex, and every corner, is pinned to the hull edge
// whatever offset was written for it. Across brushes the face across the edge
// is a coplanar face of another brush holding the same edge, under the same
// rule. geometry_def.md Track F.

#include "shared/map/brush/brush.hpp"
#include "shared/map/map.hpp"
#include "shared/map/map_geometry.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <optional>
#include <vector>

using shared::brush_polyhedron_t;

namespace
{

const Plane TOP{{0.f, 64.f, 0.f}, {0.f, 1.f, 0.f}};
const Plane SIDE{{64.f, 0.f, 0.f}, {1.f, 0.f, 0.f}};
const linalg::vec3 PULL{12.f, 12.f, 0.f};
const linalg::vec3 SHARED_EDGE_MIDDLE{64.f, 64.f, 0.f};

shared::brush_geometry_t make_box_with_grids(int top_level, int side_level)
{
  shared::brush_geometry_t brush = shared::make_box_brush({0, 0, 0}, {64, 64, 64});
  shared::sync_face_surfaces(brush);
  shared::resize_face_grid(shared::face_surface_for(brush, TOP), top_level);
  shared::resize_face_grid(shared::face_surface_for(brush, SIDE), side_level);
  for (shared::face_surface_t &face : brush.face_surfaces)
  {
    for (linalg::vec3 &offset : face.offsets)
      offset = PULL;
  }
  return brush;
}

struct read_grids_t
{
  brush_polyhedron_t hull;
  shared::brush_face_grids_t pulled;
  shared::brush_face_grids_t flat;
};

void clear_offsets(shared::brush_geometry_t &brush)
{
  for (shared::face_surface_t &face : brush.face_surfaces)
  {
    for (linalg::vec3 &offset : face.offsets)
      offset = {0.f, 0.f, 0.f};
  }
}

read_grids_t read_grids(const shared::brush_geometry_t &brush)
{
  shared::brush_geometry_t unpulled = brush;
  clear_offsets(unpulled);

  const std::optional<brush_polyhedron_t> hull =
      shared::try_build_brush_polyhedron(brush.hull_points);
  assert(hull);
  return {*hull, shared::build_brush_face_grids(brush, *hull, {}),
          shared::build_brush_face_grids(unpulled, *hull, {})};
}

size_t face_with_normal(const brush_polyhedron_t &hull, const linalg::vec3 &normal)
{
  for (size_t face = 0; face < hull.faces.size(); ++face)
  {
    if (linalg::dot(hull.faces[face].plane.normal, normal) > 0.999f)
      return face;
  }
  assert(false);
  return 0;
}

bool is_on_box_edge(const linalg::vec3 &base)
{
  int extreme_axes = 0;
  for (int axis = 0; axis < 3; ++axis)
    extreme_axes += std::abs(std::abs(base[axis]) - 64.f) < 1e-4f ? 1 : 0;
  return extreme_axes >= 2;
}

bool is_on_shared_edge(const linalg::vec3 &base)
{
  return std::abs(base.x - 64.f) < 1e-4f && std::abs(base.y - 64.f) < 1e-4f;
}

struct boundary_count_t
{
  size_t pinned = 0;
  size_t pulled = 0;
  size_t interior = 0;
};

boundary_count_t count_face(const read_grids_t &grids, size_t face,
                            bool shared_edge_pulled)
{
  const std::vector<linalg::vec3> &read = grids.pulled.grid_vertices[face];
  const std::vector<linalg::vec3> &base = grids.flat.grid_vertices[face];
  assert(read.size() == base.size() && !read.empty());

  boundary_count_t count;
  for (size_t vertex = 0; vertex < read.size(); ++vertex)
  {
    const linalg::vec3 offset = read[vertex] - base[vertex];
    if (!is_on_box_edge(base[vertex]))
    {
      assert(linalg::length(offset - PULL) < 1e-4f);
      ++count.interior;
      continue;
    }

    const bool corner = std::abs(std::abs(base[vertex].z) - 64.f) < 1e-4f &&
                        is_on_shared_edge(base[vertex]);
    if (shared_edge_pulled && is_on_shared_edge(base[vertex]) && !corner)
    {
      assert(linalg::length(offset - PULL) < 1e-4f);
      ++count.pulled;
      continue;
    }

    assert(linalg::length(offset) < 1e-4f);
    ++count.pinned;
  }
  return count;
}

void test_a_face_beside_flat_faces_keeps_its_whole_boundary()
{
  const read_grids_t grids = read_grids(make_box_with_grids(2, 0));
  const boundary_count_t top =
      count_face(grids, face_with_normal(grids.hull, TOP.normal), false);
  assert(top.interior == 1 && top.pinned == 8 && top.pulled == 0);
  std::cout << "test_a_face_beside_flat_faces_keeps_its_whole_boundary passed" << std::endl;
}

void test_same_level_neighbours_weld_only_their_shared_edge()
{
  const read_grids_t grids = read_grids(make_box_with_grids(2, 2));
  const size_t top = face_with_normal(grids.hull, TOP.normal);
  const size_t side = face_with_normal(grids.hull, SIDE.normal);

  const boundary_count_t top_count = count_face(grids, top, true);
  const boundary_count_t side_count = count_face(grids, side, true);
  assert(top_count.interior == 1 && top_count.pulled == 1 && top_count.pinned == 7);
  assert(side_count.interior == 1 && side_count.pulled == 1 && side_count.pinned == 7);

  size_t coincident = 0;
  for (const linalg::vec3 &a : grids.pulled.grid_vertices[top])
  {
    for (const linalg::vec3 &b : grids.pulled.grid_vertices[side])
    {
      if (linalg::length(a - b) < 1e-5f)
      {
        ++coincident;
        break;
      }
    }
  }
  assert(coincident == 3);
  std::cout << "test_same_level_neighbours_weld_only_their_shared_edge passed" << std::endl;
}

void test_different_levels_pin_instead_of_leaving_a_t_junction()
{
  const read_grids_t grids = read_grids(make_box_with_grids(2, 1));
  const boundary_count_t top =
      count_face(grids, face_with_normal(grids.hull, TOP.normal), false);
  const boundary_count_t side =
      count_face(grids, face_with_normal(grids.hull, SIDE.normal), false);
  assert(top.interior == 1 && top.pinned == 8 && top.pulled == 0);
  assert(side.interior == 0 && side.pinned == 4 && side.pulled == 0);
  std::cout << "test_different_levels_pin_instead_of_leaving_a_t_junction passed"
            << std::endl;
}

const linalg::vec3 WRITE{0.f, 8.f, 0.f};
const linalg::vec3 TOP_CENTRE{0.f, 64.f, 0.f};

struct written_count_t
{
  size_t written = 0;
  size_t untouched = 0;
};

written_count_t count_written(const shared::brush_geometry_t &brush, const Plane &plane,
                              const linalg::vec3 &expected)
{
  written_count_t count;
  for (const linalg::vec3 &offset : shared::find_face_surface(brush, plane)->offsets)
  {
    if (linalg::length(offset) < 1e-4f)
      ++count.untouched;
    else if (linalg::length(offset - expected) < 1e-4f)
      ++count.written;
    else
      assert(false);
  }
  return count;
}

void test_a_sculpt_stroke_never_writes_a_pinned_vertex()
{
  shared::brush_geometry_t brush = make_box_with_grids(2, 0);
  clear_offsets(brush);
  const size_t moved = shared::sculpt_brush_grid_vertices(brush, TOP_CENTRE, 100.f, WRITE, {});
  const written_count_t top = count_written(brush, TOP, WRITE);
  assert(moved == 1 && top.written == 1 && top.untouched == 8);
  std::cout << "test_a_sculpt_stroke_never_writes_a_pinned_vertex passed" << std::endl;
}

void test_a_nudge_never_writes_a_pinned_vertex()
{
  shared::brush_geometry_t brush = make_box_with_grids(2, 0);
  clear_offsets(brush);
  const read_grids_t grids = read_grids(brush);
  const std::vector<linalg::vec3> &points =
      grids.flat.grid_vertices[face_with_normal(grids.hull, TOP.normal)];
  const size_t moved = shared::nudge_brush_grid_vertices(brush, points, WRITE, {});
  const written_count_t top = count_written(brush, TOP, WRITE);
  assert(moved == 1 && top.written == 1 && top.untouched == 8);
  std::cout << "test_a_nudge_never_writes_a_pinned_vertex passed" << std::endl;
}

void test_a_sculpt_stroke_writes_a_welded_vertex_into_both_faces()
{
  shared::brush_geometry_t brush = make_box_with_grids(2, 2);
  clear_offsets(brush);
  const size_t moved =
      shared::sculpt_brush_grid_vertices(brush, SHARED_EDGE_MIDDLE, 10.f, WRITE, {});
  const written_count_t top = count_written(brush, TOP, WRITE);
  const written_count_t side = count_written(brush, SIDE, WRITE);
  assert(moved == 2 && top.written == 1 && top.untouched == 8);
  assert(side.written == 1 && side.untouched == 8);
  std::cout << "test_a_sculpt_stroke_writes_a_welded_vertex_into_both_faces passed"
            << std::endl;
}

void test_the_same_stroke_on_two_brushes_keeps_their_seam_coincident()
{
  shared::brush_geometry_t left = make_box_with_grids(2, 0);
  clear_offsets(left);
  shared::brush_geometry_t right = shared::make_box_brush({128, 0, 0}, {64, 64, 64});
  shared::sync_face_surfaces(right);
  shared::resize_face_grid(shared::face_surface_for(right, TOP), 2);

  const linalg::vec3 seam{64.f, 64.f, 0.f};
  shared::sculpt_brush_grid_vertices(left, seam, 100.f, WRITE, {});
  shared::sculpt_brush_grid_vertices(right, seam, 100.f, WRITE, {});

  const read_grids_t left_grids = read_grids(left);
  const read_grids_t right_grids = read_grids(right);
  const std::vector<linalg::vec3> &left_top =
      left_grids.pulled.grid_vertices[face_with_normal(left_grids.hull, TOP.normal)];
  const std::vector<linalg::vec3> &right_top =
      right_grids.pulled.grid_vertices[face_with_normal(right_grids.hull, TOP.normal)];

  size_t on_seam = 0;
  size_t coincident = 0;
  for (const linalg::vec3 &a : left_top)
  {
    if (std::abs(a.x - 64.f) > 1e-4f)
      continue;
    ++on_seam;
    for (const linalg::vec3 &b : right_top)
    {
      if (linalg::length(a - b) < 1e-5f)
      {
        ++coincident;
        break;
      }
    }
  }
  assert(on_seam == 3 && coincident == 3);
  std::cout << "test_the_same_stroke_on_two_brushes_keeps_their_seam_coincident passed"
            << std::endl;
}

// Two floor tiles meeting at x = 64, the right one shifted by `right_offset_z`
// so its seam edge can differ from the left one's.
std::vector<shared::map_geometry_t> make_tiles(int left_level, int right_level,
                                               float right_offset_z)
{
  shared::brush_geometry_t left = make_box_with_grids(left_level, 0);
  clear_offsets(left);

  shared::brush_geometry_t right =
      shared::make_box_brush({128, 0, right_offset_z}, {64, 64, 64});
  shared::sync_face_surfaces(right);
  shared::resize_face_grid(shared::face_surface_for(right, TOP), right_level);

  std::vector<shared::map_geometry_t> tiles;
  tiles.push_back({1, shared::geometry_value_t{left}});
  tiles.push_back({2, shared::geometry_value_t{right}});
  return tiles;
}

written_count_t sculpt_tile_at_seam(std::vector<shared::map_geometry_t> &tiles, size_t index)
{
  shared::brush_geometry_t &brush = std::get<shared::brush_geometry_t>(tiles[index].value);
  shared::sculpt_brush_grid_vertices(brush, SHARED_EDGE_MIDDLE, 10.f, WRITE,
                                     {tiles, tiles[index].uid});
  return count_written(brush, TOP, WRITE);
}

void test_tiles_at_the_same_level_weld_their_seam()
{
  std::vector<shared::map_geometry_t> tiles = make_tiles(2, 2, 0.f);
  const written_count_t left = sculpt_tile_at_seam(tiles, 0);
  const written_count_t right = sculpt_tile_at_seam(tiles, 1);
  assert(left.written == 1 && left.untouched == 8);
  assert(right.written == 1 && right.untouched == 8);
  std::cout << "test_tiles_at_the_same_level_weld_their_seam passed" << std::endl;
}

void test_tiles_at_different_levels_pin_their_seam()
{
  std::vector<shared::map_geometry_t> tiles = make_tiles(2, 3, 0.f);
  const written_count_t left = sculpt_tile_at_seam(tiles, 0);
  assert(left.written == 0 && left.untouched == 9);
  std::cout << "test_tiles_at_different_levels_pin_their_seam passed" << std::endl;
}

void test_a_tile_offset_along_the_seam_pins_it()
{
  std::vector<shared::map_geometry_t> tiles = make_tiles(2, 2, 64.f);
  const written_count_t left = sculpt_tile_at_seam(tiles, 0);
  assert(left.written == 0 && left.untouched == 9);
  std::cout << "test_a_tile_offset_along_the_seam_pins_it passed" << std::endl;
}

} // namespace

int main()
{
  test_a_face_beside_flat_faces_keeps_its_whole_boundary();
  test_same_level_neighbours_weld_only_their_shared_edge();
  test_different_levels_pin_instead_of_leaving_a_t_junction();
  test_a_sculpt_stroke_never_writes_a_pinned_vertex();
  test_a_nudge_never_writes_a_pinned_vertex();
  test_a_sculpt_stroke_writes_a_welded_vertex_into_both_faces();
  test_the_same_stroke_on_two_brushes_keeps_their_seam_coincident();
  test_tiles_at_the_same_level_weld_their_seam();
  test_tiles_at_different_levels_pin_their_seam();
  test_a_tile_offset_along_the_seam_pins_it();
  std::cout << "brush_boundary_test passed" << std::endl;
  return 0;
}
