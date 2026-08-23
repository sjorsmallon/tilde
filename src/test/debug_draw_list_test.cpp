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

#include "client/hitbox_debug_draw.hpp"
#include "client/renderer.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

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

// The hit-volume faces are wound counter-clockwise seen from OUTSIDE
// (HOUSE_FRONT_FACE), and NOTHING IN THE GAME SHOWS IT: the overlay pipeline is
// CULL_MODE_NONE, so a volume wound inside-out draws identically today. That is
// precisely why it is tested here rather than looked at -- the day a face
// pipeline starts culling, or a shader reads a real normal instead of an
// absolute one, a silent wrong winding becomes a bug with no visible cause.
//
// The check is the definition: every volume is convex, so a polygon faces
// outward exactly when its normal points away from any interior point.
void assert_faces_wound_outward(const assets::posed_hitbox_t &hitbox,
                                const linalg::vec3f &interior, const char *what)
{
  uint32_t polygons = 0;

  const auto face = [&](Span<const linalg::vec3f> corners, color_t) {
    assert(corners.size() >= 3);
    ++polygons;

    const linalg::vec3f normal =
        linalg::cross(corners[1] - corners[0], corners[2] - corners[1]);

    linalg::vec3f centroid{0, 0, 0};
    for (const linalg::vec3f &corner : corners)
      centroid = centroid + corner;
    centroid = centroid * (1.0f / (float)corners.size());

    // Not normalized on either side: only the SIGN is the claim, and a
    // degenerate polygon would divide by zero to make a nicer-looking assert.
    const float outward = linalg::dot(normal, centroid - interior);
    if (outward <= 0.0f)
    {
      std::cout << "  " << what << ": a face is wound inward (" << outward << ")" << std::endl;
      assert(false);
    }
  };

  client::draw_posed_hitbox_faces(face, hitbox, colors::white);
  assert(polygons > 0);
}

void test_hitbox_faces_are_wound_outward()
{
  assets::posed_hitbox_t sphere;
  sphere.shape  = assets::hitbox_shape_t::Sphere;
  sphere.start  = {3, -2, 1};
  sphere.radius = 0.4f;
  assert_faces_wound_outward(sphere, sphere.start, "sphere");

  // Deliberately off every world axis, so basis_around's seed choice and the
  // ring winding are exercised together rather than landing on a lucky case.
  assets::posed_hitbox_t capsule;
  capsule.shape  = assets::hitbox_shape_t::Capsule;
  capsule.start  = {0.2f, 1.0f, -0.3f};
  capsule.end    = {-0.6f, 1.7f, 0.9f};
  capsule.radius = 0.25f;
  assert_faces_wound_outward(capsule, capsule.center(), "capsule");

  // The same axis, so the only difference under test is the flat discs the
  // rounded caps are replaced by -- which are the one place the winding is
  // reversed by hand.
  assets::posed_hitbox_t cylinder = capsule;
  cylinder.shape                  = assets::hitbox_shape_t::Cylinder;
  assert_faces_wound_outward(cylinder, cylinder.center(), "cylinder");

  // A ROTATED frame, or all six rows of the face table would be checked against
  // an axis-aligned box that cannot tell right from forward.
  assets::posed_hitbox_t box;
  box.shape        = assets::hitbox_shape_t::Box;
  box.start        = {1, 2, 3};
  box.end          = {1, 2, 3};
  box.half_extents = {0.3f, 0.5f, 0.2f};
  box.frame        = {linalg::normalize(linalg::vec3f{1, 1, 0}),
                      linalg::normalize(linalg::vec3f{-1, 1, 0}), {0, 0, 1}};
  assert_faces_wound_outward(box, box.center(), "box");

  std::cout << "test_hitbox_faces_are_wound_outward passed" << std::endl;
}

// A capsule's faces have to sit on the same surface its wireframe traces, or the
// rings float off the shape they are drawn on. Both come out of the shared
// HITBOX_RING_SEGMENTS, so this is really a check that the parametrisation of
// the two agrees -- every face corner is exactly `radius` from the volume's
// medial segment.
void test_hitbox_face_corners_lie_on_the_surface()
{
  assets::posed_hitbox_t capsule;
  capsule.shape  = assets::hitbox_shape_t::Capsule;
  capsule.start  = {0.2f, 1.0f, -0.3f};
  capsule.end    = {-0.6f, 1.7f, 0.9f};
  capsule.radius = 0.25f;

  const auto face = [&](Span<const linalg::vec3f> corners, color_t) {
    for (const linalg::vec3f &corner : corners)
    {
      const float outside = assets::distance_outside_hitbox(capsule, corner);
      assert(outside < 1e-4f); // never OUTSIDE the volume it bounds

      // ...and never inside it either: a corner pulled in would be a face that
      // does not reach the wireframe ring drawn at the same angle.
      const linalg::vec3f along   = capsule.end - capsule.start;
      const float         squared = linalg::dot(along, along);
      float t = linalg::dot(corner - capsule.start, along) / squared;
      t       = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
      const float distance = linalg::length(corner - (capsule.start + along * t));
      assert(std::fabs(distance - capsule.radius) < 1e-4f);
    }
  };

  client::draw_posed_hitbox_faces(face, capsule, colors::white);

  std::cout << "test_hitbox_face_corners_lie_on_the_surface passed" << std::endl;
}

} // namespace

int main()
{
  test_default_lifetime_survives_exactly_one_frame();
  test_timed_entries_outlive_the_frame_that_made_them();
  test_retire_compacts_the_polygon_pool();
  test_compositions_bottom_out_in_lines();
  test_clear_drops_everything();
  test_hitbox_faces_are_wound_outward();
  test_hitbox_face_corners_lie_on_the_surface();

  std::cout << "All debug_draw_list tests passed!" << std::endl;
  return 0;
}
