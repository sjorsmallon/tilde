#pragma once

// The debug draw list: plain storage with typed appends, and NO Vulkan. It is
// the renderer's one GPU-free piece, so it has its own header and translation
// unit (debug_draw_list.cpp) and is tested without a device. renderer.hpp
// includes this; nothing here includes renderer.hpp.

#include <cstdint>
#include <string>
#include <vector>

#include "../shared/aabb.hpp"
#include "../shared/color.hpp"
#include "../shared/linalg.hpp"
#include "../shared/span.hpp"

namespace client
{
namespace renderer
{

enum class fill_mode_t : uint8_t
{
  solid,
  wireframe
};

struct debug_line_t
{
  linalg::vec3f start{};
  linalg::vec3f end{};
  color_t color{};
  float depth_bias{};
  float remaining_seconds{}; 
  bool draw_when_occluded{};
};


struct debug_face_style_t
{
  bool shaded = false;
  bool draw_when_occluded = false;
  bool rim = false;
  float depth_bias = 0.0f;
};

struct debug_polygon_t
{
  uint32_t first_vertex;
  uint32_t vertex_count;
  color_t color;
  float remaining_seconds;
  debug_face_style_t  style;
};

struct debug_text_t
{
  linalg::vec3f position;
  std::string text;
  color_t color;
  float remaining_seconds;
  bool backed = false;
};

struct debug_draw_list_t
{
  std::vector<debug_line_t> lines{};
  std::vector<linalg::vec3f> polygon_vertices{}; // one pool, sliced by polygons
  std::vector<debug_polygon_t> polygons{};
  std::vector<debug_text_t> texts{};


  // call this once at frame start to retire things that are done.
  void advance_retire_timers(float delta_seconds);
  
  void clear(); // wholesale drop (map switch, state exit)


  void line(const linalg::vec3f& start, const linalg::vec3f& end, color_t color,
            float depth_bias = 0.0f, float duration_seconds = 0.0f,
            bool draw_when_occluded = false);

  // same thing, different spelling.
  void aabb(const linalg::vec3f& min, const linalg::vec3f& max, color_t color,
            fill_mode_t fill = fill_mode_t::wireframe, float depth_bias = 0.0f,
            float duration_seconds = 0.0f, bool draw_when_occluded = false);
  void box(const linalg::vec3f& center, const linalg::vec3f& half_extents, color_t color,
           fill_mode_t fill = fill_mode_t::wireframe, float depth_bias = 0.0f,
           float duration_seconds = 0.0f, bool draw_when_occluded = false);

  // similar but not quite.
  void oriented_box(const shared::oriented_box_t& box, color_t color, float depth_bias = 0.0f,
                    float duration_seconds = 0.0f, bool draw_when_occluded = false);
  // triangle fan.
  //Any convex polygon may be triangulated as a single fan, by arbitrarily selecting any point inside it as the center. (https://en.wikipedia.org/wiki/Triangle_fan)
  void filled_polygon(Span<const linalg::vec3f> vertices, color_t color,
                      float duration_seconds = 0.0f, debug_face_style_t style = {});


  // these decompose into lines, but are nice-to-have.
  void arrow(const linalg::vec3f& start, const linalg::vec3f& end, color_t color,
             float duration_seconds = 0.0f, bool draw_when_occluded = false);
  void wire_circle(const linalg::vec3f& center, float radius, const linalg::vec3f& normal,
                   color_t color, float duration_seconds = 0.0f);
  void wire_sphere(const linalg::vec3f& center, float radius, color_t color,
                   float duration_seconds = 0.0f);
  void wire_capsule(const linalg::vec3f& center, float radius, float half_height, color_t color,
                    float duration_seconds = 0.0f);

  
  // composed over the 3d scene, so no need for depth bias. off-screen text is skipped entirely.
  void text(const linalg::vec3f& world_position, const char *text, color_t color,
            float duration_seconds = 0.0f);

  // text with a background.
  void backed_text(const linalg::vec3f& world_position, const char *text, color_t color,
                   float duration_seconds = 0.0f);
};

} // namespace renderer
} // namespace client
