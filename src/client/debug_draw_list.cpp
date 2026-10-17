// debug_draw_list_t: plain storage with typed appends, and NO Vulkan.
//
// Split out of renderer.cpp on purpose. Every composition here bottoms out in
// `lines`, `polygons` or `texts`, so the recorder only ever sees three flat
// arrays however the caller spelled the shape -- and none of that needs a
// device, a swapchain or a command buffer. Keeping it in its own translation
// unit is what lets debug_draw_list_test link without the client DLL, which is
// the same reason the editor tools can be exercised with no GPU.

#include "debug_draw_list.hpp"

#include "../shared/array.hpp"

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

constexpr Array<Array<int32_t, 2>, 12> BOX_EDGES = {{{0, 1}, {1, 2}, {2, 3}, {3, 0},
                                                     {4, 5}, {5, 6}, {6, 7}, {7, 4},
                                                     {0, 4}, {1, 5}, {2, 6}, {3, 7}}};

linalg::vec3f construct_perpendicular_vector(const linalg::vec3f& axis)
{
  return std::fabs(axis.y) < 0.9f ? linalg::vec3f{0, 1, 0} : linalg::vec3f{1, 0, 0};
}

// in a previous life, I would have never understood why this is nice. I do now.
template <typename debug_draw_type>
bool retire_expired_draw_instructions(std::vector<debug_draw_type>& debug_draw_shape_array, float delta_seconds)
{
  const size_t before = debug_draw_shape_array.size();
  for (auto& entry : debug_draw_shape_array)
  {
    entry.remaining_seconds -= delta_seconds;
  }

  std::erase_if(debug_draw_shape_array, [](const debug_draw_type& entry) { return entry.remaining_seconds <= 0.0f; });
  return debug_draw_shape_array.size() != before;
}

} // namespace

