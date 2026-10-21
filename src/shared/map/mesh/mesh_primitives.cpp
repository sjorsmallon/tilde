#include "mesh_primitives.hpp"

#include "../../curve.hpp"
#include "../brush/brush.hpp"
#include "log.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace shared
{

namespace
{

constexpr linalg::vec3 AXES[3] = {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
constexpr linalg::vec3 WORLD_UP{0.f, 1.f, 0.f};
constexpr linalg::vec3 WORLD_X{1.f, 0.f, 0.f};
constexpr float CURVE_MIN_HORIZONTAL_ADVANCE = 1e-3f;

void smooth_face(mesh_geometry_t &mesh, size_t face_index)
{
  mesh.faces[face_index].surface.smoothing_group = PRIMITIVE_CURVED_SMOOTHING_GROUP;
}

mesh_geometry_t build_prism(const primitive_recipe_t &recipe)
{
  const primitive_frame_t frame = make_primitive_frame(AXES[recipe.axis_index]);
  std::vector<linalg::vec3> footprint;
  footprint.reserve((size_t)recipe.prism_sides);
  for (int side = 0; side < recipe.prism_sides; ++side)
  {
    const float angle =
        (float)side / (float)recipe.prism_sides * 2.f * std::numbers::pi_v<float>;
    footprint.push_back((frame.side * std::cos(angle) + frame.up * std::sin(angle)) *
                            recipe.prism_radius -
                        frame.axis * recipe.prism_half_length);
  }
  mesh_geometry_t mesh = make_extruded_mesh(footprint, frame.axis, 2.f * recipe.prism_half_length);
  for (size_t face = 2; face < mesh.faces.size(); ++face)
    smooth_face(mesh, face);
  return mesh;
}

mesh_geometry_t build_arch(const primitive_recipe_t &recipe)
{
  const primitive_frame_t frame = make_primitive_frame(AXES[recipe.axis_index]);
  const float outer_radius = recipe.arch_inner_radius + recipe.arch_thickness;
  const size_t segments = (size_t)recipe.arch_segments;
  const linalg::vec3 base = frame.axis * -recipe.arch_half_depth;

  std::vector<linalg::vec3> footprint;
  footprint.reserve(2 * (segments + 1));
  for (size_t rib = 0; rib <= segments; ++rib)
  {
    const float angle = (float)rib / (float)segments * std::numbers::pi_v<float>;
    footprint.push_back((frame.side * std::cos(angle) + frame.up * std::sin(angle)) *
                            outer_radius +
                        base);
  }
  for (size_t rib = segments + 1; rib-- > 0;)
  {
    const float angle = (float)rib / (float)segments * std::numbers::pi_v<float>;
    footprint.push_back((frame.side * std::cos(angle) + frame.up * std::sin(angle)) *
                            recipe.arch_inner_radius +
                        base);
  }

  mesh_geometry_t mesh = make_extruded_mesh(footprint, frame.axis, 2.f * recipe.arch_half_depth);
  const size_t edges = footprint.size();
  for (size_t edge = 0; edge < edges; ++edge)
  {
    const bool radial_end = edge == segments || edge == edges - 1;
    if (!radial_end)
      smooth_face(mesh, 2 + edge);
  }
  return mesh;
}

mesh_geometry_t build_straight_stairs(const primitive_recipe_t &recipe)
{
  const primitive_frame_t frame = make_primitive_frame(AXES[recipe.axis_index]);
  const int steps = recipe.stair_steps;
  const float length = (float)steps * recipe.stair_tread_depth;
  const float half_width = recipe.stair_width * 0.5f;

  std::vector<std::array<float, 2>> profile;
  profile.reserve((size_t)steps * 2 + 2);
  profile.push_back({0.f, 0.f});
  profile.push_back({length, 0.f});
  profile.push_back({length, (float)steps * recipe.stair_rise});
  for (int step = steps - 1; step >= 0; --step)
  {
    const float along = (float)step * recipe.stair_tread_depth;
    profile.push_back({along, (float)(step + 1) * recipe.stair_rise});
    if (step > 0)
      profile.push_back({along, (float)step * recipe.stair_rise});
  }

  std::vector<linalg::vec3> footprint;
  footprint.reserve(profile.size());
  for (const std::array<float, 2> &point : profile)
    footprint.push_back(frame.side * half_width + frame.axis * point[0] + frame.up * point[1]);
  return make_extruded_mesh(footprint, frame.side * -1.f, recipe.stair_width);
}

void append_shell(mesh_geometry_t &mesh, const mesh_geometry_t &shell)
{
  const uint32_t offset = (uint32_t)mesh.vertices.size();
  mesh.vertices.insert(mesh.vertices.end(), shell.vertices.begin(), shell.vertices.end());
  for (const mesh_face_t &face : shell.faces)
  {
    mesh_face_t moved = face;
    for (uint32_t &index : moved.vertex_indices)
      index += offset;
    mesh.faces.push_back(std::move(moved));
  }
}

mesh_geometry_t build_spiral_stairs(const primitive_recipe_t &recipe)
{
  const float turn = (recipe.spiral_clockwise ? -1.f : 1.f) * recipe.spiral_degrees_per_step *
                     std::numbers::pi_v<float> / 180.f;
  const float outer_radius = recipe.spiral_inner_radius + recipe.spiral_tread_width;

  mesh_geometry_t mesh;
  for (int step = 0; step < recipe.stair_steps; ++step)
  {
    const float angle_0 = (float)step * turn;
    const float angle_1 = (float)(step + 1) * turn;
    const linalg::vec3 direction_0{std::cos(angle_0), 0.f, std::sin(angle_0)};
    const linalg::vec3 direction_1{std::cos(angle_1), 0.f, std::sin(angle_1)};
    const linalg::vec3 lift{0.f, (float)step * recipe.stair_rise, 0.f};

    std::vector<linalg::vec3> ring = {
        direction_0 * recipe.spiral_inner_radius + lift,
        direction_1 * recipe.spiral_inner_radius + lift,
        direction_1 * outer_radius + lift,
        direction_0 * outer_radius + lift,
    };
    if (recipe.spiral_clockwise)
      std::reverse(ring.begin(), ring.end());

    mesh_geometry_t slab = make_extruded_mesh(ring, WORLD_UP, recipe.stair_rise);
    smooth_face(slab, 2);
    smooth_face(slab, 4);
    append_shell(mesh, slab);
  }
  return mesh;
}

bool ring_is_planar(const mesh_geometry_t &mesh, const std::vector<uint32_t> &ring)
{
  linalg::vec3 normal{0.f, 0.f, 0.f};
  for (size_t i = 0; i < ring.size(); ++i)
  {
    const linalg::vec3 &current = mesh.vertices[ring[i]];
    const linalg::vec3 &next = mesh.vertices[ring[(i + 1) % ring.size()]];
    normal.x += (current.y - next.y) * (current.z + next.z);
    normal.y += (current.z - next.z) * (current.x + next.x);
    normal.z += (current.x - next.x) * (current.y + next.y);
  }
  if (linalg::length_squared(normal) < 1e-12f)
    return true;
  normal = linalg::normalize(normal);
  const linalg::vec3 &origin = mesh.vertices[ring[0]];
  for (uint32_t index : ring)
    if (std::abs(linalg::dot(normal, mesh.vertices[index] - origin)) > BRUSH_COPLANAR_EPSILON)
      return false;
  return true;
}

void push_quad_or_triangles(mesh_geometry_t &mesh, std::vector<uint32_t> ring)
{
  if (ring_is_planar(mesh, ring))
  {
    mesh.faces.push_back({std::move(ring), {}});
    return;
  }
  mesh.faces.push_back({{ring[0], ring[1], ring[2]}, {}});
  mesh.faces.push_back({{ring[0], ring[2], ring[3]}, {}});
}

mesh_geometry_t build_curve_wall(const primitive_recipe_t &recipe)
{
  mesh_geometry_t mesh;
  if (recipe.curve_points.size() < 2)
    return mesh;

  const std::vector<linalg::vec3> samples = sample_catmull_rom(
      Span<const linalg::vec3>(recipe.curve_points.data(), recipe.curve_points.size()),
      recipe.curve_samples_per_segment);
  const float half_width = recipe.curve_width * 0.5f;

  std::optional<linalg::vec3> last_kept;
  size_t sections = 0;
  for (size_t i = 0; i < samples.size(); ++i)
  {
    const linalg::vec3 &at = samples[i];
    if (last_kept)
    {
      const linalg::vec3 advance{at.x - last_kept->x, 0.f, at.z - last_kept->z};
      if (linalg::length(advance) < CURVE_MIN_HORIZONTAL_ADVANCE)
        continue;
    }
    const linalg::vec3 &previous = samples[i == 0 ? 0 : i - 1];
    const linalg::vec3 &next = samples[i + 1 < samples.size() ? i + 1 : i];
    const linalg::vec3 direction = linalg::normalize(next - previous);
    linalg::vec3 side = linalg::cross(WORLD_UP, direction);
    if (linalg::length_squared(side) < 1e-6f)
      side = linalg::cross(WORLD_UP, WORLD_X);
    side = linalg::normalize(side);

    mesh.vertices.push_back(at - side * half_width);
    mesh.vertices.push_back(at + side * half_width);
    mesh.vertices.push_back(at - side * half_width + WORLD_UP * recipe.curve_height);
    mesh.vertices.push_back(at + side * half_width + WORLD_UP * recipe.curve_height);
    last_kept = at;
    ++sections;
  }
  if (sections < 2)
  {
    mesh.vertices.clear();
    return mesh;
  }

  mesh.faces.push_back({{0, 2, 3, 1}, {}});
  for (uint32_t section = 0; section + 1 < sections; ++section)
  {
    const uint32_t b = section * 4;
    const uint32_t n = b + 4;
    push_quad_or_triangles(mesh, {b, b + 1, n + 1, n});
    push_quad_or_triangles(mesh, {b + 2, n + 2, n + 3, b + 3});
    mesh.faces.push_back({{b, n, n + 2, b + 2}, {}});
    smooth_face(mesh, mesh.faces.size() - 1);
    mesh.faces.push_back({{b + 1, b + 3, n + 3, n + 1}, {}});
    smooth_face(mesh, mesh.faces.size() - 1);
  }
  const uint32_t e = (uint32_t)(sections - 1) * 4;
  mesh.faces.push_back({{e, e + 1, e + 3, e + 2}, {}});
  return mesh;
}

} // namespace

mesh_geometry_t make_primitive_mesh(const primitive_recipe_t &unclamped)
{
  primitive_recipe_t recipe = unclamped;
  clamp_primitive_recipe(recipe);

  mesh_geometry_t mesh;
  switch (recipe.kind)
  {
    case primitive_kind_t::Prism: mesh = build_prism(recipe); break;
    case primitive_kind_t::Arch: mesh = build_arch(recipe); break;
    case primitive_kind_t::Straight_Stairs: mesh = build_straight_stairs(recipe); break;
    case primitive_kind_t::Spiral_Stairs: mesh = build_spiral_stairs(recipe); break;
    case primitive_kind_t::Curve: mesh = build_curve_wall(recipe); break;
  }

  for (linalg::vec3 &vertex : mesh.vertices)
    vertex = turn_about_up(vertex, recipe.quarter_turns);
  if (recipe.kind != primitive_kind_t::Curve && !mesh.vertices.empty())
  {
    const linalg::vec3 corner = compute_mesh_local_bounds(mesh).min;
    for (linalg::vec3 &vertex : mesh.vertices)
      vertex = vertex - corner;
  }

  mesh.source = recipe_mesh_source_t{recipe};
  if (const std::optional<mesh_defect_t> defect = find_mesh_defect(mesh))
    fatal_error("make_primitive_mesh: a {} built {}", to_string(recipe.kind),
                format_mesh_defect(*defect));
  return mesh;
}

void regenerate_mesh_from_recipe(mesh_geometry_t &mesh, const primitive_recipe_t &recipe)
{
  mesh_geometry_t built = make_primitive_mesh(recipe);
  const size_t carried = std::min(mesh.faces.size(), built.faces.size());
  for (size_t face = 0; face < carried; ++face)
  {
    face_surface_t surface = mesh.faces[face].surface;
    surface.smoothing_group = built.faces[face].surface.smoothing_group;
    built.faces[face].surface = std::move(surface);
  }
  mesh.vertices = std::move(built.vertices);
  mesh.faces = std::move(built.faces);
  mesh.source = std::move(built.source);
}

bool freeze_mesh_recipe(mesh_geometry_t &mesh)
{
  if (!std::holds_alternative<recipe_mesh_source_t>(mesh.source))
    return false;
  mesh.source = inline_mesh_source_t{};
  return true;
}

void rebase_curve_points_to_wall_corner(primitive_recipe_t &recipe)
{
  if (recipe.curve_points.size() < 2)
    return;
  primitive_recipe_t unturned = recipe;
  unturned.quarter_turns = 0;
  const mesh_geometry_t wall = make_primitive_mesh(unturned);
  if (wall.vertices.empty())
    return;
  const linalg::vec3 corner = compute_mesh_local_bounds(wall).min;
  for (linalg::vec3 &point : recipe.curve_points)
    point = point - corner;
}

} // namespace shared
