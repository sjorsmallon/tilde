// debug_draw_list_t: plain storage with typed appends, and NO Vulkan.
//
// Split out of renderer.cpp on purpose. Every composition here bottoms out in
// `lines`, `polygons` or `texts`, so the recorder only ever sees three flat
// arrays however the caller spelled the shape -- and none of that needs a
// device, a swapchain or a command buffer. Keeping it in its own translation
// unit is what lets debug_draw_list_test link without the client DLL, which is
// the same reason the editor tools can be exercised with no GPU.

#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace client
{
namespace renderer
{


namespace
{

constexpr uint32_t CIRCLE_SEGMENTS = 16;

// Any vector not parallel to the axis works; picking the world axis the shape is
// least aligned with keeps the cross product well conditioned.
linalg::vec3f perpendicular_seed(const linalg::vec3f &axis)
{
  return std::fabs(axis.y) < 0.9f ? linalg::vec3f{0, 1, 0} : linalg::vec3f{1, 0, 0};
}

// Drop entries whose time has run out. Returns true if anything went, so the
// polygon pool can be rebuilt only when it has to be.
template <typename Entry_T>
bool retire_entries(std::vector<Entry_T> &entries, float delta_seconds)
{
  const size_t before = entries.size();
  for (Entry_T &entry : entries)
    entry.remaining_seconds -= delta_seconds;

  std::erase_if(entries, [](const Entry_T &entry) { return entry.remaining_seconds <= 0.0f; });
  return entries.size() != before;
}

} // namespace

void debug_draw_list_t::retire(float delta_seconds)
{
  retire_entries(lines, delta_seconds);
  retire_entries(texts, delta_seconds);

  if (retire_entries(polygons, delta_seconds))
  {
    // The survivors' vertices have to be compacted with them, or first_vertex
    // points into the wrong slice. Rebuilding the pool keeps the invariant
    // local to this one place.
    std::vector<linalg::vec3f> survivors;
    survivors.reserve(polygon_vertices.size());
    for (debug_polygon_t &polygon : polygons)
    {
      const uint32_t moved_to = (uint32_t)survivors.size();
      survivors.insert(survivors.end(), polygon_vertices.begin() + polygon.first_vertex,
                       polygon_vertices.begin() + polygon.first_vertex + polygon.vertex_count);
      polygon.first_vertex = moved_to;
    }
    polygon_vertices.swap(survivors);
  }
}

void debug_draw_list_t::clear()
{
  lines.clear();
  polygon_vertices.clear();
  polygons.clear();
  texts.clear();
}

void debug_draw_list_t::line(const linalg::vec3f &start, const linalg::vec3f &end, color_t color,
                             float depth_bias, float seconds)
{
  lines.push_back({start, end, color, depth_bias, seconds});
}

void debug_draw_list_t::aabb(const linalg::vec3f &min, const linalg::vec3f &max, color_t color,
                             fill_mode_t fill, float depth_bias, float seconds)
{
  const linalg::vec3f corners[8] = {
      {min.x, min.y, min.z}, {max.x, min.y, min.z}, {max.x, max.y, min.z}, {min.x, max.y, min.z},
      {min.x, min.y, max.z}, {max.x, min.y, max.z}, {max.x, max.y, max.z}, {min.x, max.y, max.z}};

  // A solid box is six quads PLUS the twelve edges: the faces alone read as a
  // silhouette from anything but a corner-on angle.
  if (fill == fill_mode_t::solid)
  {
    static constexpr int FACES[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7},
                                        {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
    for (const int *face : FACES)
    {
      const linalg::vec3f quad[4] = {corners[face[0]], corners[face[1]], corners[face[2]],
                                     corners[face[3]]};
      filled_polygon(Span<const linalg::vec3f>(quad, 4), color, seconds, /*shaded*/ true);
    }
  }

  static constexpr int EDGES[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                       {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  for (const int *edge : EDGES)
    line(corners[edge[0]], corners[edge[1]], color, depth_bias, seconds);
}

void debug_draw_list_t::box(const linalg::vec3f &center, const linalg::vec3f &half_extents,
                            color_t color, fill_mode_t fill, float depth_bias, float seconds)
{
  aabb(center - half_extents, center + half_extents, color, fill, depth_bias, seconds);
}

void debug_draw_list_t::filled_polygon(Span<const linalg::vec3f> vertices, color_t color,
                                       float seconds, bool shaded)
{
  if (vertices.size() < 3)
    return;

  const uint32_t first = (uint32_t)polygon_vertices.size();
  polygon_vertices.insert(polygon_vertices.end(), vertices.begin(), vertices.end());
  polygons.push_back({first, (uint32_t)vertices.size(), color, seconds, shaded});
}

void debug_draw_list_t::arrow(const linalg::vec3f &start, const linalg::vec3f &end, color_t color,
                              float seconds)
{
  line(start, end, color, 0.0f, seconds);

  const linalg::vec3f along  = end - start;
  const float         length = linalg::length(along);
  if (length < 1e-4f)
    return;

  const linalg::vec3f axis = along * (1.0f / length);
  const linalg::vec3f side = linalg::normalize(linalg::cross(axis, perpendicular_seed(axis)));
  const linalg::vec3f up   = linalg::cross(axis, side);

  // Four barbs rather than two, so the head reads from any angle instead of
  // collapsing to a line when you look down the plane it was drawn in.
  const float head_length = std::min(length * 0.25f, 16.0f);
  const float head_radius = head_length * 0.4f;
  const linalg::vec3f base = end - axis * head_length;
  for (const linalg::vec3f &offset : {side * head_radius, side * -head_radius, up * head_radius,
                                      up * -head_radius})
    line(end, base + offset, color, 0.0f, seconds);
}

void debug_draw_list_t::wire_circle(const linalg::vec3f &center, float radius,
                                    const linalg::vec3f &normal, color_t color, float seconds)
{
  const linalg::vec3f axis = linalg::normalize(normal);
  const linalg::vec3f side = linalg::normalize(linalg::cross(axis, perpendicular_seed(axis)));
  const linalg::vec3f up   = linalg::cross(axis, side);

  linalg::vec3f previous = center + side * radius;
  for (uint32_t step = 1; step <= CIRCLE_SEGMENTS; ++step)
  {
    const float         angle = (float)step / (float)CIRCLE_SEGMENTS * 2.0f * linalg::PI;
    const linalg::vec3f point =
        center + side * (std::cos(angle) * radius) + up * (std::sin(angle) * radius);
    line(previous, point, color, 0.0f, seconds);
    previous = point;
  }
}

void debug_draw_list_t::wire_sphere(const linalg::vec3f &center, float radius, color_t color,
                                    float seconds)
{
  // Three great circles -- enough to read as a sphere from any angle.
  wire_circle(center, radius, {0, 1, 0}, color, seconds);
  wire_circle(center, radius, {1, 0, 0}, color, seconds);
  wire_circle(center, radius, {0, 0, 1}, color, seconds);
}

void debug_draw_list_t::wire_capsule(const linalg::vec3f &center, float radius, float half_height,
                                     color_t color, float seconds)
{
  // A ring at each end, four lines down the sides, and rings THROUGH the ends so
  // the caps read as rounded -- which is the whole visible difference from a
  // cylinder, and the whole difference in the hit test too.
  const linalg::vec3f bottom = {center.x, center.y - half_height, center.z};
  const linalg::vec3f top    = {center.x, center.y + half_height, center.z};

  wire_circle(bottom, radius, {0, 1, 0}, color, seconds);
  wire_circle(top, radius, {0, 1, 0}, color, seconds);
  wire_circle(bottom, radius, {1, 0, 0}, color, seconds);
  wire_circle(top, radius, {0, 0, 1}, color, seconds);

  for (const linalg::vec3f &offset : {linalg::vec3f{radius, 0, 0}, linalg::vec3f{-radius, 0, 0},
                                      linalg::vec3f{0, 0, radius}, linalg::vec3f{0, 0, -radius}})
    line(bottom + offset, top + offset, color, 0.0f, seconds);
}

void debug_draw_list_t::text(const linalg::vec3f &world_position, const char *text, color_t color,
                             float seconds)
{
  texts.push_back({world_position, text, color, seconds});
}

} // namespace renderer
} // namespace client