void debug_draw_list_t::advance_retire_timers(float delta_seconds)
{
  retire_expired_draw_instructions(lines, delta_seconds);
  retire_expired_draw_instructions(texts, delta_seconds);

  if (retire_expired_draw_instructions(polygons, delta_seconds))
  {
    // The survivors' vertices have to be compacted with them, or first_vertex
    // points into the wrong slice. Rebuilding the pool keeps the invariant
    // local to this one place.
    auto survivors = std::vector<linalg::vec3f>{};
    survivors.reserve(polygon_vertices.size());
    for (debug_polygon_t &polygon : polygons)
    {
      const uint32_t destination_idx = (uint32_t)survivors.size();
      survivors.insert(survivors.end(), polygon_vertices.begin() + polygon.first_vertex,
                       polygon_vertices.begin() + polygon.first_vertex + polygon.vertex_count);
      polygon.first_vertex = destination_idx;
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

void debug_draw_list_t::line(const linalg::vec3f& start, const linalg::vec3f& end, color_t color,
                             float depth_bias, float duration_seconds, bool draw_when_occluded)
{
  lines.push_back(debug_line_t{
    .start = start,
    .end = end,
    .color = color,
    .depth_bias = depth_bias,
    .remaining_seconds = duration_seconds,
    .draw_when_occluded = draw_when_occluded});
}

void debug_draw_list_t::aabb(const linalg::vec3f& min, const linalg::vec3f& max, color_t color,
                             fill_mode_t fill, float depth_bias, float duration_seconds,
                             bool draw_when_occluded)
{
  const Array<linalg::vec3f, 8> corners = {{
      {min.x, min.y, min.z}, {max.x, min.y, min.z}, {max.x, max.y, min.z}, {min.x, max.y, min.z},
      {min.x, min.y, max.z}, {max.x, min.y, max.z}, {max.x, max.y, max.z}, {min.x, max.y, max.z}}};

  // ah,  we need to add a filled polygon for each face.
  if (fill == fill_mode_t::solid)
  {
    
    static constexpr Array<Array<int32_t, 4>, 6> FACES = {{{3, 2, 1, 0}, {6, 7, 4, 5},
                                                            {7, 3, 0, 4}, {2, 6, 5, 1},
                                                            {7, 6, 2, 3}, {0, 1, 5, 4}}};
    for (const Array<int32_t, 4>& face : FACES)
    {
      const Array<linalg::vec3f, 4> quad = {
          {corners[face[0]], corners[face[1]], corners[face[2]], corners[face[3]]}};
      filled_polygon(quad, color, duration_seconds,
                     {.shaded = true, .draw_when_occluded = draw_when_occluded});
    }
  }

  for (const Array<int32_t, 2>& edge : BOX_EDGES)
    line(corners[edge[0]], corners[edge[1]], color, depth_bias, duration_seconds,
         draw_when_occluded);
}

void debug_draw_list_t::box(const linalg::vec3f& center, const linalg::vec3f& half_extents,
                            color_t color, fill_mode_t fill, float depth_bias,
                            float duration_seconds, bool draw_when_occluded)
{
  aabb(center - half_extents, center + half_extents, color, fill, depth_bias, duration_seconds,
       draw_when_occluded);
}

void debug_draw_list_t::oriented_box(const shared::oriented_box_t& box, color_t color,
                                     float depth_bias, float duration_seconds,
                                     bool draw_when_occluded)
{
  const Array<linalg::vec3f, 8> corners = shared::get_oriented_box_corners(box);
  for (const Array<int32_t, 2>& edge : BOX_EDGES)
  {
    line(corners[edge[0]], corners[edge[1]], color, depth_bias, duration_seconds,
         draw_when_occluded);
  }
    
}

void debug_draw_list_t::filled_polygon(Span<const linalg::vec3f> vertices, color_t color,
                                       float duration_seconds, debug_face_style_t style)
{
  if (vertices.size() < 3)
  {
    log_warning("trying to draw a filled polygon that's less than three vertices.");
    return;
  }

  const uint32_t first = (uint32_t)polygon_vertices.size();
  polygon_vertices.insert(polygon_vertices.end(), vertices.begin(), vertices.end());
  polygons.push_back({first, (uint32_t)vertices.size(), color, duration_seconds, style});
}

void debug_draw_list_t::arrow(const linalg::vec3f& start, const linalg::vec3f& end, color_t color,
                              float duration_seconds, bool draw_when_occluded)
{
  // shaft
  line(start, end, color, 0.0f, duration_seconds, draw_when_occluded);

  const linalg::vec3f along  = end - start;
  const float length = linalg::length(along);

  if (length < 1e-4f) return;

  const linalg::vec3f axis = along * (1.0f / length);
  const linalg::vec3f side = linalg::normalize(linalg::cross(axis, construct_perpendicular_vector(axis)));
  const linalg::vec3f up = linalg::cross(axis, side);

  // Four barbs rather than two, so the head reads from any angle instead of
  // collapsing to a line when you look down the plane it was drawn in.
  const float head_length = std::min(length * 0.25f, 16.0f);
  const float head_radius = head_length * 0.4f;
  const linalg::vec3f base = end - axis * head_length;
  const Array<linalg::vec3f, 4> arrow_head_offsets = {
      {side * head_radius, side * -head_radius, up * head_radius, up * -head_radius}};
  for (const linalg::vec3f& offset : arrow_head_offsets)
  {
    line(end, base + offset, color, 0.0f, duration_seconds, draw_when_occluded);
  }
    
}

void debug_draw_list_t::wire_circle(const linalg::vec3f& center, float radius,
                                    const linalg::vec3f& normal, color_t color,
                                    float duration_seconds)
{
  const linalg::vec3f axis = linalg::normalize(normal);
  const linalg::vec3f side = linalg::normalize(linalg::cross(axis, construct_perpendicular_vector(axis)));
  const linalg::vec3f up = linalg::cross(axis, side);

  linalg::vec3f previous = center + side * radius;
  // advance in a circle.
  for (uint32_t step = 1; step <= CIRCLE_SEGMENTS; ++step)
  {
    const float angle = (float)step / (float)CIRCLE_SEGMENTS * 2.0f * linalg::PI;
    const linalg::vec3f point = center + side * (std::cos(angle) * radius) + up * (std::sin(angle) * radius);
    line(previous, point, color, 0.0f, duration_seconds);
    previous = point;
  }
}

void debug_draw_list_t::wire_sphere(const linalg::vec3f& center, float radius, color_t color,
                                    float duration_seconds)
{
  // Three great circles -- enough to read as a sphere from any angle.
  wire_circle(center, radius, {0, 1, 0}, color, duration_seconds);
  wire_circle(center, radius, {1, 0, 0}, color, duration_seconds);
  wire_circle(center, radius, {0, 0, 1}, color, duration_seconds);
}

void debug_draw_list_t::wire_capsule(const linalg::vec3f& center, float radius, float half_height,
                                     color_t color, float duration_seconds)
{
  // A ring at each end, four lines down the sides, and rings THROUGH the ends so
  // the caps read as rounded -- which is the whole visible difference from a
  // cylinder, and the whole difference in the hit test too.
  const linalg::vec3f bottom = {center.x, center.y - half_height, center.z};
  const linalg::vec3f top = {center.x, center.y + half_height, center.z};

  wire_circle(bottom, radius, {0, 1, 0}, color, duration_seconds);
  wire_circle(top, radius, {0, 1, 0}, color, duration_seconds);
  wire_circle(bottom, radius, {1, 0, 0}, color, duration_seconds);
  wire_circle(top, radius, {0, 0, 1}, color, duration_seconds);

  for (const linalg::vec3f& offset : {linalg::vec3f{radius, 0, 0}, linalg::vec3f{-radius, 0, 0},
                                      linalg::vec3f{0, 0, radius}, linalg::vec3f{0, 0, -radius}})
  {
    line(bottom + offset, top + offset, color, 0.0f, duration_seconds);
  }
}

void debug_draw_list_t::text(const linalg::vec3f& world_position, const char *text, color_t color,
                             float duration_seconds)
{
  texts.push_back({.position          = world_position,
                   .text              = text,
                   .color             = color,
                   .remaining_seconds = duration_seconds});
}

void debug_draw_list_t::backed_text(const linalg::vec3f& world_position, const char *text,
                                    color_t color, float duration_seconds)
{
  texts.push_back({.position          = world_position,
                   .text              = text,
                   .color             = color,
                   .remaining_seconds = duration_seconds,
                   .backed            = true});
}

} // namespace renderer
} // namespace client
