#pragma once

// The solid of a typed character (projector_plan.md ss3b, the typewriter): a few convex quads in its cell, u 0
// to 1 across and v 0 to 1 down, hand-authored where the shape matters for play (a slash is a slope, a T is a
// bar on a stem, a V is two leaning strokes) and otherwise one box round the glyph's ink as the font places it.
// The quads are placed into picture uv by the character's column and row of the text grid; the ball bounces
// off them and the projector casts them from its glass like any dark of its picture (projector_mask.hpp).

#include "array.hpp"
#include "projector_mask.hpp"
#include "span.hpp"

#include <cstdint>
#include <vector>

namespace shared
{

inline constexpr uint32_t MAX_GLYPH_SOLID_QUADS = 3;

struct glyph_solid_t
{
  char                                               character  = ' ';
  uint32_t                                           quad_count = 0;
  Array<projector_mask_quad_t, MAX_GLYPH_SOLID_QUADS> quads;
};

// The hand-authored rows. A character with no row is solid as its ink box.
[[nodiscard]] Span<const glyph_solid_t> glyph_solid_rows();
[[nodiscard]] const glyph_solid_t*      find_glyph_solid(char character);

// `character`'s quads at (column, row) of a `columns` by `rows` text over the picture, appended to `out`: the
// table's row scaled into the cell, or the ink box from `font_bytes` (text_raster.hpp) for a character without
// one. `cell_aspect` is a cell's width over its height where the picture lands. Nothing for a blank.
void append_glyph_quads(Span<const uint8_t> font_bytes, char character, uint32_t column, uint32_t row,
                        uint32_t columns, uint32_t rows, float cell_aspect, std::vector<projector_mask_quad_t>& out);

} // namespace shared
