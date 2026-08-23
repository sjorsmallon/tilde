// The UI layer's two pieces of real logic, both GPU-free: the font bake and
// ui_draw_list_t's vertex/batch emission, plus the layout anchors.
//
// It bakes the REAL resources/fonts TTF rather than a fixture, because the thing
// worth guarding is that the shipped font produces sane metrics -- a bake that
// silently returns zero advances draws nothing and looks exactly like a
// rendering bug.
//
// Like debug_draw_list_test, this compiles the split translation units instead
// of linking game_client, which is what makes "no device, no swapchain, no
// window" true.

// renderer.hpp pulls in SDL.h for the lifecycle signatures, and SDL rewrites
// `main` unless told not to. This test owns its own entry point.
#define SDL_MAIN_HANDLED

#include "client/ui/font.hpp"
#include "client/ui/layout.hpp"
#include "client/ui/list_menu.hpp"
#include "client/ui/navigation.hpp"
#include "client/ui/screen.hpp"
#include "shared/asset.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string_view>

using client::renderer::ui_draw_list_t;
using client::ui::add_node;
using client::ui::anchor_t;
using client::ui::anchored;
using client::ui::ease_t;
using client::ui::font_atlas_t;
using client::ui::font_size_t;
using client::ui::glyph_t;
using client::ui::inset;
using client::ui::measure_text;
using client::ui::nav_direction_t;
using client::ui::PRINTABLE_ASCII_COUNT;
using client::ui::text_align_t;
using client::ui::ui_font_t;
using client::ui::ui_node_id_t;
using client::ui::ui_property_t;
using client::ui::ui_rect_t;
using client::ui::ui_solid_content_t;
using client::ui::ui_text_content_t;
using client::ui::ui_screen_t;
using client::ui::UI_INVALID_NODE_ID;
using client::ui::list_menu_style_t;
using client::ui::list_menu_t;
using client::ui::ui_input_t;

