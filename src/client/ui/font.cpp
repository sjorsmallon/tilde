#include "font.hpp"

#include "../../shared/log.hpp"

// The one translation unit that owns stb_truetype's implementation, exactly as
// asset.cpp owns stb_image's.
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../shared/stb_truetype.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace client::ui
{
namespace
{

// OVERSAMPLING IS OFF, and it is a decision rather than an omission. It buys
// subpixel POSITIONING -- glyphs that look right at a fractional pen -- and pays
// for it in blur, because stb's prefilter shifts every offset onto a quarter
// pixel (stbtt__oversample_shift) and the quad is then drawn at half the atlas
// rect's size, so the sampler minifies 2:1 with two taps. A HUD's text sits
// still, so draw_text snaps each quad to whole pixels instead and samples the
// atlas 1:1, which is strictly sharper. The day text slides or scrolls, the
// trade flips: turn 2x1 back on and stop snapping. Do not do one without the
// other -- that combination is the blurry one.
constexpr uint32_t OVERSAMPLING = 1;

// Atlas growth: start small and double the HEIGHT until every size fits. Width
// stays at 1024 because glyph rows pack across, not down, so growing height is
// what actually buys room. 4096 is the last attempt -- beyond it the font is
// asking for more than a HUD needs and the honest answer is to fail.
constexpr int32_t ATLAS_WIDTH      = 1024;
constexpr int32_t ATLAS_MIN_HEIGHT = 256;
constexpr int32_t ATLAS_MAX_HEIGHT = 4096;

// Fill `out` from a successful pack. stbtt_packedchar is stbtt_GetPackedQuad's
// input; converting here means the draw path never divides by the atlas size.
void fill_metrics(font_metrics_t &out, const stbtt_packedchar *packed, float pixel_height,
                  const stbtt_fontinfo &info, int32_t atlas_width, int32_t atlas_height)
{
  const float scale = stbtt_ScaleForPixelHeight(&info, pixel_height);

  int32_t ascent = 0, descent = 0, line_gap = 0;
  stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);

  out.pixel_height = pixel_height;
  out.ascent       = (float)ascent * scale;
  out.descent      = -(float)descent * scale; // stb reports descent negative; we want a magnitude
  out.line_height  = (float)(ascent - descent + line_gap) * scale;

  const float inverse_width  = 1.0f / (float)atlas_width;
  const float inverse_height = 1.0f / (float)atlas_height;

  for (uint32_t index = 0; index < PRINTABLE_ASCII_COUNT; ++index)
  {
    const stbtt_packedchar &source = packed[index];
    glyph_t                &glyph  = out.glyphs[index];

    glyph.x_offset  = source.xoff;
    glyph.y_offset  = source.yoff;
    glyph.x_offset2 = source.xoff2;
    glyph.y_offset2 = source.yoff2;
    glyph.x_advance = source.xadvance;

    // A ZERO-AREA UV RECT MEANS "NO INK", and it is an invariant this loop
    // establishes rather than one the packer hands over. stb still allocates a
    // rect for a whitespace glyph -- the padding alone makes it one texel wide
    // -- so taking the packed rect at face value would have draw_text
    // emitting six vertices per space for a half-pixel of transparency. Asking
    // the font whether the glyph has an outline is the honest test, and it makes
    // the rule downstream a single comparison.
    const int32_t glyph_index =
        stbtt_FindGlyphIndex(&info, (int32_t)(FIRST_PRINTABLE_ASCII + index));
    if (stbtt_IsGlyphEmpty(&info, glyph_index))
    {
      glyph.u0 = glyph.v0 = glyph.u1 = glyph.v1 = 0.0f;
      continue;
    }

    glyph.u0 = (float)source.x0 * inverse_width;
    glyph.v0 = (float)source.y0 * inverse_height;
    glyph.u1 = (float)source.x1 * inverse_width;
    glyph.v1 = (float)source.y1 * inverse_height;
  }
}

// Codepoint -> index into font_metrics_t::glyphs, or PRINTABLE_ASCII_COUNT for
// anything outside the baked range.
uint32_t glyph_index_for(unsigned char character)
{
  const uint32_t codepoint = (uint32_t)character;
  if (codepoint < FIRST_PRINTABLE_ASCII || codepoint >= FIRST_PRINTABLE_ASCII + PRINTABLE_ASCII_COUNT)
    return PRINTABLE_ASCII_COUNT;
  return codepoint - FIRST_PRINTABLE_ASCII;
}

} // namespace

