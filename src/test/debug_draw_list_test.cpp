// debug_draw_list_t is the one piece of the renderer with real logic that needs
// no GPU: what retire() keeps, what it drops, and whether the polygon pool stays
// consistent when entries in the middle of it die.
//
// Those are exactly the semantics the two debug-draw LIFETIMES rest on -- a
// per-frame entry that must die next frame, and a one-shot event entry that must
// not -- so they are worth a test rather than a play-through.

// renderer.hpp pulls in SDL.h for the lifecycle signatures, and SDL rewrites
// `main` unless told not to. This test owns its own entry point.
#define SDL_MAIN_HANDLED

#include "client/renderer.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using client::renderer::debug_draw_list_t;

namespace
{

constexpr float FRAME = 1.0f / 60.0f;

void test_default_lifetime_survives_exactly_one_frame()
{
  debug_draw_list_t debug;

  debug.line({0, 0, 0}, {1, 0, 0}, colors::red);
  assert(debug.lines.size() == 1);

  // The frame it was appended in is the frame it draws in. The NEXT retire kills
  // it -- that is the old clear-per-frame behaviour, spelled as a lifetime.
  debug.retire(FRAME);
  assert(debug.lines.empty());

  std::cout << "test_default_lifetime_survives_exactly_one_frame passed" << std::endl;
}

void test_timed_entries_outlive_the_frame_that_made_them()
{
  debug_draw_list_t debug;

  // A hitscan trace fires in a fixed tick, not in a render frame. With a
  // per-frame lifetime it would be visible for one frame, i.e. invisible.
  debug.line({0, 0, 0}, {1, 0, 0}, colors::red, /*depth_bias*/ 0.0f, /*seconds*/ 0.25f);

  // A tenth of a second a step, so the boundary lands nowhere near a float
  // rounding edge: two steps leave 0.05 and the third takes it under.
  constexpr float STEP = 0.1f;

  int frames = 0;
  while (!debug.lines.empty())
  {
    debug.retire(STEP);
    ++frames;
    assert(frames < 1000); // it must actually expire
  }

  assert(frames == 3);

  std::cout << "test_timed_entries_outlive_the_frame_that_made_them passed" << std::endl;
}

void test_retire_compacts_the_polygon_pool()
{
  debug_draw_list_t debug;

  const linalg::vec3f triangle[3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  const linalg::vec3f quad[4]     = {{0, 0, 5}, {1, 0, 5}, {1, 1, 5}, {0, 1, 5}};

  // Middle one dies; the survivors' slices have to move with them, or
  // first_vertex points into the wrong part of the pool.
  debug.filled_polygon(Span<const linalg::vec3f>(quad, 4), colors::green, /*seconds*/ 1.0f);
  debug.filled_polygon(Span<const linalg::vec3f>(triangle, 3), colors::red);
  debug.filled_polygon(Span<const linalg::vec3f>(quad, 4), colors::blue, /*seconds*/ 1.0f);

  assert(debug.polygons.size() == 3);
  assert(debug.polygon_vertices.size() == 11);

  debug.retire(FRAME);

  assert(debug.polygons.size() == 2);
  assert(debug.polygon_vertices.size() == 8);

  for (const client::renderer::debug_polygon_t &polygon : debug.polygons)
  {
    assert(polygon.first_vertex + polygon.vertex_count <= debug.polygon_vertices.size());
    // Both survivors are the quad at z = 5.
    for (uint32_t index = 0; index < polygon.vertex_count; ++index)
      assert(debug.polygon_vertices[polygon.first_vertex + index].z == 5.0f);
  }

  std::cout << "test_retire_compacts_the_polygon_pool passed" << std::endl;
}

void test_compositions_bottom_out_in_lines()
{
  debug_draw_list_t debug;

  // A wireframe box is twelve edges and nothing else -- the recorder only ever
  // sees flat arrays, however the caller spelled the shape.
  debug.aabb({0, 0, 0}, {1, 1, 1}, colors::white);
  assert(debug.lines.size() == 12);
  assert(debug.polygons.empty());

  // A solid one is six faces PLUS those edges.
  debug.clear();
  debug.box({0, 0, 0}, {1, 1, 1}, colors::white, client::renderer::fill_mode_t::solid);
  assert(debug.polygons.size() == 6);
  assert(debug.lines.size() == 12);

  debug.clear();
  debug.wire_sphere({0, 0, 0}, 1.0f, colors::white);
  assert(debug.lines.size() == 3 * 16); // three great circles, sixteen segments each

  debug.clear();
  debug.arrow({0, 0, 0}, {0, 0, 100}, colors::white);
  assert(debug.lines.size() == 5); // the shaft plus four barbs

  std::cout << "test_compositions_bottom_out_in_lines passed" << std::endl;
}

void test_clear_drops_everything()
{
  debug_draw_list_t debug;

  const linalg::vec3f triangle[3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  debug.line({0, 0, 0}, {1, 0, 0}, colors::red, 0.0f, 10.0f);
  debug.filled_polygon(Span<const linalg::vec3f>(triangle, 3), colors::green, 10.0f);
  debug.text({0, 0, 0}, "label", colors::white, 10.0f);

  // Even long-lived entries go: a map switch invalidates the world they name.
  debug.clear();
  assert(debug.lines.empty());
  assert(debug.polygons.empty());
  assert(debug.polygon_vertices.empty());
  assert(debug.texts.empty());

  std::cout << "test_clear_drops_everything passed" << std::endl;
}

} // namespace

int main()
{
  test_default_lifetime_survives_exactly_one_frame();
  test_timed_entries_outlive_the_frame_that_made_them();
  test_retire_compacts_the_polygon_pool();
  test_compositions_bottom_out_in_lines();
  test_clear_drops_everything();

  std::cout << "All debug_draw_list tests passed!" << std::endl;
  return 0;
}