namespace
{

constexpr float SMALL_HEIGHT  = 18.0f;
constexpr float MEDIUM_HEIGHT = 28.0f;
constexpr float LARGE_HEIGHT  = 48.0f;

// The atlas DIMENSIONS travel with the font because one test needs them: UVs are
// normalized at bake time, so checking that a glyph covers as many pixels as it
// does texels means multiplying them back out.
struct baked_font_t
{
  ui_font_t font;
  float     atlas_width  = 0;
  float     atlas_height = 0;
};

// Baked once and shared: the pack is the slow part of this test and nothing
// below mutates it.
// The byte layer is the only way to read a file now, and it hangs off the one
// launcher-owned state -- so a test that reads anything has to own that state
// and mount it, exactly as a launcher does. This test still links no device, no
// swapchain and no window.
assets::asset_state_t &test_asset_state()
{
  static assets::asset_state_t state = [] {
    assets::asset_state_t fresh;
    return fresh;
  }();
  static const bool mounted = [] {
    assets::set_state(&state);
    assets::mount_asset_source();
    return true;
  }();
  (void)mounted;
  return state;
}

const baked_font_t &shipped()
{
  static const baked_font_t baked = [] {
    test_asset_state();
    const std::optional<font_atlas_t> atlas = client::ui::try_bake_font(
        assets::read_asset_bytes(client::ui::DEFAULT_FONT_PATH),
        {SMALL_HEIGHT, MEDIUM_HEIGHT, LARGE_HEIGHT});
    if (!atlas)
    {
      std::cerr << "could not bake " << client::ui::DEFAULT_FONT_PATH
                << " -- run from the project root" << std::endl;
      std::abort();
    }

    baked_font_t result;
    // A made-up atlas handle. Nothing here uploads, and the draw path only ever
    // COMPARES the handle -- but it has to be distinct from the invalid handle
    // ui_draw_list_t::rect uses, or text and solid rects would merge into one
    // batch and the batching test would pass for the wrong reason.
    result.font.atlas.index = 7;
    result.font.sizes       = atlas->sizes;
    result.atlas_width      = (float)atlas->image.width;
    result.atlas_height     = (float)atlas->image.height;
    return result;
  }();
  return baked;
}

const ui_font_t &shipped_font() { return shipped().font; }

uint32_t glyph_index(char character)
{
  return (uint32_t)(unsigned char)character - client::ui::FIRST_PRINTABLE_ASCII;
}

void test_bake_produces_sane_metrics()
{
  const ui_font_t &font = shipped_font();

  for (uint32_t size_index = 0; size_index < client::ui::FONT_SIZE_COUNT; ++size_index)
  {
    const client::ui::font_metrics_t &metrics = font.sizes.values[size_index];

    assert(metrics.pixel_height > 0.0f);
    assert(metrics.ascent > 0.0f);
    assert(metrics.descent > 0.0f);
    // A line has to be at least tall enough to hold the ink it contains, or
    // stacked rows overlap.
    assert(metrics.line_height >= metrics.ascent + metrics.descent);

    for (uint32_t index = 0; index < PRINTABLE_ASCII_COUNT; ++index)
    {
      const glyph_t &glyph = metrics.glyphs[index];

      // Every printable codepoint advances the pen. A zero here is the failure
      // mode this whole test exists for: text that draws as nothing.
      //
      // Named before it is asserted, because the bare assert says only that SOME
      // glyph at SOME size is dead -- and the way this actually fails is one
      // codepoint at two of the three sizes (stb's missing-glyph latch; see
      // font.cpp's pack loop), which is a one-run diagnosis with the codepoint
      // and a multi-hour one without it.
      if (glyph.x_advance <= 0.0f)
        std::cerr << "zero advance: size index " << size_index << ", codepoint "
                  << (index + client::ui::FIRST_PRINTABLE_ASCII) << " ('"
                  << (char)(index + client::ui::FIRST_PRINTABLE_ASCII) << "')" << std::endl;
      assert(glyph.x_advance > 0.0f);

      // UVs stay inside the atlas, and are not inverted.
      assert(glyph.u0 >= 0.0f && glyph.u0 <= 1.0f);
      assert(glyph.v0 >= 0.0f && glyph.v0 <= 1.0f);
      assert(glyph.u1 >= glyph.u0 && glyph.u1 <= 1.0f);
      assert(glyph.v1 >= glyph.v0 && glyph.v1 <= 1.0f);
    }
  }

  std::cout << "test_bake_produces_sane_metrics passed" << std::endl;
}

void test_space_advances_but_has_no_ink()
{
  const client::ui::font_metrics_t &metrics = shipped_font().sizes[font_size_t::medium];
  const glyph_t                    &space   = metrics.glyphs[glyph_index(' ')];

  assert(space.x_advance > 0.0f);
  // Zero-area in the atlas, which is what draw_text's ink check keys off to
  // avoid emitting six vertices per space.
  assert(space.u1 <= space.u0 || space.v1 <= space.v0);

  std::cout << "test_space_advances_but_has_no_ink passed" << std::endl;
}

void test_measure_text()
{
  const ui_font_t                  &font    = shipped_font();
  const client::ui::font_metrics_t &metrics = font.sizes[font_size_t::medium];

  const linalg::vec2 empty = measure_text(font, font_size_t::medium, "");
  assert(empty.x == 0.0f);
  // Height is the LINE BOX, not the ink: an empty string still occupies a row,
  // which is what keeps a counter from changing height as it ticks.
  assert(empty.y == metrics.line_height);

  const float expected = metrics.glyphs[glyph_index('a')].x_advance +
                         metrics.glyphs[glyph_index('b')].x_advance;
  const linalg::vec2 measured = measure_text(font, font_size_t::medium, "ab");
  assert(std::fabs(measured.x - expected) < 0.001f);

  // Same string, bigger size, wider. If this ever fails the sizes got baked from
  // one pixel height.
  assert(measure_text(font, font_size_t::large, "ab").x > measured.x);
  assert(measure_text(font, font_size_t::small, "ab").x < measured.x);

  // Unbaked bytes contribute nothing rather than tripping an out-of-range read.
  assert(measure_text(font, font_size_t::medium, "a\tb").x == measured.x);

  std::cout << "test_measure_text passed" << std::endl;
}

void test_draw_text_emits_one_batch_and_skips_spaces()
{
  const ui_font_t &font = shipped_font();

  ui_draw_list_t list;
  client::ui::draw_text(list, font, font_size_t::medium, {10.0f, 10.0f}, "ab", colors::white);

  // Six vertices per printable glyph, non-indexed.
  assert(list.vertices.size() == 12);
  // One atlas, so one bind and one draw.
  assert(list.batches.size() == 1);
  assert(list.batches[0].first_vertex == 0);
  assert(list.batches[0].vertex_count == 12);

  ui_draw_list_t spaced;
  client::ui::draw_text(spaced, font, font_size_t::medium, {10.0f, 10.0f}, "a b", colors::white);
  assert(spaced.vertices.size() == 12); // the space costs nothing

  // ...but it still moves the pen, so the second glyph is further right than it
  // would be without it.
  const float without_space = list.vertices[6].position.x;
  const float with_space    = spaced.vertices[6].position.x;
  assert(with_space > without_space);

  std::cout << "test_draw_text_emits_one_batch_and_skips_spaces passed" << std::endl;
}

// THE REASON THE TEXT IS SHARP, and the one thing here that fails silently in
// the worst way: a glyph landing on a fractional pixel or drawn at anything but
// its atlas rect's size is resampled by the bilinear filter and comes out soft.
// It still reads, so nothing else in this file would notice.
//
// It regressed once already. An oversampled bake puts every x_offset on a
// quarter pixel (stb shifts by -(oversample-1)/(2*oversample)) and halves the
// quad against its rect, so flooring the PEN -- which is what draw_text used to
// do -- snapped nothing at all.
void test_glyph_quads_land_on_whole_pixels()
{
  const baked_font_t &baked = shipped();

  for (uint32_t size_index = 0; size_index < client::ui::FONT_SIZE_COUNT; ++size_index)
  {
    const font_size_t size = (font_size_t)size_index;

    // A deliberately fractional origin: the SNAP is what is under test, not
    // whether whole numbers stay whole.
    ui_draw_list_t list;
    client::ui::draw_text(list, baked.font, size, {10.3f, 20.7f}, "Ag", colors::white);
    assert(list.vertices.size() == 12);

    for (const client::renderer::ui_vertex_t &vertex : list.vertices)
    {
      assert(vertex.position.x == std::floor(vertex.position.x));
      assert(vertex.position.y == std::floor(vertex.position.y));
    }

    // ONE TEXEL PER PIXEL, both axes, for both glyphs. This is the half the
    // snapping does not cover: a quad on whole pixels that is a different SIZE
    // from its rect scales the glyph and blurs it just as thoroughly.
    for (uint32_t glyph_ordinal = 0; glyph_ordinal < 2; ++glyph_ordinal)
    {
      const uint32_t first = glyph_ordinal * 6;

      const float pixel_width  = list.vertices[first + 2].position.x - list.vertices[first].position.x;
      const float pixel_height = list.vertices[first + 2].position.y - list.vertices[first].position.y;

      const float texel_width =
          (list.vertices[first + 2].uv.x - list.vertices[first].uv.x) * baked.atlas_width;
      const float texel_height =
          (list.vertices[first + 2].uv.y - list.vertices[first].uv.y) * baked.atlas_height;

      assert(pixel_width > 0.0f && pixel_height > 0.0f);
      assert(std::fabs(pixel_width - texel_width) < 0.01f);
      assert(std::fabs(pixel_height - texel_height) < 0.01f);
    }
  }

  // The pen keeps its FRACTIONAL advances across the snap, so a long run does not
  // drift: the last glyph of a 20-character string sits within a pixel of where
  // the accumulated advances put it. Rounding the pen per glyph instead would
  // bias every step the same way and push the tail visibly right.
  const std::string_view run      = "iiiiiiiiiiiiiiiiiiii";
  const float            advance  = baked.font.sizes[font_size_t::small].glyphs[glyph_index('i')].x_advance;

  ui_draw_list_t list;
  client::ui::draw_text(list, baked.font, font_size_t::small, {0.0f, 0.0f}, run, colors::white);
  assert(list.vertices.size() == run.size() * 6);

  const float span = list.vertices[(run.size() - 1) * 6].position.x - list.vertices[0].position.x;
  assert(std::fabs(span - advance * (float)(run.size() - 1)) <= 1.0f);

  std::cout << "test_glyph_quads_land_on_whole_pixels passed" << std::endl;
}

void test_batching_merges_by_texture()
{
  const ui_font_t &font = shipped_font();

  ui_draw_list_t list;

  // Two solid rects: same (invalid = white) texture, so one batch.
  list.rect({0, 0}, {10, 10}, colors::red);
  list.rect({20, 0}, {30, 10}, colors::blue);
  assert(list.batches.size() == 1);
  assert(list.batches[0].vertex_count == 12);

  // Text switches to the atlas handle, so a second batch opens.
  client::ui::draw_text(list, font, font_size_t::small, {0, 20}, "x", colors::white);
  assert(list.batches.size() == 2);
  assert(list.batches[1].first_vertex == 12);
  assert(list.batches[1].vertex_count == 6);

  // Back to a rect: a THIRD batch, not a merge with the first. Batches are runs
  // in append order, because that order is also the draw order.
  list.rect({0, 40}, {10, 50}, colors::green);
  assert(list.batches.size() == 3);

  list.clear();
  assert(list.vertices.empty());
  assert(list.batches.empty());

  std::cout << "test_batching_merges_by_texture passed" << std::endl;
}

void test_quad_winding_and_uvs()
{
  ui_draw_list_t list;
  list.quad({1, 2}, {11, 22}, {0.25f, 0.5f}, {0.75f, 1.0f}, colors::white);

  assert(list.vertices.size() == 6);

  // Two triangles covering the rect: the corners present must be exactly the
  // four of the rectangle, and the shared diagonal appears twice.
  assert(list.vertices[0].position.x == 1.0f && list.vertices[0].position.y == 2.0f);
  assert(list.vertices[2].position.x == 11.0f && list.vertices[2].position.y == 22.0f);
  assert(list.vertices[5].position.x == 1.0f && list.vertices[5].position.y == 22.0f);

  // UVs travel with their corner, or glyphs come out mirrored.
  assert(list.vertices[0].uv.x == 0.25f && list.vertices[0].uv.y == 0.5f);
  assert(list.vertices[2].uv.x == 0.75f && list.vertices[2].uv.y == 1.0f);

  std::cout << "test_quad_winding_and_uvs passed" << std::endl;
}

void test_anchors()
{
  const linalg::vec2 screen{800.0f, 600.0f};
  const linalg::vec2 margin{10.0f, 20.0f};
  const linalg::vec2 size{100.0f, 50.0f};

  const ui_rect_t top_left = anchored(screen, anchor_t::top_left, {.margin = margin, .size = size});
  assert(top_left.min.x == 10.0f && top_left.min.y == 20.0f);
  assert(top_left.max.x == 110.0f && top_left.max.y == 70.0f);

  const ui_rect_t top_right = anchored(screen, anchor_t::top_right, {.margin = margin, .size = size});
  assert(top_right.max.x == 790.0f && top_right.min.y == 20.0f);

  const ui_rect_t bottom_left = anchored(screen, anchor_t::bottom_left, {.margin = margin, .size = size});
  assert(bottom_left.min.x == 10.0f && bottom_left.max.y == 580.0f);

  const ui_rect_t bottom_right = anchored(screen, anchor_t::bottom_right, {.margin = margin, .size = size});
  assert(bottom_right.max.x == 790.0f && bottom_right.max.y == 580.0f);

  // A margin on a centered axis is ignored -- "20px in from the middle" is not a
  // thing anyone means.
  const ui_rect_t centered = anchored(screen, anchor_t::center, {.margin = margin, .size = size});
  assert(centered.min.x == 350.0f && centered.min.y == 275.0f);
  assert(centered.center().x == 400.0f && centered.center().y == 300.0f);

  const ui_rect_t top_center = anchored(screen, anchor_t::top_center, {.margin = margin, .size = size});
  assert(top_center.min.x == 350.0f && top_center.min.y == 20.0f);

  const ui_rect_t center_left = anchored(screen, anchor_t::center_left, {.margin = margin, .size = size});
  assert(center_left.min.x == 10.0f && center_left.min.y == 275.0f);

  const ui_rect_t center_right = anchored(screen, anchor_t::center_right, {.margin = margin, .size = size});
  assert(center_right.max.x == 790.0f && center_right.min.y == 275.0f);

  const ui_rect_t bottom_center = anchored(screen, anchor_t::bottom_center, {.margin = margin, .size = size});
  assert(bottom_center.min.x == 350.0f && bottom_center.max.y == 580.0f);

  // Every anchor stays inside the screen and keeps the size it was given.
  for (uint32_t index = 0; index < 9; ++index)
  {
    const ui_rect_t rect = anchored(screen, (anchor_t)index, {.margin = margin, .size = size});
    assert(rect.size().x == size.x && rect.size().y == size.y);
    assert(rect.min.x >= 0.0f && rect.min.y >= 0.0f);
    assert(rect.max.x <= screen.x && rect.max.y <= screen.y);
  }

  // inset shrinks on every side; a negative amount grows, which is how a panel
  // gets padding from the text rect it wraps.
  const ui_rect_t padded = inset(top_left, -4.0f);
  assert(padded.min.x == 6.0f && padded.max.x == 114.0f);

  std::cout << "test_anchors passed" << std::endl;
}

void test_bake_rejects_bad_input()
{
  // Empty data, and a size with no height. Both are caller bugs the optional is
  // there to report rather than an atlas full of zeroes.
  assert(!client::ui::try_bake_font(Span<const uint8_t>(), {18.f, 28.f, 48.f}).has_value());

  const uint8_t garbage[16] = {};
  assert(!client::ui::try_bake_font(Span<const uint8_t>(garbage, 16), {18.f, 28.f, 48.f})
              .has_value());

  test_asset_state();
  assert(!client::ui::try_bake_font(assets::read_asset_bytes(client::ui::DEFAULT_FONT_PATH),
                                    {18.f, 0.f, 48.f})
              .has_value());

  // A font that is not there is no longer this function's failure to report: the
  // byte layer owns presence, and asset_exists is the probe. try_bake_font keeps
  // its prefix because the SIZES are a caller parameter and the file is not.
  assert(!assets::asset_exists("resources/fonts/does_not_exist.ttf"));

  std::cout << "test_bake_rejects_bad_input passed" << std::endl;
}

void test_text_alignment()
{
  const ui_font_t &font = shipped_font();
  const ui_rect_t  area{{100.0f, 100.0f}, {400.0f, 140.0f}};

  // Same string, three alignments: the pen moves right each time. Comparing
  // emitted vertices rather than a returned position is deliberate -- what
  // matters is where the glyphs LANDED.
  float first_x[3] = {};
  const text_align_t alignments[3] = {text_align_t::left, text_align_t::center,
                                      text_align_t::right};

  for (uint32_t index = 0; index < 3; ++index)
  {
    ui_draw_list_t list;
    client::ui::draw_text_aligned(list, font, font_size_t::medium, area, alignments[index], "ab",
                                  colors::white);
    assert(!list.vertices.empty());
    first_x[index] = list.vertices[0].position.x;
  }

  assert(first_x[0] < first_x[1] && first_x[1] < first_x[2]);

  // Compare the alignment OFFSETS rather than absolute positions: vertices[0].x
  // is the pen plus the first glyph's left side bearing, and that bearing is
  // identical in all three runs, so differencing cancels it out. Asserting an
  // absolute edge would be asserting the font's bearing instead of the layout.
  const float measured = measure_text(font, font_size_t::medium, "ab").x;
  const float slack    = area.size().x - measured;

  assert(std::fabs((first_x[1] - first_x[0]) - slack * 0.5f) <= 1.0f);
  assert(std::fabs((first_x[2] - first_x[0]) - slack) <= 1.0f);

  // Vertically the line box is CENTRED in its area, so the same string in a
  // taller box sits lower by half the extra height.
  const ui_rect_t tall{{100.0f, 100.0f}, {400.0f, 240.0f}};

  ui_draw_list_t in_short;
  ui_draw_list_t in_tall;
  client::ui::draw_text_aligned(in_short, font, font_size_t::medium, area, text_align_t::left, "ab",
                                colors::white);
  client::ui::draw_text_aligned(in_tall, font, font_size_t::medium, tall, text_align_t::left, "ab",
                                colors::white);

  const float dropped = in_tall.vertices[0].position.y - in_short.vertices[0].position.y;
  assert(std::fabs(dropped - (tall.size().y - area.size().y) * 0.5f) <= 1.0f);

  // It is the LINE BOX that is centred, not the ink -- so a string whose glyphs
  // are shorter does not drift upward. "ab" has no ascender, "bd" does.
  ui_draw_list_t short_glyphs;
  ui_draw_list_t tall_glyphs;
  client::ui::draw_text_aligned(short_glyphs, font, font_size_t::medium, area, text_align_t::left,
                                "aa", colors::white);
  client::ui::draw_text_aligned(tall_glyphs, font, font_size_t::medium, area, text_align_t::left,
                                "dd", colors::white);
  // Same baseline for both, so the difference in quad tops is purely the
  // difference in glyph height -- the box did not move.
  const float short_bottom = short_glyphs.vertices[2].position.y;
  const float tall_bottom  = tall_glyphs.vertices[2].position.y;
  assert(std::fabs(short_bottom - tall_bottom) <= 1.0f);

  std::cout << "test_text_alignment passed" << std::endl;
}

void test_screen_resolution_inherits()
{
  ui_screen_t screen;

  const ui_node_id_t root  = add_node(screen, UI_INVALID_NODE_ID, ui_rect_t{{100, 100}, {400, 300}});
  const ui_node_id_t child = add_node(screen, root, ui_rect_t{{5, 5}, {15, 15}});

  screen[root].offset   = {10.0f, 0.0f};
  screen[root].opacity  = 0.5f;
  screen[child].offset  = {0.0f, 2.0f};
  screen[child].opacity = 0.5f;

  // Rects translate by every ancestor's origin PLUS its animated offset, which
  // is what lets a whole screen slide by animating the root alone.
  const client::ui::resolved_node_t resolved = client::ui::resolve_node(screen, child);
  assert(resolved.rect.min.x == 115.0f && resolved.rect.min.y == 107.0f);
  assert(resolved.rect.max.x == 125.0f && resolved.rect.max.y == 117.0f);

  // Opacity MULTIPLIES down, so nesting two half-fades gives a quarter.
  assert(std::fabs(resolved.opacity - 0.25f) < 0.0001f);

  // The root resolves to exactly its own rect plus its own offset: its parent is
  // the framebuffer, so the chain ends there.
  const client::ui::resolved_node_t root_resolved = client::ui::resolve_node(screen, root);
  assert(root_resolved.rect.min.x == 110.0f && root_resolved.rect.min.y == 100.0f);
  assert(root_resolved.opacity == 0.5f);

  assert(client::ui::node_depth(screen, root) == 0);
  assert(client::ui::node_depth(screen, child) == 1);

  // A missing node resolves to the identity rather than reading out of range.
  assert(client::ui::resolve_node(screen, UI_INVALID_NODE_ID).opacity == 1.0f);

  std::cout << "test_screen_resolution_inherits passed" << std::endl;
}

void test_draw_screen_emits_content()
{
  const ui_font_t &font = shipped_font();

  ui_screen_t          screen;
  const ui_node_id_t root = add_node(screen, UI_INVALID_NODE_ID, ui_rect_t{{0, 0}, {200, 100}});

  // A layout-only root emits nothing; only content does.
  {
    ui_draw_list_t empty;
    client::ui::draw_screen(empty, screen, font);
    assert(empty.vertices.empty());
  }

  const ui_node_id_t bar = add_node(screen, root, ui_rect_t{{0, 0}, {4, 40}});
  screen[bar].content      = ui_solid_content_t{};

  const ui_node_id_t label = add_node(screen, root, ui_rect_t{{10, 0}, {200, 40}});
  screen[label].content = ui_text_content_t{font_size_t::medium, "ab", text_align_t::left};

  ui_draw_list_t list;
  client::ui::draw_screen(list, screen, font);

  // Six for the bar, six per inked glyph.
  assert(list.vertices.size() == 6 + 12);
  // Solid then atlas: two binds, in that order, because children draw in the
  // order they were added.
  assert(list.batches.size() == 2);
  assert(list.batches[0].vertex_count == 6);
  assert(list.batches[1].vertex_count == 12);

  // A fully transparent parent skips its whole subtree rather than emitting
  // invisible geometry.
  screen[root].opacity = 0.0f;
  ui_draw_list_t faded;
  client::ui::draw_screen(faded, screen, font);
  assert(faded.vertices.empty());

  // Half opacity reaches the vertex colour: tint alpha 255 * 0.25 == 64.
  screen[root].opacity = 0.5f;
  screen[bar].opacity  = 0.5f;
  ui_draw_list_t modulated;
  client::ui::draw_screen(modulated, screen, font);
  assert(((modulated.vertices[0].color >> 24) & 0xFF) == 64);

  std::cout << "test_draw_screen_emits_content passed" << std::endl;
}

void test_animation_lands_exactly_and_retires()
{
  ui_screen_t          screen;
  const ui_node_id_t node = add_node(screen, UI_INVALID_NODE_ID, ui_rect_t{});

  animate(screen, node, ui_property_t::opacity)
      .from(0.0f)
      .to(1.0f)
      .duration(0.5f)
      .ease(ease_t::linear);

  advance_animations(screen, 0.25f);
  assert(std::fabs(screen[node].opacity - 0.5f) < 0.0001f);
  assert(screen.animations.size() == 1);

  // Overshooting the duration lands on the EXACT endpoint. A fade stuck at
  // 0.998 is visible and no later frame would correct it.
  advance_animations(screen, 0.30f);
  assert(screen[node].opacity == 1.0f);
  assert(screen.animations.empty());

  // Zero duration is an instant set, not a divide by zero.
  animate(screen, node, ui_property_t::offset_y).from(0.0f).to(12.0f).duration(0.0f);
  advance_animations(screen, 0.016f);
  assert(screen[node].offset.y == 12.0f);
  assert(screen.animations.empty());

  std::cout << "test_animation_lands_exactly_and_retires passed" << std::endl;
}

void test_animation_replaces_same_target()
{
  ui_screen_t          screen;
  const ui_node_id_t node = add_node(screen, UI_INVALID_NODE_ID, ui_rect_t{});

  animate(screen, node, ui_property_t::opacity).from(0.0f).to(1.0f).duration(1.0f);
  animate(screen, node, ui_property_t::opacity).from(0.0f).to(0.5f).duration(1.0f);

  // Re-targeting one property REPLACES: two tweens on one float is not a blend,
  // it is whichever was advanced last. A highlight re-aimed mid-slide does this.
  assert(screen.animations.size() == 1);
  assert(screen.animations[0].to == 0.5f);

  // A different property on the same node is a separate animation.
  animate(screen, node, ui_property_t::offset_x).to(4.0f).duration(1.0f);
  assert(screen.animations.size() == 2);

  // from/to both default to the property's CURRENT value, which is what makes
  // `.to(x)` alone mean "from wherever it is now".
  screen[node].opacity = 0.3f;
  animate(screen, node, ui_property_t::opacity).duration(1.0f);
  advance_animations(screen, 0.5f);
  assert(std::fabs(screen[node].opacity - 0.3f) < 0.0001f);

  std::cout << "test_animation_replaces_same_target passed" << std::endl;
}

// The reason the nodes, the tweens and the focus are one type: a screen is
// replaced as a VALUE, so an id can never name a node from a previous build.
// Held apart, the assignment below would leave a tween and a focus id addressing
// whatever now sits at that index -- silently, since an id is a uint16_t.
void test_rebuilding_a_screen_replaces_its_animations_and_focus()
{
  ui_screen_t screen;
  const ui_node_id_t root = add_node(screen, UI_INVALID_NODE_ID, ui_rect_t{{0, 0}, {100, 100}});
  const ui_node_id_t row  = add_node(screen, root, ui_rect_t{{0, 0}, {100, 40}});

  screen[row].focusable = true;
  screen.focused_node   = row;
  animate(screen, row, ui_property_t::opacity).from(0.0f).to(1.0f).duration(1.0f);
  assert(screen.animations.size() == 1);

  ui_screen_t rebuilt;
  add_node(rebuilt, UI_INVALID_NODE_ID, ui_rect_t{{0, 0}, {100, 100}});
  screen = rebuilt;

  assert(screen.animations.empty());
  assert(screen.focused_node == UI_INVALID_NODE_ID);
  assert(screen.root == 0);

  // Advancing a screen whose build left no tweens is not a special case.
  advance_animations(screen, 0.016f);

  std::cout << "test_rebuilding_a_screen_replaces_its_animations_and_focus passed" << std::endl;
}

void test_easing_endpoints()
{
  const ease_t all[4] = {ease_t::linear, ease_t::in_cubic, ease_t::out_cubic,
                         ease_t::in_out_cubic};

  for (const ease_t easing : all)
  {
    assert(client::ui::apply_ease(easing, 0.0f) == 0.0f);
    assert(client::ui::apply_ease(easing, 1.0f) == 1.0f);
    // Clamped, so a caller that oversteps cannot overshoot -- cubics are not
    // bounded outside [0,1].
    assert(client::ui::apply_ease(easing, -0.5f) == 0.0f);
    assert(client::ui::apply_ease(easing, 1.5f) == 1.0f);

    const float middle = client::ui::apply_ease(easing, 0.5f);
    assert(middle > 0.0f && middle < 1.0f);
  }

  std::cout << "test_easing_endpoints passed" << std::endl;
}

// Two columns of two, which is the layout a linear focus index gets wrong:
//
//   A   B
//   C   D
struct grid_screen_t
{
  ui_screen_t    screen;
  ui_node_id_t a = UI_INVALID_NODE_ID, b = UI_INVALID_NODE_ID, c = UI_INVALID_NODE_ID, d = UI_INVALID_NODE_ID;
};

grid_screen_t build_grid()
{
  grid_screen_t grid;
  const ui_node_id_t root = add_node(grid.screen, UI_INVALID_NODE_ID, ui_rect_t{{0, 0}, {300, 90}});

  grid.a = add_node(grid.screen, root, ui_rect_t{{0, 0}, {100, 40}});
  grid.b = add_node(grid.screen, root, ui_rect_t{{200, 0}, {300, 40}});
  grid.c = add_node(grid.screen, root, ui_rect_t{{0, 50}, {100, 90}});
  grid.d = add_node(grid.screen, root, ui_rect_t{{200, 50}, {300, 90}});

  for (const ui_node_id_t id : {grid.a, grid.b, grid.c, grid.d})
    grid.screen[id].focusable = true;

  return grid;
}

void test_navigation_is_geometric()
{
  const grid_screen_t grid = build_grid();
  const ui_screen_t  &screen = grid.screen;

  // Down stays in its COLUMN, which is the whole point of scoring lateral drift
  // rather than walking an array: an index-based menu would go A -> B here.
  assert(client::ui::find_neighbour(screen, grid.a, nav_direction_t::down) == grid.c);
  assert(client::ui::find_neighbour(screen, grid.b, nav_direction_t::down) == grid.d);

  // Right stays in its ROW.
  assert(client::ui::find_neighbour(screen, grid.a, nav_direction_t::right) == grid.b);
  assert(client::ui::find_neighbour(screen, grid.c, nav_direction_t::right) == grid.d);
  assert(client::ui::find_neighbour(screen, grid.b, nav_direction_t::left) == grid.a);

  assert(client::ui::find_neighbour(screen, grid.c, nav_direction_t::up) == grid.a);

  // Edges report nothing; wrapping is the caller's policy, not the search's.
  assert(client::ui::find_neighbour(screen, grid.c, nav_direction_t::down) == UI_INVALID_NODE_ID);
  assert(client::ui::find_neighbour(screen, grid.a, nav_direction_t::up) == UI_INVALID_NODE_ID);
  assert(client::ui::find_neighbour(screen, grid.a, nav_direction_t::left) == UI_INVALID_NODE_ID);

  // ...and the wrapping form comes back around IN THE SAME COLUMN.
  assert(client::ui::find_neighbour_wrapping(screen, grid.c, nav_direction_t::down) == grid.a);
  assert(client::ui::find_neighbour_wrapping(screen, grid.a, nav_direction_t::up) == grid.c);
  assert(client::ui::find_neighbour_wrapping(screen, grid.d, nav_direction_t::down) == grid.b);
  assert(client::ui::find_neighbour_wrapping(screen, grid.b, nav_direction_t::up) == grid.d);

  assert(client::ui::first_focusable(screen) == grid.a);

  // A screen with nothing focusable is a legitimate screen, not an error.
  ui_screen_t bare;
  add_node(bare, UI_INVALID_NODE_ID, ui_rect_t{{0, 0}, {10, 10}});
  assert(client::ui::first_focusable(bare) == UI_INVALID_NODE_ID);
  assert(client::ui::find_neighbour(bare, UI_INVALID_NODE_ID, nav_direction_t::down) == UI_INVALID_NODE_ID);

  std::cout << "test_navigation_is_geometric passed" << std::endl;
}

void test_single_column_navigation_wraps()
{
  ui_screen_t          screen;
  const ui_node_id_t root = add_node(screen, UI_INVALID_NODE_ID, ui_rect_t{{50, 50}, {450, 260}});

  ui_node_id_t rows[5] = {};
  for (uint32_t index = 0; index < 5; ++index)
  {
    const float top   = (float)index * 42.0f;
    rows[index]       = add_node(screen, root, ui_rect_t{{0, top}, {400, top + 42.0f}});
    screen[rows[index]].focusable = true;
  }

  for (uint32_t index = 0; index + 1 < 5; ++index)
    assert(client::ui::find_neighbour(screen, rows[index], nav_direction_t::down) == rows[index + 1]);

  // Down off the bottom lands on the top row, and up off the top on the bottom.
  assert(client::ui::find_neighbour_wrapping(screen, rows[4], nav_direction_t::down) == rows[0]);
  assert(client::ui::find_neighbour_wrapping(screen, rows[0], nav_direction_t::up) == rows[4]);

  // Nothing is left or right of a single column.
  assert(client::ui::find_neighbour(screen, rows[2], nav_direction_t::left) == UI_INVALID_NODE_ID);
  assert(client::ui::find_neighbour(screen, rows[2], nav_direction_t::right) == UI_INVALID_NODE_ID);

  std::cout << "test_single_column_navigation_wraps passed" << std::endl;
}

void test_hit_test()
{
  const grid_screen_t grid = build_grid();
  const ui_screen_t  &screen = grid.screen;

  assert(client::ui::hit_test(screen, {50.0f, 20.0f}) == grid.a);
  assert(client::ui::hit_test(screen, {250.0f, 70.0f}) == grid.d);

  // The gap between the columns and everything off the screen is a clean miss --
  // a click landing on nothing must not activate the nearest row.
  assert(client::ui::hit_test(screen, {150.0f, 20.0f}) == UI_INVALID_NODE_ID);
  assert(client::ui::hit_test(screen, {-5.0f, 20.0f}) == UI_INVALID_NODE_ID);
  assert(client::ui::hit_test(screen, {50.0f, 45.0f}) == UI_INVALID_NODE_ID);

  // Half-open on the max edge, so two abutting rows never both claim a point.
  assert(client::ui::hit_test(screen, {0.0f, 0.0f}) == grid.a);
  assert(client::ui::hit_test(screen, {100.0f, 20.0f}) == UI_INVALID_NODE_ID);

  // A focusable child inside a focusable parent resolves to the DEEPEST.
  ui_screen_t          nested;
  const ui_node_id_t panel = add_node(nested, UI_INVALID_NODE_ID, ui_rect_t{{0, 0}, {200, 200}});
  const ui_node_id_t button = add_node(nested, panel, ui_rect_t{{10, 10}, {60, 40}});
  nested[panel].focusable   = true;
  nested[button].focusable  = true;
  assert(client::ui::hit_test(nested, {30.0f, 20.0f}) == button);
  assert(client::ui::hit_test(nested, {150.0f, 150.0f}) == panel);

  // Hit-testing follows an animated offset, or a mid-slide row is clickable
  // where it used to be rather than where it is drawn.
  nested[button].offset = {100.0f, 0.0f};
  assert(client::ui::hit_test(nested, {30.0f, 20.0f}) == panel);
  assert(client::ui::hit_test(nested, {130.0f, 20.0f}) == button);

  std::cout << "test_hit_test passed" << std::endl;
}

// The shared list widget, which both menus are. Everything here is what a screen
// gets for free by using it rather than growing its own copy: the rows land
// where the anchor says, the pointer and the keyboard agree on what is focused,
// and activation reports a ROW INDEX -- the thing the caller's enum is.
void test_list_menu_rows_layout_and_activate()
{
  const char *labels[] = {"RESUME", "DISCONNECT", "QUIT"};

  list_menu_style_t style;
  style.anchor                  = anchor_t::top_left;
  style.margin                  = {0.0f, 0.0f};
  style.width                   = 200.0f;
  style.row_height              = 40.0f;
  style.label_inset             = 20.0f;
  style.intro_fade_seconds      = 0.0f; // otherwise everything is mid-fade
  style.highlight_slide_seconds = 0.0f;

  const linalg::vec2 screen_size = {800.0f, 600.0f};
  list_menu_t        menu        = client::ui::build_list_menu(labels, style, screen_size);

  assert(menu.row_count() == 3);

  // Focus starts on the first row, so a screen is never drawn with no highlight.
  assert(menu.screen.focused_node == menu.rows[0]);

  // Rows stack downward from the anchored panel, inset for the highlight bar.
  const ui_rect_t second = client::ui::resolve_node(menu.screen, menu.rows[1]).rect;
  assert(std::fabs(second.min.x - 20.0f) < 0.01f);
  assert(std::fabs(second.min.y - 40.0f) < 0.01f);
  assert(std::fabs(second.max.y - 80.0f) < 0.01f);

  // A backdrop is a NODE only when the style asks for one: an invisible
  // full-screen quad per frame is not the default a menu should pay for.
  assert(menu.backdrop == UI_INVALID_NODE_ID);

  // Keyboard: down moves focus and the bar follows it.
  ui_input_t input;
  input.navigate[nav_direction_t::down] = true;
  assert(!client::ui::update_list_menu(menu, input, screen_size));
  assert(menu.screen.focused_node == menu.rows[1]);

  client::ui::advance_list_menu(menu, 0.016f, screen_size);
  const ui_rect_t bar = client::ui::resolve_node(menu.screen, menu.highlight_indicator).rect;
  assert(std::fabs(bar.min.y - second.min.y) < 0.01f);

  // ...and a click activates what is under the POINTER, not what focus
  // remembers -- the two disagree here on purpose.
  ui_input_t click;
  click.pointer_position = {100.0f, 100.0f}; // the third row
  click.pointer_activate = true;
  const std::optional<uint32_t> activated = client::ui::update_list_menu(menu, click, screen_size);
  assert(activated && *activated == 2);

  // A click on nothing is not an activation.
  ui_input_t miss;
  miss.pointer_position = {700.0f, 500.0f};
  miss.pointer_activate = true;
  assert(!client::ui::update_list_menu(menu, miss, screen_size));

  std::cout << "test_list_menu_rows_layout_and_activate passed" << std::endl;
}


} // namespace

int main()
{
  test_bake_produces_sane_metrics();
  test_space_advances_but_has_no_ink();
  test_measure_text();
  test_draw_text_emits_one_batch_and_skips_spaces();
  test_glyph_quads_land_on_whole_pixels();
  test_batching_merges_by_texture();
  test_quad_winding_and_uvs();
  test_anchors();
  test_bake_rejects_bad_input();
  test_text_alignment();
  test_screen_resolution_inherits();
  test_draw_screen_emits_content();
  test_animation_lands_exactly_and_retires();
  test_animation_replaces_same_target();
  test_rebuilding_a_screen_replaces_its_animations_and_focus();
  test_easing_endpoints();
  test_navigation_is_geometric();
  test_single_column_navigation_wraps();
  test_hit_test();
  test_list_menu_rows_layout_and_activate();

  std::cout << "All UI tests passed!" << std::endl;
  return 0;
}

