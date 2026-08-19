// ui_draw_list_t's append logic: the second piece of the renderer with real
// logic and no GPU in it. It lives in its own translation unit for the same
// reason debug_draw_list.cpp does -- batching and vertex emission are testable
// without a device, a swapchain or a window, and ui_test compiles this file
// directly rather than linking the client DLL.
//
// The type itself is declared in renderer.hpp because render_frame consumes it.

#include "renderer.hpp"

namespace client
{
namespace renderer
{

void ui_draw_list_t::clear()
{
  // Capacity is the point: the HUD rebuilds every frame from state, so these two
  // vectors are refilled at frame rate and should stop allocating after the
  // first few frames.
  vertices.clear();
  batches.clear();
}

void ui_draw_list_t::quad(linalg::vec2 min, linalg::vec2 max, linalg::vec2 uv_min,
                          linalg::vec2 uv_max, color_t color, texture_handle_t texture)
{
  const uint32_t first_vertex = (uint32_t)vertices.size();

  // Extend the open batch when the texture matches. A string of glyphs all comes
  // from one atlas, so this is what makes a line of text one draw call rather
  // than one per character.
  if (!batches.empty() && batches.back().texture == texture)
    batches.back().vertex_count += 6;
  else
    batches.push_back(ui_batch_t{texture, first_vertex, 6});

  const uint32_t packed = to_abgr(color);

  // Two triangles, counter-clockwise seen on screen with y growing DOWNWARD --
  // which is the winding the UI pipeline is built with. It does not follow
  // HOUSE_FRONT_FACE's 3D convention because there is no outside to be seen
  // from; the pipeline simply disables culling, so this ordering is about
  // nothing but covering the rectangle.
  vertices.push_back(ui_vertex_t{{min.x, min.y}, {uv_min.x, uv_min.y}, packed});
  vertices.push_back(ui_vertex_t{{max.x, min.y}, {uv_max.x, uv_min.y}, packed});
  vertices.push_back(ui_vertex_t{{max.x, max.y}, {uv_max.x, uv_max.y}, packed});

  vertices.push_back(ui_vertex_t{{min.x, min.y}, {uv_min.x, uv_min.y}, packed});
  vertices.push_back(ui_vertex_t{{max.x, max.y}, {uv_max.x, uv_max.y}, packed});
  vertices.push_back(ui_vertex_t{{min.x, max.y}, {uv_min.x, uv_max.y}, packed});
}

void ui_draw_list_t::rect(linalg::vec2 min, linalg::vec2 max, color_t color)
{
  // UVs are irrelevant against a 1x1 white texture, but they have to be
  // SOMETHING; (0,0)-(1,1) keeps a batch of rects mergeable with itself and
  // makes the vertices readable in a capture.
  quad(min, max, {0.0f, 0.0f}, {1.0f, 1.0f}, color, texture_handle_t{});
}

} // namespace renderer
} // namespace client
