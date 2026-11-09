#include "text_raster.hpp"

#include "log.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace shared
{

namespace
{

// A font placed in a cell: the space's advance fills the cell's width (a monospace font's cell IS its advance
// box), the glyph is uniform where the picture lands, and the ascent-to-descent line is centred on the cell's
// height. Lengths are in cell heights.
struct placed_font_t
{
  stbtt_fontinfo info;
  int32_t        ascent  = 0;
  int32_t        descent = 0;
  float          scale_cell = 0.f;
  float          baseline_v = 0.f;
  float          cell_aspect = 1.f;
};

placed_font_t place_font_or_die(Span<const uint8_t> font_bytes, float cell_aspect)
{
  if (!(cell_aspect > 0.f))
    fatal_error("[text] a cell's aspect is its width over its height, not {}", cell_aspect);
  placed_font_t font;
  const int32_t font_offset = font_bytes.count == 0 ? -1 : stbtt_GetFontOffsetForIndex(font_bytes.data, 0);
  if (font_offset < 0 || !stbtt_InitFont(&font.info, font_bytes.data, font_offset))
    fatal_error("[text] the {} font bytes carry no font", font_bytes.count);
  int32_t line_gap = 0;
  stbtt_GetFontVMetrics(&font.info, &font.ascent, &font.descent, &line_gap);
  int32_t advance = 0, left_side_bearing = 0;
  stbtt_GetCodepointHMetrics(&font.info, ' ', &advance, &left_side_bearing);
  if (advance <= 0 || font.ascent <= font.descent)
    fatal_error("[text] the font's space advances {} on a line of {}: not a font a cell can hold", advance,
                font.ascent - font.descent);
  font.cell_aspect = cell_aspect;
  font.scale_cell  = cell_aspect / static_cast<float>(advance);
  const float line = static_cast<float>(font.ascent - font.descent) * font.scale_cell;
  font.baseline_v  = 0.5f * (1.f - line) + static_cast<float>(font.ascent) * font.scale_cell;
  return font;
}

bool character_has_ink(char character)
{
  return character > ' ' && character <= '~';
}

// Where a character's pen starts across its cell, in cell heights, centred on its own advance.
float compute_pen_u(const placed_font_t& font, char character)
{
  int32_t advance = 0, left_side_bearing = 0;
  stbtt_GetCodepointHMetrics(&font.info, character, &advance, &left_side_bearing);
  return 0.5f * (font.cell_aspect - static_cast<float>(advance) * font.scale_cell);
}

} // namespace

void rasterise_character_grid(Span<const uint8_t> font_bytes, Span<const char> characters, uint32_t columns,
                              uint32_t rows, uint32_t width, uint32_t height, float cell_aspect,
                              Span<uint8_t> coverage)
{
  if (characters.count != static_cast<size_t>(columns) * rows)
    fatal_error("[text] {} characters for a {} by {} grid", characters.count, columns, rows);
  if (coverage.count != static_cast<size_t>(width) * height)
    fatal_error("[text] {} coverage bytes for a {} by {} picture", coverage.count, width, height);
  if (columns == 0 || rows == 0 || width == 0 || height == 0)
    fatal_error("[text] an empty grid or picture: {} by {} cells on {} by {} pixels", columns, rows, width, height);

  const placed_font_t font        = place_font_or_die(font_bytes, cell_aspect);
  const float         cell_width  = static_cast<float>(width) / static_cast<float>(columns);
  const float         cell_height = static_cast<float>(height) / static_cast<float>(rows);
  const float         scale_y     = cell_height * font.scale_cell;
  const float         scale_x     = cell_width * font.scale_cell / cell_aspect;

  std::fill(coverage.begin(), coverage.end(), uint8_t{0});
  std::vector<uint8_t> glyph;
  for (uint32_t row = 0; row < rows; ++row)
    for (uint32_t column = 0; column < columns; ++column)
    {
      const char character = characters[static_cast<size_t>(row) * columns + column];
      if (!character_has_ink(character))
        continue;

      const float   pen_x = (static_cast<float>(column) + compute_pen_u(font, character) / cell_aspect) * cell_width;
      const float   baseline_y     = (static_cast<float>(row) + font.baseline_v) * cell_height;
      const int32_t pen_x_floor    = static_cast<int32_t>(std::floor(pen_x));
      const int32_t baseline_floor = static_cast<int32_t>(std::floor(baseline_y));
      const float   shift_x        = pen_x - static_cast<float>(pen_x_floor);
      const float   shift_y        = baseline_y - static_cast<float>(baseline_floor);

      int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      stbtt_GetCodepointBitmapBoxSubpixel(&font.info, character, scale_x, scale_y, shift_x, shift_y, &x0, &y0, &x1,
                                          &y1);
      const int32_t glyph_width  = x1 - x0;
      const int32_t glyph_height = y1 - y0;
      if (glyph_width <= 0 || glyph_height <= 0)
        continue;
      glyph.assign(static_cast<size_t>(glyph_width) * static_cast<size_t>(glyph_height), 0);
      stbtt_MakeCodepointBitmapSubpixel(&font.info, glyph.data(), glyph_width, glyph_height, glyph_width, scale_x,
                                        scale_y, shift_x, shift_y, character);

      for (int32_t glyph_y = 0; glyph_y < glyph_height; ++glyph_y)
      {
        const int32_t pixel_y = baseline_floor + y0 + glyph_y;
        if (pixel_y < 0 || pixel_y >= static_cast<int32_t>(height))
          continue;
        for (int32_t glyph_x = 0; glyph_x < glyph_width; ++glyph_x)
        {
          const int32_t pixel_x = pen_x_floor + x0 + glyph_x;
          if (pixel_x < 0 || pixel_x >= static_cast<int32_t>(width))
            continue;
          uint8_t& pixel = coverage[static_cast<size_t>(pixel_y) * width + static_cast<size_t>(pixel_x)];
          pixel          = std::max(pixel, glyph[static_cast<size_t>(glyph_y) * glyph_width + glyph_x]);
        }
      }
    }
}

std::optional<character_ink_box_t> try_compute_character_ink_box(Span<const uint8_t> font_bytes, char character,
                                                                 float cell_aspect)
{
  if (!character_has_ink(character))
    return std::nullopt;

  const placed_font_t font = place_font_or_die(font_bytes, cell_aspect);
  int32_t             x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  if (!stbtt_GetCodepointBox(&font.info, character, &x0, &y0, &x1, &y1) || x1 <= x0 || y1 <= y0)
    return std::nullopt;

  const float pen_u  = compute_pen_u(font, character);
  const float across = font.scale_cell / cell_aspect;
  return character_ink_box_t{
      .u0 = std::clamp((pen_u + static_cast<float>(x0) * font.scale_cell) / cell_aspect, 0.f, 1.f),
      .v0 = std::clamp(font.baseline_v - static_cast<float>(y1) * font.scale_cell, 0.f, 1.f),
      .u1 = std::clamp((pen_u + static_cast<float>(x1) * font.scale_cell) / cell_aspect, 0.f, 1.f),
      .v1 = std::clamp(font.baseline_v - static_cast<float>(y0) * font.scale_cell, 0.f, 1.f)};
}

} // namespace shared
