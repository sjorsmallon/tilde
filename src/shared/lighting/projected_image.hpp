#pragma once

// What a projector shows is the Projected_Image_Entity naming it: a texture under PROJECTED_IMAGE_ROWS lines of
// PROJECTED_IMAGE_COLUMNS characters. The text is the replicated state and the picture is derived from it on
// either end: the glyphs rasterised over the texture at the picture layer's resolution for the drawn light
// (compose_projected_image_cell), and as solids for the mask (projector_mask.hpp), the texture's dark cells
// on the projector's grid beside each character's quads (glyph_solids.hpp), recomputed whenever the picture's
// hash moves.

#include "assets/asset_id.hpp"
#include "assets/asset_types.hpp"
#include "entity_uid.hpp"
#include "projector_mask.hpp"

#include <cstdint>

namespace entities
{
struct Projected_Image_Entity;
}

namespace shared
{

struct Entity_System;
struct map_t;

// On a 16 by 9 frame a cell is 1 wide to 2 tall on the wall: the font's advance on its line, so glyphs abut.
inline constexpr uint32_t PROJECTED_IMAGE_COLUMNS = 32;
inline constexpr uint32_t PROJECTED_IMAGE_ROWS    = 9;
// The glyphs' font; its advance is half its ascent-to-descent line, which the grid above is shaped by.
inline constexpr assets::asset_name_t PROJECTED_IMAGE_FONT = "Consolas_Regular";

// The first image naming `projector`, or null when none does: a projector with no image is a white frame.
// The map overload is the editor's, which lays out lights from the map and not a session.
[[nodiscard]] const entities::Projected_Image_Entity* find_projected_image_for_projector(const Entity_System& system,
                                                                                            entity_uid_t projector);
[[nodiscard]] const entities::Projected_Image_Entity* find_projected_image_for_projector(const map_t& map,
                                                                                            entity_uid_t projector);

// One character; `column` below PROJECTED_IMAGE_COLUMNS, `row` below PROJECTED_IMAGE_ROWS. A line is padded with
// spaces up to a set column, and reads as spaces past its length.
[[nodiscard]] char projected_image_character_at(const entities::Projected_Image_Entity& image, uint32_t column,
                                                uint32_t row);
void               set_projected_image_character(entities::Projected_Image_Entity& image, uint32_t column, uint32_t row,
                                                 char character);
// Whether any line holds anything but spaces.
[[nodiscard]] bool projected_image_has_text(const entities::Projected_Image_Entity& image);

// A text cell's width over its height where the picture lands, for a frame of `projector_aspect`.
[[nodiscard]] float compute_projected_image_cell_aspect(float projector_aspect);

// The text in black over white, `width` by `height` rgba, u right and v down as the texture's rows, the glyphs
// uniform on a frame of `projector_aspect`.
[[nodiscard]] assets::texture_asset_t rasterise_projected_image_text(const entities::Projected_Image_Entity& image,
                                                                     uint32_t width, uint32_t height,
                                                                     float projector_aspect);

// The pixels of one text cell of a `width` by `height` picture: the columns split at floor(column * width / 32)
// and the rows alike, so the cells tile the picture exactly whatever its size.
struct projected_image_cell_pixels_t
{
  uint32_t x      = 0;
  uint32_t y      = 0;
  uint32_t width  = 0;
  uint32_t height = 0;
};
[[nodiscard]] projected_image_cell_pixels_t compute_projected_image_cell_pixels(uint32_t column, uint32_t row,
                                                                                uint32_t width, uint32_t height);

// One cell of the drawn picture, at the cell's own pixel size: its character in black over the texture's patch
// (white for Missing, sampled bilinearly at the picture's resolution), the glyph uniform on a frame of
// `projector_aspect`. What the client copies into the cell's pixels when its character changes.
[[nodiscard]] assets::texture_asset_t compose_projected_image_cell(const entities::Projected_Image_Entity& image,
                                                                   uint32_t column, uint32_t row, uint32_t width,
                                                                   uint32_t height, float projector_aspect);

// FNV-1a over the text, the texture and the frame's aspect, what the mask cache and the picture layer compare.
[[nodiscard]] uint64_t compute_projected_image_picture_hash(const entities::Projected_Image_Entity& image,
                                                            float                                   projector_aspect);

// The image's mask for a frame of `projector_aspect`: the texture's dark cells covered on the frame's grid and
// every character's quads, computed on the first ask and again whenever the picture's hash has moved, kept per
// image.
[[nodiscard]] const projector_mask_t& find_or_compute_projector_mask_for_image(projector_mask_cache_t& cache,
                                                                                const entities::Projected_Image_Entity& image,
                                                                                float projector_aspect);

} // namespace shared
