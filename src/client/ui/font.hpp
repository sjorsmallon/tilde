#pragma once

// A raster glyph atlas baked from a TrueType file, and the two things anyone
// does with one: measure a string, and turn it into quads.
//
// THE BAKE IS GPU-FREE. try_bake_font produces a font_atlas_t -- pixels plus
// metrics, no Vulkan anywhere -- and registration is two lines at the call site:
//
//   const font_atlas_t atlas = *try_bake_font(bytes, {18.f, 28.f, 48.f});
//   ui_font_t font{renderer::register_texture(atlas.image, /*srgb*/ false), atlas.sizes};
//
// That split is not ceremony. It is what lets ui_test bake the real font and
// check every glyph without a device, a swapchain or a window -- the same reason
// debug_draw_list.cpp is its own translation unit.
//
// This header deliberately does NOT include stb_truetype.h. Metrics are
// converted to plain floats at bake time, so nothing downstream ever sees an
// stb type and including this costs nothing.
//
// ui_def.md is the design and the reasoning.

#include "../../shared/array.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/color.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/span.hpp"
#include "../renderer.hpp"
#include "layout.hpp"

#include <optional>
#include <string_view>

namespace client::ui
{

// The font the game ships. A named constant rather than an assets.def class
// because there is exactly one font: promote it when there is a second, which is
// also when "which font" becomes a thing a call site has to say.
inline constexpr const char *DEFAULT_FONT_PATH = "resources/fonts/anwb-uu-regular.ttf";

// Codepoints 32..126 -- space through '~'. Everything a HUD, a kill feed and a
// console banner needs. A wider range is a longer pack_range and a bigger atlas,
// not a different design; see ui_def.md.
inline constexpr uint32_t FIRST_PRINTABLE_ASCII = 32;
inline constexpr uint32_t PRINTABLE_ASCII_COUNT = 95;

// A CLOSED set of sizes, on purpose. A raster atlas is sharp at the size it was
// baked at and blurry between, so "any size you like" would be an API that
// mostly returns bad-looking text. Three named sizes are a decision, and the
// call site reads as one.
enum class font_size_t : uint8_t
{
  small,  // kill feed, secondary readouts
  medium, // health / ammo
  large   // announcements
};

inline constexpr uint32_t FONT_SIZE_COUNT = 3;

} // namespace client::ui

// Global scope on purpose: enum_traits is declared in shared/array.hpp, which
// knows nothing about this namespace. Hand-written enums provide `count` alone;
// only def_gen's carry the reflection id beside it.
template <> struct enum_traits<client::ui::font_size_t>
{
  static constexpr uint32_t count = client::ui::FONT_SIZE_COUNT;
};

namespace client::ui
{

// One glyph's place in the atlas and its place on the line. Pre-divided into
// normalized UVs at bake time, so drawing is a copy rather than a conversion --
// this is stbtt_GetPackedQuad's arithmetic, done once instead of per glyph per
// frame.
struct glyph_t
{
  // A ZERO-AREA rect means the glyph has no ink (space, and anything else the
  // font leaves blank). draw_text keys off exactly that, which is why the bake
  // establishes it rather than passing the packer's one-texel rect through.
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
  // Offsets from the pen position (x = pen, y = BASELINE) to the quad corners.
  float x_offset = 0, y_offset = 0, x_offset2 = 0, y_offset2 = 0;
  float x_advance = 0;
};

struct font_metrics_t
{
  float pixel_height = 0;
  float ascent       = 0; // baseline to top, positive
  float descent      = 0; // baseline to bottom, positive
  float line_height  = 0; // ascent + descent + line gap: baseline to next baseline
  Array<glyph_t, PRINTABLE_ASCII_COUNT> glyphs = {};
};

// What the bake returns: everything needed to draw, minus the upload.
struct font_atlas_t
{
  assets::texture_asset_t                  image;
  Enum_Array<font_size_t, font_metrics_t>  sizes;
};

// What the draw path holds, once the image is on the GPU.
struct ui_font_t
{
  renderer::texture_handle_t              atlas;
  Enum_Array<font_size_t, font_metrics_t> sizes;
};

// Bake every size into ONE atlas. Empty optional means the font data would not
// parse or would not fit in the largest atlas tried (4096 tall) -- both are
// logged where they happen.
//
// The caller decides what a failure means. Client init treats it as fatal, which
// is the honest reading: a game that cannot draw text cannot run.
[[nodiscard]] std::optional<font_atlas_t>
try_bake_font(Span<const uint8_t> ttf_bytes, const Enum_Array<font_size_t, float> &pixel_heights);

// Reads the whole file and bakes it. The convenience form, and the only caller
// that needs to know a font lives on disk.
[[nodiscard]] std::optional<font_atlas_t>
try_bake_font_from_file(const char *path, const Enum_Array<font_size_t, float> &pixel_heights);

// The advance width and line height a string would occupy. Height is one line's
// line_height regardless of content, so a row of text does not change height
// when its glyphs do -- a counter ticking from 9 to 10 must not move.
//
// Newlines are NOT handled: one call is one line. Multi-line layout is the
// caller's, because where the second line goes is a layout decision.
[[nodiscard]] linalg::vec2 measure_text(const ui_font_t &font, font_size_t size,
                                        std::string_view text);

// `top_left` is the top-left of the LINE BOX, not the baseline -- the baseline
// is derived by adding ascent, so no call site does font math. Non-printable
// bytes and anything outside the baked range are skipped silently; a HUD is not
// the place to discover that a name had a tab in it.
void draw_text(renderer::ui_draw_list_t &list, const ui_font_t &font, font_size_t size,
               linalg::vec2 top_left, std::string_view text, color_t color);

// Horizontal placement within a box. Vertical placement has no enum because
// there is only one sane answer for a row of text -- see draw_text_aligned.
enum class text_align_t : uint8_t
{
  left,
  center,
  right
};

// Place one line inside `area`: horizontally per `align`, and always CENTERED
// VERTICALLY, because a row's box is taller than its line box and text sitting
// on the top edge of its own row is never what anyone meant.
//
// This is what a `TextLayout` object with setters and a Build() call would have
// been, minus the object -- the caller already owns these fields (a tree node
// holds exactly them), so a wrapper would own nothing and only add a lifetime.
// Layout stays a pure function of its arguments, like measure_text beside it.
//
// Wrapping is deliberately absent: one call is still one line. It lands when a
// screen needs a paragraph, and not before.
void draw_text_aligned(renderer::ui_draw_list_t &list, const ui_font_t &font, font_size_t size,
                       ui_rect_t area, text_align_t align, std::string_view text, color_t color);

} // namespace client::ui
