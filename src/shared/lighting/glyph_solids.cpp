#include "glyph_solids.hpp"

#include "log.hpp"
#include "text_raster.hpp"

namespace shared
{

namespace
{

constexpr projector_mask_quad_t box(float u0, float v0, float u1, float v1)
{
  return make_projector_mask_quad(u0, v0, u1, v1);
}

constexpr projector_mask_quad_t quad(float u0, float v0, float u1, float v1, float u2, float v2, float u3, float v3)
{
  return projector_mask_quad_t{.corners = {{{u0, v0}, {u1, v1}, {u2, v2}, {u3, v3}}}};
}

constexpr glyph_solid_t row(char character, projector_mask_quad_t a)
{
  return glyph_solid_t{.character = character, .quad_count = 1, .quads = {{a, {}, {}}}};
}

constexpr glyph_solid_t row(char character, projector_mask_quad_t a, projector_mask_quad_t b)
{
  return glyph_solid_t{.character = character, .quad_count = 2, .quads = {{a, b, {}}}};
}

constexpr glyph_solid_t row(char character, projector_mask_quad_t a, projector_mask_quad_t b, projector_mask_quad_t c)
{
  return glyph_solid_t{.character = character, .quad_count = 3, .quads = {{a, b, c}}};
}

// Corners in order round each quad; a slanted stroke is a parallelogram from its top edge to its bottom edge.
constexpr Array<glyph_solid_t, 10> GLYPH_SOLIDS = {{
    row('/', quad(0.70f, 0.05f, 0.95f, 0.05f, 0.30f, 0.95f, 0.05f, 0.95f)),
    row('\\', quad(0.05f, 0.05f, 0.30f, 0.05f, 0.95f, 0.95f, 0.70f, 0.95f)),
    row('_', box(0.00f, 0.82f, 1.00f, 0.95f)),
    row('-', box(0.10f, 0.45f, 0.90f, 0.57f)),
    row('|', box(0.42f, 0.05f, 0.58f, 0.95f)),
    row('T', box(0.08f, 0.08f, 0.92f, 0.26f), box(0.40f, 0.26f, 0.60f, 0.95f)),
    row('V', quad(0.05f, 0.05f, 0.28f, 0.05f, 0.62f, 0.95f, 0.38f, 0.95f),
        quad(0.72f, 0.05f, 0.95f, 0.05f, 0.62f, 0.95f, 0.38f, 0.95f)),
    row('L', box(0.15f, 0.05f, 0.38f, 0.95f), box(0.15f, 0.78f, 0.92f, 0.95f)),
    row('A', quad(0.38f, 0.05f, 0.62f, 0.05f, 0.28f, 0.95f, 0.05f, 0.95f),
        quad(0.38f, 0.05f, 0.62f, 0.05f, 0.95f, 0.95f, 0.72f, 0.95f), box(0.22f, 0.58f, 0.78f, 0.70f)),
    row('7', box(0.08f, 0.05f, 0.92f, 0.22f), quad(0.70f, 0.22f, 0.92f, 0.22f, 0.50f, 0.95f, 0.28f, 0.95f)),
}};

} // namespace

Span<const glyph_solid_t> glyph_solid_rows()
{
  return GLYPH_SOLIDS;
}

const glyph_solid_t* find_glyph_solid(char character)
{
  for (const glyph_solid_t& solid : GLYPH_SOLIDS)
    if (solid.character == character)
      return &solid;
  return nullptr;
}

void append_glyph_quads(Span<const uint8_t> font_bytes, char character, uint32_t column, uint32_t row,
                        uint32_t columns, uint32_t rows, float cell_aspect, std::vector<projector_mask_quad_t>& out)
{
  if (column >= columns || row >= rows)
    fatal_error("[glyph] character ({}, {}) is outside the {} by {} text", column, row, columns, rows);
  if (character <= ' ' || character > '~')
    return;

  const float cell_u = 1.f / static_cast<float>(columns);
  const float cell_v = 1.f / static_cast<float>(rows);
  const auto  placed = [&](const projector_mask_quad_t& in_cell)
  {
    projector_mask_quad_t placed_quad;
    for (uint32_t corner = 0; corner < 4; ++corner)
      placed_quad.corners[corner] = {(static_cast<float>(column) + in_cell.corners[corner].u) * cell_u,
                                     (static_cast<float>(row) + in_cell.corners[corner].v) * cell_v};
    return placed_quad;
  };

  if (const glyph_solid_t* solid = find_glyph_solid(character))
  {
    for (uint32_t index = 0; index < solid->quad_count; ++index)
      out.push_back(placed(solid->quads[index]));
    return;
  }
  if (const std::optional<character_ink_box_t> ink = try_compute_character_ink_box(font_bytes, character, cell_aspect))
    out.push_back(placed(make_projector_mask_quad(ink->u0, ink->v0, ink->u1, ink->v1)));
}

} // namespace shared