std::optional<font_atlas_t> try_bake_font(Span<const uint8_t>                    ttf_bytes,
                                          const Enum_Array<font_size_t, float> &pixel_heights)
{
  if (ttf_bytes.count == 0)
  {
    log_error("[ui] try_bake_font: empty font data");
    return std::nullopt;
  }

  // Checked SEPARATELY from InitFont, and first: stbtt_GetFontOffsetForIndex
  // returns -1 when the bytes carry no font at that index, and stbtt_InitFont
  // does not validate the offset it is handed -- it reads through it and walks
  // off the buffer. A corrupt file must fail, not crash.
  const int32_t font_offset = stbtt_GetFontOffsetForIndex(ttf_bytes.data, 0);
  if (font_offset < 0)
  {
    log_error("[ui] try_bake_font: no font at index 0 -- not a TrueType file?");
    return std::nullopt;
  }

  stbtt_fontinfo info{};
  if (!stbtt_InitFont(&info, ttf_bytes.data, font_offset))
  {
    log_error("[ui] try_bake_font: stbtt_InitFont failed");
    return std::nullopt;
  }

  for (uint32_t index = 0; index < FONT_SIZE_COUNT; ++index)
  {
    if (pixel_heights.values[index] <= 0.0f)
    {
      log_error("[ui] try_bake_font: size {} has a non-positive pixel height ({})", index,
                pixel_heights.values[index]);
      return std::nullopt;
    }
  }

  // One packed-char block per size, all alive across the single PackFontRanges
  // call because stb writes into them as it packs.
  Enum_Array<font_size_t, Array<stbtt_packedchar, PRINTABLE_ASCII_COUNT>> packed = {};

  std::vector<uint8_t> coverage;

  for (int32_t atlas_height = ATLAS_MIN_HEIGHT; atlas_height <= ATLAS_MAX_HEIGHT; atlas_height *= 2)
  {
    coverage.assign((size_t)ATLAS_WIDTH * (size_t)atlas_height, 0);

    stbtt_pack_context pack_context{};
    if (!stbtt_PackBegin(&pack_context, coverage.data(), ATLAS_WIDTH, atlas_height,
                         /*stride*/ 0, /*padding*/ 1, nullptr))
    {
      log_error("[ui] try_bake_font: stbtt_PackBegin failed at {}x{}", ATLAS_WIDTH, atlas_height);
      return std::nullopt;
    }

    // Set explicitly rather than leaned on: PackBegin's default happens to be
    // 1x1, and this is not a value to discover from someone else's default.
    stbtt_PackSetOversampling(&pack_context, OVERSAMPLING, OVERSAMPLING);

    // ONE CALL PER SIZE, and it must stay that way even though oversampling no
    // longer forces it. stbtt_PackFontRangesGatherRects carries a
    // `missing_glyph_added` latch ACROSS every range in a call: the first
    // codepoint the font lacks gets a real .notdef rect and each one after it
    // gets a zero-area rect, which RenderIntoRects then skips -- leaving that
    // glyph with a ZERO ADVANCE. With the three sizes batched into one call, a
    // codepoint missing from the font (the shipped face has no '_') draws and
    // advances at `small` and collapses to nothing at the other two. Per-size
    // calls reset the latch, so each size gets its own .notdef.
    bool packed_every_size = true;
    for (uint32_t index = 0; index < FONT_SIZE_COUNT; ++index)
    {
      stbtt_pack_range range{};
      range.font_size                        = pixel_heights.values[index];
      range.first_unicode_codepoint_in_range = (int32_t)FIRST_PRINTABLE_ASCII;
      range.num_chars                        = (int32_t)PRINTABLE_ASCII_COUNT;
      range.chardata_for_range               = packed.values[index].data;

      if (!stbtt_PackFontRanges(&pack_context, ttf_bytes.data, 0, &range, 1))
      {
        packed_every_size = false;
        break;
      }
    }

    stbtt_PackEnd(&pack_context);

    if (!packed_every_size)
      continue; // too small -- grow and try again

    font_atlas_t atlas;
    for (uint32_t index = 0; index < FONT_SIZE_COUNT; ++index)
      fill_metrics(atlas.sizes.values[index], packed.values[index].data,
                   pixel_heights.values[index], info, ATLAS_WIDTH, atlas_height);

    // texture_asset_t is 4-channel by construction (stb_image is forced to
    // RGBA), so coverage is expanded to white-with-alpha rather than uploaded as
    // R8. That is also what keeps ui.frag a single multiply with no swizzle: a
    // glyph samples (1,1,1,coverage) and the vertex colour comes through
    // untouched.
    atlas.image.width    = ATLAS_WIDTH;
    atlas.image.height   = atlas_height;
    atlas.image.channels = 4;
    atlas.image.pixels.resize(coverage.size() * 4);
    for (size_t texel = 0; texel < coverage.size(); ++texel)
    {
      atlas.image.pixels[texel * 4 + 0] = 255;
      atlas.image.pixels[texel * 4 + 1] = 255;
      atlas.image.pixels[texel * 4 + 2] = 255;
      atlas.image.pixels[texel * 4 + 3] = coverage[texel];
    }

    return atlas;
  }

  log_error("[ui] try_bake_font: {} sizes did not fit in a {}x{} atlas", FONT_SIZE_COUNT,
            ATLAS_WIDTH, ATLAS_MAX_HEIGHT);
  return std::nullopt;
}

