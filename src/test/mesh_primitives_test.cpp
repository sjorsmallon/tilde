// A primitive is one recipe mesh (geometry_def.md §11.8 increment 10): every
// kind builds a closed planar solid with its low corner at the origin and
// decomposes into the pieces the brush era placed one by one; a quarter turn
// swaps the extents exactly; a regeneration keeps each face's surface by
// index; a hand edit freezes the recipe; a curve's points rebase to its corner
// and stay there. The one curve, centripetal Catmull-Rom, is pinned here too.

#include "shared/curve.hpp"
#include "shared/map/mesh/mesh_decomposition.hpp"
#include "shared/map/mesh/mesh_primitives.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

using shared::mesh_geometry_t;
using shared::primitive_kind_t;
using shared::primitive_recipe_t;

namespace
{

bool nearly(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

void assert_solid_at_the_corner(const mesh_geometry_t &mesh, const char *label)
{
  const std::optional<shared::mesh_defect_t> defect = shared::find_mesh_defect(mesh);
  if (defect)
    std::cout << "  " << label << ": " << shared::format_mesh_defect(*defect) << "\n";
  assert(!defect.has_value());
  assert(shared::compute_mesh_volume(mesh) > 0.f);
  const shared::aabb_bounds_t bounds = shared::compute_mesh_local_bounds(mesh);
  assert(nearly(bounds.min.x, 0.f, 1e-3f) && nearly(bounds.min.y, 0.f, 1e-3f) &&
         nearly(bounds.min.z, 0.f, 1e-3f));
  assert(std::holds_alternative<shared::recipe_mesh_source_t>(mesh.source));
}

size_t count_pieces(const mesh_geometry_t &mesh)
{
  const std::optional<std::vector<mesh_geometry_t>> pieces =
      shared::try_decompose_mesh_into_convex_pieces(mesh);
  assert(pieces.has_value());
  return pieces->size();
}

void test_a_prism_is_its_polygon_extruded()
{
  primitive_recipe_t recipe;
  recipe.kind = primitive_kind_t::Prism;
  recipe.prism_sides = 12;
  recipe.prism_radius = 48.f;
  recipe.prism_half_length = 40.f;
  const mesh_geometry_t prism = shared::make_primitive_mesh(recipe);
  assert_solid_at_the_corner(prism, "prism");
  assert(prism.faces.size() == 14);
  const float area = 0.5f * 12.f * 48.f * 48.f * std::sin(2.f * std::numbers::pi_v<float> / 12.f);
  const float volume = area * 80.f;
  assert(nearly(shared::compute_mesh_volume(prism), volume, volume * 1e-3f));
  assert(count_pieces(prism) == 1);
  for (size_t face = 2; face < prism.faces.size(); ++face)
    assert(prism.faces[face].surface.smoothing_group == shared::PRIMITIVE_CURVED_SMOOTHING_GROUP);
  assert(prism.faces[0].surface.smoothing_group == 0);

  const shared::aabb_bounds_t bounds = shared::compute_mesh_local_bounds(prism);
  assert(nearly(bounds.max.y, 80.f, 1e-3f));
  std::cout << "  a 12-sided prism: 14 faces, the polygon's volume, one piece\n";
}

void test_an_arch_is_one_solid_that_decomposes_into_its_segments()
{
  primitive_recipe_t recipe;
  recipe.kind = primitive_kind_t::Arch;
  recipe.axis_index = 2;
  recipe.arch_segments = 8;
  recipe.arch_inner_radius = 64.f;
  recipe.arch_thickness = 16.f;
  recipe.arch_half_depth = 8.f;
  const mesh_geometry_t arch = shared::make_primitive_mesh(recipe);
  assert_solid_at_the_corner(arch, "arch");
  assert(arch.faces.size() == 2 + 2 * 9);
  assert(count_pieces(arch) == 8);

  const float polygon_area = 0.5f * 8.f * std::sin(std::numbers::pi_v<float> / 8.f) *
                             (80.f * 80.f - 64.f * 64.f);
  const float volume = polygon_area * 16.f;
  assert(nearly(shared::compute_mesh_volume(arch), volume, volume * 1e-3f));

  size_t smoothed = 0;
  for (const shared::mesh_face_t &face : arch.faces)
    smoothed += face.surface.smoothing_group == shared::PRIMITIVE_CURVED_SMOOTHING_GROUP;
  assert(smoothed == 16);

  const shared::aabb_bounds_t bounds = shared::compute_mesh_local_bounds(arch);
  assert(nearly(bounds.max.x, 160.f, 1e-3f) && nearly(bounds.max.y, 80.f, 1e-3f) &&
         nearly(bounds.max.z, 16.f, 1e-3f));
  std::cout << "  an 8-segment arch: one solid, 20 faces, 8 pieces, 16 rim faces smoothed\n";
}

void test_a_straight_flight_is_the_profile_extruded_and_decomposes_into_its_steps()
{
  primitive_recipe_t recipe;
  recipe.kind = primitive_kind_t::Straight_Stairs;
  recipe.axis_index = 0;
  recipe.stair_steps = 5;
  recipe.stair_rise = 16.f;
  recipe.stair_tread_depth = 24.f;
  recipe.stair_width = 96.f;
  const mesh_geometry_t flight = shared::make_primitive_mesh(recipe);
  assert_solid_at_the_corner(flight, "flight");
  assert(flight.faces.size() == 2 + 2 * 5 + 2);
  float volume = 0.f;
  for (int step = 1; step <= 5; ++step)
    volume += (float)step * 16.f * 24.f * 96.f;
  assert(nearly(shared::compute_mesh_volume(flight), volume, volume * 1e-3f));
  assert(count_pieces(flight) == 5);

  const shared::aabb_bounds_t bounds = shared::compute_mesh_local_bounds(flight);
  assert(nearly(bounds.max.x, 120.f, 1e-3f) && nearly(bounds.max.y, 80.f, 1e-3f) &&
         nearly(bounds.max.z, 96.f, 1e-3f));

  recipe.axis_index = 1;
  const mesh_geometry_t clamped = shared::make_primitive_mesh(recipe);
  assert(std::get<shared::recipe_mesh_source_t>(clamped.source).recipe.axis_index == 0);
  std::cout << "  a 5-step flight: the profile extruded, the series' volume, 5 pieces\n";
}

void test_a_spiral_is_one_shell_per_slab()
{
  primitive_recipe_t recipe;
  recipe.kind = primitive_kind_t::Spiral_Stairs;
  recipe.stair_steps = 6;
  recipe.stair_rise = 16.f;
  recipe.spiral_inner_radius = 32.f;
  recipe.spiral_tread_width = 32.f;
  recipe.spiral_degrees_per_step = 45.f;
  for (const bool clockwise : {false, true})
  {
    recipe.spiral_clockwise = clockwise;
    const mesh_geometry_t spiral = shared::make_primitive_mesh(recipe);
    assert_solid_at_the_corner(spiral, clockwise ? "spiral clockwise" : "spiral");
    assert(spiral.vertices.size() == 6 * 8);
    assert(spiral.faces.size() == 6 * 6);
    assert(shared::separate_mesh_shells(spiral).size() == 6);
    assert(count_pieces(spiral) == 6);
    const float slab_volume = 0.5f * (64.f * 64.f - 32.f * 32.f) * std::sin(std::numbers::pi_v<float> / 4.f) * 16.f;
    const float volume = 6.f * slab_volume;
    assert(nearly(shared::compute_mesh_volume(spiral), volume, volume * 1e-3f));
    size_t smoothed = 0;
    for (const shared::mesh_face_t &face : spiral.faces)
      smoothed += face.surface.smoothing_group == shared::PRIMITIVE_CURVED_SMOOTHING_GROUP;
    assert(smoothed == 12);
  }
  std::cout << "  a 6-step spiral: 6 shells, 36 faces, 6 pieces, both ways round\n";
}

void test_a_curve_wall_is_one_loft()
{
  primitive_recipe_t recipe;
  recipe.kind = primitive_kind_t::Curve;
  recipe.curve_width = 16.f;
  recipe.curve_height = 64.f;
  recipe.curve_samples_per_segment = 4;

  recipe.curve_points = {{0.f, 0.f, 0.f}, {100.f, 0.f, 0.f}, {200.f, 0.f, 0.f}};
  shared::rebase_curve_points_to_wall_corner(recipe);
  const mesh_geometry_t straight = shared::make_primitive_mesh(recipe);
  assert_solid_at_the_corner(straight, "straight wall");
  assert(count_pieces(straight) == 1);
  const float volume = 200.f * 16.f * 64.f;
  assert(nearly(shared::compute_mesh_volume(straight), volume, volume * 1e-2f));

  recipe.curve_points = {{0.f, 0.f, 0.f}, {100.f, 0.f, 0.f}, {100.f, 0.f, 100.f}};
  shared::rebase_curve_points_to_wall_corner(recipe);
  const mesh_geometry_t bend = shared::make_primitive_mesh(recipe);
  assert_solid_at_the_corner(bend, "bent wall");
  assert(count_pieces(bend) > 1);
  for (const shared::mesh_face_t &face : bend.faces)
    assert(face.vertex_indices.size() == 4);

  recipe.curve_points = {{0.f, 0.f, 0.f}, {100.f, 0.f, 0.f}, {100.f, 40.f, 100.f}};
  shared::rebase_curve_points_to_wall_corner(recipe);
  const mesh_geometry_t climb = shared::make_primitive_mesh(recipe);
  assert_solid_at_the_corner(climb, "climbing wall");
  size_t triangles = 0;
  for (const shared::mesh_face_t &face : climb.faces)
    triangles += face.vertex_indices.size() == 3;
  assert(triangles > 0);

  // A zigzag turning tighter than the half width at every point. The inner
  // side would run backwards there and fold the wall through itself; those
  // samples are skipped, so the loft is a solid that decomposes.
  recipe.curve_samples_per_segment = 16;
  recipe.curve_points = {{0.f, 0.f, 0.f}, {128.f, 0.f, 0.f}, {0.f, 0.f, 64.f}, {128.f, 0.f, 128.f}};
  shared::rebase_curve_points_to_wall_corner(recipe);
  const mesh_geometry_t zigzag = shared::make_primitive_mesh(recipe);
  assert_solid_at_the_corner(zigzag, "zigzag wall");
  assert(zigzag.vertices.size() < 4 * ((recipe.curve_points.size() - 1) * 16 + 1));
  for (size_t section = 0; section + 1 < zigzag.vertices.size() / 4; ++section)
  {
    const uint32_t b = (uint32_t)section * 4;
    const uint32_t n = b + 4;
    const linalg::vec3 advance = (zigzag.vertices[n] + zigzag.vertices[n + 1]) * 0.5f -
                                 (zigzag.vertices[b] + zigzag.vertices[b + 1]) * 0.5f;
    assert(linalg::dot(zigzag.vertices[n] - zigzag.vertices[b], advance) > 0.f);
    assert(linalg::dot(zigzag.vertices[n + 1] - zigzag.vertices[b + 1], advance) > 0.f);
  }
  assert(count_pieces(zigzag) > 1);

  recipe.curve_points = {{0.f, 0.f, 0.f}, {0.f, 100.f, 0.f}};
  const mesh_geometry_t ladder = shared::make_primitive_mesh(recipe);
  assert(ladder.vertices.empty());

  recipe.curve_points = {{5.f, 5.f, 5.f}};
  assert(shared::make_primitive_mesh(recipe).vertices.empty());
  std::cout << "  a curve wall: a line is one piece, a bend several, a climb splits its "
               "twisted quads, a ladder is nothing\n";
}

bool point_is_inside_piece(const shared::collision_piece_t &piece, const linalg::vec3 &point)
{
  for (const Plane &plane : piece.planes)
    if (linalg::dot(plane.normal, point - plane.point) > 1e-3f)
      return false;
  return true;
}

size_t count_pieces_containing(const std::vector<shared::collision_piece_t> &pieces,
                               const linalg::vec3 &point, size_t first, size_t last)
{
  size_t count = 0;
  for (size_t index = first; index < last; ++index)
    count += point_is_inside_piece(pieces[index], point);
  return count;
}

std::vector<mesh_geometry_t> section_solids_matching_the_loft(const primitive_recipe_t &recipe,
                                                              const mesh_geometry_t &loft,
                                                              const char *label)
{
  const std::vector<mesh_geometry_t> solids = shared::make_curve_wall_section_solids(recipe);
  const size_t sections = loft.vertices.size() / 4;
  assert(solids.size() >= sections - 1 && solids.size() <= 2 * (sections - 1));
  for (const mesh_geometry_t &solid : solids)
  {
    const std::optional<shared::mesh_defect_t> defect = shared::find_mesh_defect(solid);
    if (defect)
      std::cout << "  " << label << " section: " << shared::format_mesh_defect(*defect) << "\n";
    assert(!defect.has_value());
    assert(shared::mesh_is_convex(solid));
    assert((solid.vertices.size() == 8 && solid.faces.size() == 6) ||
           (solid.vertices.size() == 6 && solid.faces.size() == 5));
  }
  return solids;
}

void test_a_curve_wall_collides_as_its_sections()
{
  primitive_recipe_t recipe;
  recipe.kind = primitive_kind_t::Curve;
  recipe.curve_width = 16.f;
  recipe.curve_height = 64.f;
  recipe.curve_samples_per_segment = 4;

  recipe.curve_points = {{0.f, 0.f, 0.f}, {100.f, 0.f, 0.f}, {200.f, 0.f, 0.f}};
  shared::rebase_curve_points_to_wall_corner(recipe);
  const mesh_geometry_t straight = shared::make_primitive_mesh(recipe);
  const std::vector<mesh_geometry_t> straight_solids =
      section_solids_matching_the_loft(recipe, straight, "straight");
  assert(straight_solids.size() == straight.vertices.size() / 4 - 1);
  float sum_of_sections = 0.f;
  for (const mesh_geometry_t &solid : straight_solids)
  {
    assert(solid.vertices.size() == 8);
    sum_of_sections += shared::compute_mesh_volume(solid);
  }
  const float loft_volume = shared::compute_mesh_volume(straight);
  assert(nearly(sum_of_sections, loft_volume, loft_volume * 1e-2f));

  // Two straight arms through one point, joined by a hairpin: the arms cross
  // at the second and fifth control points. The sections cover the crossing
  // twice and the notches beside it not at all.
  recipe.curve_points = {{0.f, 0.f, 0.f},     {100.f, 0.f, 50.f}, {200.f, 0.f, 100.f},
                         {200.f, 0.f, 0.f}, {100.f, 0.f, 50.f}, {0.f, 0.f, 100.f}};
  shared::rebase_curve_points_to_wall_corner(recipe);
  const mesh_geometry_t crossing = shared::make_primitive_mesh(recipe);
  assert_solid_at_the_corner(crossing, "crossing wall");
  const std::vector<mesh_geometry_t> crossing_solids =
      section_solids_matching_the_loft(recipe, crossing, "crossing");
  std::vector<shared::collision_piece_t> pieces;
  for (const mesh_geometry_t &solid : crossing_solids)
    pieces.push_back(shared::collision_piece_from_convex_mesh(solid));

  const linalg::vec3 cross_point = recipe.curve_points[1];
  const linalg::vec3 first_arm = linalg::normalize(recipe.curve_points[2] - recipe.curve_points[0]);
  const linalg::vec3 second_arm = linalg::normalize(recipe.curve_points[5] - recipe.curve_points[3]);
  const linalg::vec3 half_height{0.f, recipe.curve_height * 0.5f, 0.f};
  const linalg::vec3 overlap = cross_point + half_height + first_arm * 3.f;
  const size_t halfway = pieces.size() / 2;
  assert(count_pieces_containing(pieces, overlap, 0, halfway) >= 1);
  assert(count_pieces_containing(pieces, overlap, halfway, pieces.size()) >= 1);
  const linalg::vec3 between_the_arms = linalg::normalize(first_arm + second_arm) * 20.f;
  assert(count_pieces_containing(pieces, cross_point + half_height + between_the_arms, 0,
                                 pieces.size()) == 0);
  assert(count_pieces_containing(pieces, cross_point + half_height - between_the_arms, 0,
                                 pieces.size()) == 0);

  recipe.curve_samples_per_segment = 16;
  recipe.curve_points = {{0.f, 0.f, 0.f}, {128.f, 0.f, 0.f}, {0.f, 0.f, 64.f}, {128.f, 0.f, 128.f}};
  shared::rebase_curve_points_to_wall_corner(recipe);
  const mesh_geometry_t zigzag = shared::make_primitive_mesh(recipe);
  (void)section_solids_matching_the_loft(recipe, zigzag, "zigzag");

  std::cout << "  a curve wall collides as its sections: a line's sum to the loft, crossing arms "
               "overlap and leave the notches empty, the zigzag skips the same samples\n";
}

void test_a_quarter_turn_swaps_the_extents_exactly()
{
  primitive_recipe_t recipe;
  recipe.kind = primitive_kind_t::Straight_Stairs;
  recipe.axis_index = 0;
  recipe.stair_steps = 3;
  const mesh_geometry_t flat = shared::make_primitive_mesh(recipe);
  recipe.quarter_turns = 1;
  const mesh_geometry_t turned = shared::make_primitive_mesh(recipe);
  assert_solid_at_the_corner(turned, "turned flight");
  const shared::aabb_bounds_t before = shared::compute_mesh_local_bounds(flat);
  const shared::aabb_bounds_t after = shared::compute_mesh_local_bounds(turned);
  assert(before.max.x == after.max.z && before.max.z == after.max.x && before.max.y == after.max.y);
  assert(nearly(shared::primitive_recipe_axis(recipe).z, -1.f, 1e-5f));
  std::cout << "  a quarter turn swaps the extents bit for bit\n";
}

void test_a_regeneration_keeps_surfaces_by_index_and_a_hand_edit_freezes()
{
  primitive_recipe_t recipe;
  recipe.kind = primitive_kind_t::Arch;
  recipe.arch_segments = 4;
  mesh_geometry_t arch = shared::make_primitive_mesh(recipe);
  arch.position = {10.f, 20.f, 30.f};
  arch.faces[2].surface.material = 7;
  arch.faces[2].surface.smoothing_group = 99;
  arch.faces.back().surface.material = 3;
  const size_t faces_before = arch.faces.size();

  recipe.arch_segments = 6;
  shared::regenerate_mesh_from_recipe(arch, recipe);
  assert(arch.faces.size() == faces_before + 4);
  assert(nearly(arch.position.x, 10.f, 0.f));
  assert(arch.faces[2].surface.material == 7);
  assert(arch.faces[2].surface.smoothing_group == shared::PRIMITIVE_CURVED_SMOOTHING_GROUP);
  assert(arch.faces[faces_before - 1].surface.material == 3);
  assert(std::get<shared::recipe_mesh_source_t>(arch.source).recipe.arch_segments == 6);

  recipe.arch_segments = 2;
  shared::regenerate_mesh_from_recipe(arch, recipe);
  assert(arch.faces.size() == 2 + 2 * 3);
  assert(arch.faces[2].surface.material == 7);

  assert(shared::freeze_mesh_recipe(arch));
  assert(std::holds_alternative<shared::inline_mesh_source_t>(arch.source));
  assert(!shared::freeze_mesh_recipe(arch));
  assert(!shared::find_mesh_defect(arch).has_value());
  std::cout << "  a regeneration keeps surfaces by index; a freeze makes the source inline\n";
}

void test_curve_points_rebase_to_the_corner_and_stay_there()
{
  primitive_recipe_t recipe;
  recipe.kind = primitive_kind_t::Curve;
  recipe.curve_points = {{100.f, 50.f, -300.f}, {300.f, 50.f, -300.f}, {300.f, 50.f, -100.f}};
  recipe.curve_samples_per_segment = 3;
  const float volume_in_the_world = shared::compute_mesh_volume(shared::make_primitive_mesh(recipe));

  shared::rebase_curve_points_to_wall_corner(recipe);
  const mesh_geometry_t rebased = shared::make_primitive_mesh(recipe);
  assert_solid_at_the_corner(rebased, "rebased wall");
  assert(nearly(shared::compute_mesh_volume(rebased), volume_in_the_world,
                volume_in_the_world * 1e-3f));
  for (const linalg::vec3 &point : recipe.curve_points)
    assert(nearly(point.y, 0.f, 1e-3f) && point.x >= -1e-2f && point.z >= -1e-2f);

  const std::vector<linalg::vec3> once = recipe.curve_points;
  shared::rebase_curve_points_to_wall_corner(recipe);
  for (size_t i = 0; i < once.size(); ++i)
    assert(linalg::length(recipe.curve_points[i] - once[i]) < 1e-3f);

  recipe.curve_points = {{7.f, 8.f, 9.f}};
  shared::rebase_curve_points_to_wall_corner(recipe);
  assert(recipe.curve_points[0].x == 7.f);
  std::cout << "  curve points rebase to the wall's corner, idempotently\n";
}

void test_catmull_rom_passes_through_its_points_and_a_line_stays_a_line()
{
  const std::vector<linalg::vec3> points = {{0.f, 0.f, 0.f}, {100.f, 0.f, 0.f},
                                            {130.f, 0.f, 0.f}, {400.f, 0.f, 0.f}};
  const int samples_per_segment = 5;
  const std::vector<linalg::vec3> samples = shared::sample_catmull_rom(
      Span<const linalg::vec3>(points.data(), points.size()), samples_per_segment);
  assert(samples.size() == (points.size() - 1) * (size_t)samples_per_segment + 1);
  for (size_t i = 0; i < points.size(); ++i)
    assert(nearly(linalg::length(samples[i * (size_t)samples_per_segment] - points[i]), 0.f, 1e-4f));
  for (size_t i = 1; i < samples.size(); ++i)
  {
    assert(nearly(samples[i].y, 0.f, 1e-4f) && nearly(samples[i].z, 0.f, 1e-4f));
    assert(samples[i].x > samples[i - 1].x);
  }

  const std::vector<linalg::vec3> bend = {{0.f, 0.f, 0.f}, {100.f, 0.f, 0.f}, {100.f, 0.f, 100.f}};
  const std::vector<linalg::vec3> bent =
      shared::sample_catmull_rom(Span<const linalg::vec3>(bend.data(), bend.size()), 8);
  float lowest_z = 0.f;
  float highest_x = 100.f;
  for (const linalg::vec3 &sample : bent)
  {
    assert(nearly(sample.y, 0.f, 1e-4f));
    assert(sample.x >= -1e-3f && sample.z <= 100.f + 1e-3f);
    lowest_z = std::min(lowest_z, sample.z);
    highest_x = std::max(highest_x, sample.x);
  }
  std::cout << "catmull-rom corner: dips to z " << lowest_z << ", overshoots to x " << highest_x
            << std::endl;
  assert(lowest_z < 0.f && lowest_z > -25.f);
  assert(highest_x > 100.f && highest_x < 125.f);
}

} // namespace

int main()
{
  test_a_prism_is_its_polygon_extruded();
  test_an_arch_is_one_solid_that_decomposes_into_its_segments();
  test_a_straight_flight_is_the_profile_extruded_and_decomposes_into_its_steps();
  test_a_spiral_is_one_shell_per_slab();
  test_a_curve_wall_is_one_loft();
  test_a_curve_wall_collides_as_its_sections();
  test_a_quarter_turn_swaps_the_extents_exactly();
  test_a_regeneration_keeps_surfaces_by_index_and_a_hand_edit_freezes();
  test_curve_points_rebase_to_the_corner_and_stay_there();
  test_catmull_rom_passes_through_its_points_and_a_line_stays_a_line();
  std::cout << "mesh_primitives_test passed" << std::endl;
  return 0;
}
