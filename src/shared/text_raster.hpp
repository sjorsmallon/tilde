#pragma once

// A grid of characters drawn as one coverage picture: `columns` by `rows` cells over `width` by `height` pixels,
// each glyph scaled so the font's advance fills a cell's width (a monospace cell IS the advance box, so glyphs
// drawn edge to edge join) and its ascent-to-descent line is centred on the cell's height. `cell_aspect` is a
// cell's width over its height where the picture lands, so a glyph is drawn uniform there rather than on these
// pixels. Coverage runs 0 blank to 255 inked, row-major; `characters` holds
// columns * rows of them, a space or anything outside printable ASCII blank; `coverage` holds width * height
// bytes the caller owns. Dies on a length mismatch or bytes that carry no font. This TU owns stb_truetype's
// implementation.

#include "span.hpp"

#include <cstdint>
#include <optional>

namespace shared
{

void rasterise_character_grid(Span<const uint8_t> font_bytes, Span<const char> characters, uint32_t columns,
                              uint32_t rows, uint32_t width, uint32_t height, float cell_aspect,
                              Span<uint8_t> coverage);

// Where a character's ink lies in its cell, u 0 to 1 across and v 0 to 1 down, placed as the grid above draws
// it; empty for a character with no ink.
struct character_ink_box_t
{
  float u0 = 0.f;
  float v0 = 0.f;
  float u1 = 0.f;
  float v1 = 0.f;
};

[[nodiscard]] std::optional<character_ink_box_t> try_compute_character_ink_box(Span<const uint8_t> font_bytes,
                                                                               char character, float cell_aspect);

} // namespace shared