std::optional<font_atlas_t>
try_bake_font_from_file(const char *path, const Enum_Array<font_size_t, float> &pixel_heights)
{
  FILE *file = fopen(path, "rb");
  if (!file)
  {
    log_error("[ui] try_bake_font_from_file: cannot open '{}'", path);
    return std::nullopt;
  }

  fseek(file, 0, SEEK_END);
  const long size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (size <= 0)
  {
    log_error("[ui] try_bake_font_from_file: '{}' is empty", path);
    fclose(file);
    return std::nullopt;
  }

  std::vector<uint8_t> bytes((size_t)size);
  const size_t         read = fread(bytes.data(), 1, bytes.size(), file);
  fclose(file);

  if (read != bytes.size())
  {
    log_error("[ui] try_bake_font_from_file: short read on '{}' ({} of {} bytes)", path, read,
              bytes.size());
    return std::nullopt;
  }

  return try_bake_font(Span<const uint8_t>(bytes.data(), (uint32_t)bytes.size()), pixel_heights);
}

linalg::vec2 measure_text(const ui_font_t &font, font_size_t size, std::string_view text)
{
  const font_metrics_t &metrics = font.sizes[size];

  float width = 0.0f;
  for (const char character : text)
  {
    const uint32_t index = glyph_index_for((unsigned char)character);
    if (index < PRINTABLE_ASCII_COUNT)
      width += metrics.glyphs[index].x_advance;
  }

  // Height is the line box, not the ink. A counter ticking 9 -> 10 must not
  // change the height of the row it sits in.
  return {width, metrics.line_height};
}

void draw_text(renderer::ui_draw_list_t &list, const ui_font_t &font, font_size_t size,
               linalg::vec2 top_left, std::string_view text, color_t color)
{
  const font_metrics_t &metrics = font.sizes[size];

  // Callers hand over the top of the line box; the glyph offsets are relative to
  // the baseline.
  //
  // THE PEN STAYS FRACTIONAL AND THE QUAD IS SNAPPED, which is the whole reason
  // text here is sharp rather than soft. Advances are fractional, so rounding
  // the pen itself would accumulate a visibly wrong width across a word; but a
  // quad on a half pixel is resampled by the bilinear filter and every stem goes
  // grey at both edges. Snapping on the way out keeps both: the pen carries full
  // precision, and each glyph still lands texel-for-pixel on the atlas.
  float       pen_x = top_left.x;
  const float pen_y = top_left.y + metrics.ascent;

  for (const char character : text)
  {
    const uint32_t index = glyph_index_for((unsigned char)character);
    if (index >= PRINTABLE_ASCII_COUNT)
      continue;

    const glyph_t &glyph = metrics.glyphs[index];

    // Space and friends carry an advance but no ink. Emitting a zero-area quad
    // would cost six vertices per space for nothing.
    if (glyph.u1 > glyph.u0 && glyph.v1 > glyph.v0)
    {
      // Round the TOP-LEFT and add the unrounded extent. Rounding both corners
      // independently would stretch or squash the glyph by up to a pixel, which
      // is the same resampling this is here to avoid.
      const float x = std::floor(pen_x + glyph.x_offset + 0.5f);
      const float y = std::floor(pen_y + glyph.y_offset + 0.5f);

      list.quad({x, y},
                {x + (glyph.x_offset2 - glyph.x_offset), y + (glyph.y_offset2 - glyph.y_offset)},
                {glyph.u0, glyph.v0}, {glyph.u1, glyph.v1}, color, font.atlas);
    }

    pen_x += glyph.x_advance;
  }
}

void draw_text_aligned(renderer::ui_draw_list_t &list, const ui_font_t &font, font_size_t size,
                       ui_rect_t area, text_align_t align, std::string_view text, color_t color)
{
  const linalg::vec2 measured = measure_text(font, size, text);

  float x = area.min.x;
  if (align == text_align_t::center)
    x = area.min.x + (area.size().x - measured.x) * 0.5f;
  else if (align == text_align_t::right)
    x = area.max.x - measured.x;

  // Centre the LINE BOX, not the ink. Using the ink would make a row of digits
  // hop as it ticked from "9" to "10" and picked up a different glyph height.
  const float y = area.min.y + (area.size().y - measured.y) * 0.5f;

  draw_text(list, font, size, {x, y}, text, color);
}

} // namespace client::ui
