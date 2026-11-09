#include "projected_image.hpp"

#include "assets/asset.hpp"
#include "entities/entity_reflection.hpp"
#include "entities/generated/entities/projected_image_entity_generated.hpp"
#include "entity_system.hpp"
#include "glyph_solids.hpp"
#include "log.hpp"
#include "map/map.hpp"
#include "text_raster.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <vector>

namespace shared
{

using image_line_t = network::pascal_string_t<PROJECTED_IMAGE_COLUMNS>;

static_assert(Enum_Array<entities::Image_Row, image_line_t>::count == PROJECTED_IMAGE_ROWS,
              "an image has one line per row");
static_assert(std::is_same_v<decltype(entities::Projected_Image_Entity::text), Enum_Array<entities::Image_Row, image_line_t>>,
              "a line holds PROJECTED_IMAGE_COLUMNS characters");

namespace
{

void check_character_is_on_grid(uint32_t column, uint32_t row)
{
  if (column >= PROJECTED_IMAGE_COLUMNS || row >= PROJECTED_IMAGE_ROWS)
    fatal_error("[image] character ({}, {}) is outside the {} by {} text", column, row, PROJECTED_IMAGE_COLUMNS,
                PROJECTED_IMAGE_ROWS);
}

const assets::texture_asset_t& get_picture_texture(const entities::Projected_Image_Entity& image)
{
  const assets::asset_handle_t<assets::texture_asset_t> handle  = assets::get_texture(image.texture);
  const assets::texture_asset_t*                        picture = handle.valid() ? assets::get(handle) : nullptr;
  if (picture == nullptr)
    fatal_error("[image] {} names texture '{}', which did not load", image.entity_id, assets::to_string(image.texture));
  if (picture->width <= 0 || picture->height <= 0 || picture->channels != 4 ||
      picture->pixels.size() < static_cast<size_t>(picture->width) * static_cast<size_t>(picture->height) * 4)
    fatal_error("[image] {} names texture '{}', which is not an rgba picture", image.entity_id,
                assets::to_string(image.texture));
  return *picture;
}

// The texture at a texel-space point, clamped at its edges, as the picture layer's linear blit reads it.
Array<uint8_t, 3> sample_texture_bilinear(const assets::texture_asset_t& texture, float x, float y)
{
  const int32_t last_x = texture.width - 1;
  const int32_t last_y = texture.height - 1;
  const float   fx     = std::clamp(x, 0.f, static_cast<float>(last_x));
  const float   fy     = std::clamp(y, 0.f, static_cast<float>(last_y));
  const int32_t x0     = static_cast<int32_t>(fx);
  const int32_t y0     = static_cast<int32_t>(fy);
  const int32_t x1     = std::min(x0 + 1, last_x);
  const int32_t y1     = std::min(y0 + 1, last_y);
  const float   tx     = fx - static_cast<float>(x0);
  const float   ty     = fy - static_cast<float>(y0);
  const auto    texel  = [&](int32_t tx_index, int32_t ty_index)
  { return &texture.pixels[(static_cast<size_t>(ty_index) * texture.width + tx_index) * 4]; };
  const uint8_t* a = texel(x0, y0);
  const uint8_t* b = texel(x1, y0);
  const uint8_t* c = texel(x0, y1);
  const uint8_t* d = texel(x1, y1);
  Array<uint8_t, 3> out;
  for (uint32_t channel = 0; channel < 3; ++channel)
  {
    const float top    = a[channel] + (b[channel] - a[channel]) * tx;
    const float bottom = c[channel] + (d[channel] - c[channel]) * tx;
    out[channel]       = static_cast<uint8_t>(std::clamp(top + (bottom - top) * ty + 0.5f, 0.f, 255.f));
  }
  return out;
}

Span<const uint8_t> get_projected_image_font_bytes()
{
  const assets::asset_handle_t<assets::font_asset_t> handle = assets::get_font(assets::font_id(PROJECTED_IMAGE_FONT));
  const assets::font_asset_t*                        font   = handle.valid() ? assets::get(handle) : nullptr;
  if (font == nullptr || font->bytes.count == 0)
    fatal_error("[image] the font '{}' did not load", PROJECTED_IMAGE_FONT.text);
  return font->bytes;
}

projector_mask_t compute_projector_mask_from_image(const entities::Projected_Image_Entity& image,
                                                   float projector_aspect, uint64_t picture_hash)
{
  const projector_mask_grid_t grid = compute_projector_mask_grid_for_aspect(projector_aspect);
  projector_mask_t            mask;
  if (image.texture != assets::texture_asset::Missing)
    mask = compute_projector_mask_from_cells(compute_dark_cells_of_picture(get_picture_texture(image), grid.columns,
                                                                           grid.rows, PROJECTOR_MASK_DARK_THRESHOLD),
                                             grid.columns, grid.rows);
  mask.columns      = grid.columns;
  mask.rows         = grid.rows;
  mask.image        = image.entity_id;
  mask.picture_hash = picture_hash;
  if (mask.capped)
    log_warning("[image] {} has more dark than {} rectangles cover; the rest is dark but not solid", image.entity_id,
                MAX_PROJECTOR_MASK_RECTANGLES);

  if (projected_image_has_text(image))
  {
    const Span<const uint8_t> font        = get_projected_image_font_bytes();
    const float               cell_aspect = compute_projected_image_cell_aspect(projector_aspect);
    for (uint32_t row = 0; row < PROJECTED_IMAGE_ROWS; ++row)
      for (uint32_t column = 0; column < PROJECTED_IMAGE_COLUMNS; ++column)
        append_glyph_quads(font, projected_image_character_at(image, column, row), column, row,
                           PROJECTED_IMAGE_COLUMNS, PROJECTED_IMAGE_ROWS, cell_aspect, mask.quads);
  }
  return mask;
}

} // namespace

const entities::Projected_Image_Entity* find_projected_image_for_projector(const Entity_System& system,
                                                                             entity_uid_t         projector)
{
  for (const entities::Projected_Image_Entity& image : system.entities_of_type<entities::Projected_Image_Entity>())
    if (image.projector == projector)
      return &image;
  return nullptr;
}

const entities::Projected_Image_Entity* find_projected_image_for_projector(const map_t& map, entity_uid_t projector)
{
  for (const map_entity_t& entry : map.entities)
    if (const entities::Projected_Image_Entity* image =
            entities::entity_as<entities::Projected_Image_Entity>(entry.entity.get());
        image != nullptr && image->projector == projector)
      return image;
  return nullptr;
}

char projected_image_character_at(const entities::Projected_Image_Entity& image, uint32_t column, uint32_t row)
{
  check_character_is_on_grid(column, row);
  const image_line_t& line = image.text.values[row];
  return column < line.length ? line.data[column] : ' ';
}

void set_projected_image_character(entities::Projected_Image_Entity& image, uint32_t column, uint32_t row,
                                   char character)
{
  check_character_is_on_grid(column, row);
  image_line_t& line = image.text.values[row];
  for (; line.length <= column; ++line.length)
    line.data[line.length] = ' ';
  line.data[column] = character;
}

bool projected_image_has_text(const entities::Projected_Image_Entity& image)
{
  for (const image_line_t& line : image.text.values)
    for (uint32_t column = 0; column < line.length; ++column)
      if (line.data[column] != ' ')
        return true;
  return false;
}

float compute_projected_image_cell_aspect(float projector_aspect)
{
  if (!(projector_aspect > 0.f))
    fatal_error("[image] a projector's aspect is its width over its height, not {}", projector_aspect);
  return projector_aspect * static_cast<float>(PROJECTED_IMAGE_ROWS) / static_cast<float>(PROJECTED_IMAGE_COLUMNS);
}

assets::texture_asset_t rasterise_projected_image_text(const entities::Projected_Image_Entity& image, uint32_t width,
                                                       uint32_t height, float projector_aspect)
{
  std::vector<char> characters(static_cast<size_t>(PROJECTED_IMAGE_COLUMNS) * PROJECTED_IMAGE_ROWS, ' ');
  for (uint32_t row = 0; row < PROJECTED_IMAGE_ROWS; ++row)
    for (uint32_t column = 0; column < PROJECTED_IMAGE_COLUMNS; ++column)
      characters[static_cast<size_t>(row) * PROJECTED_IMAGE_COLUMNS + column] =
          projected_image_character_at(image, column, row);

  std::vector<uint8_t> coverage(static_cast<size_t>(width) * height, 0);
  rasterise_character_grid(get_projected_image_font_bytes(), Span<const char>(characters), PROJECTED_IMAGE_COLUMNS,
                           PROJECTED_IMAGE_ROWS, width, height, compute_projected_image_cell_aspect(projector_aspect),
                           Span<uint8_t>(coverage));

  assets::texture_asset_t picture;
  picture.width    = static_cast<int32_t>(width);
  picture.height   = static_cast<int32_t>(height);
  picture.channels = 4;
  picture.pixels.resize(coverage.size() * 4);
  for (size_t pixel = 0; pixel < coverage.size(); ++pixel)
  {
    const uint8_t light          = static_cast<uint8_t>(255u - coverage[pixel]);
    picture.pixels[pixel * 4 + 0] = light;
    picture.pixels[pixel * 4 + 1] = light;
    picture.pixels[pixel * 4 + 2] = light;
    picture.pixels[pixel * 4 + 3] = 255;
  }
  return picture;
}

projected_image_cell_pixels_t compute_projected_image_cell_pixels(uint32_t column, uint32_t row, uint32_t width,
                                                                  uint32_t height)
{
  check_character_is_on_grid(column, row);
  const uint32_t x0 = column * width / PROJECTED_IMAGE_COLUMNS;
  const uint32_t x1 = (column + 1) * width / PROJECTED_IMAGE_COLUMNS;
  const uint32_t y0 = row * height / PROJECTED_IMAGE_ROWS;
  const uint32_t y1 = (row + 1) * height / PROJECTED_IMAGE_ROWS;
  if (x1 <= x0 || y1 <= y0)
    fatal_error("[image] a {} by {} picture has no pixels for cell ({}, {})", width, height, column, row);
  return projected_image_cell_pixels_t{.x = x0, .y = y0, .width = x1 - x0, .height = y1 - y0};
}

assets::texture_asset_t compose_projected_image_cell(const entities::Projected_Image_Entity& image, uint32_t column,
                                                     uint32_t row, uint32_t width, uint32_t height,
                                                     float projector_aspect)
{
  const projected_image_cell_pixels_t cell      = compute_projected_image_cell_pixels(column, row, width, height);
  const char                          character = projected_image_character_at(image, column, row);
  std::vector<uint8_t>                coverage(static_cast<size_t>(cell.width) * cell.height, 0);
  if (character != ' ')
    rasterise_character_grid(get_projected_image_font_bytes(), Span<const char>(&character, 1), 1, 1, cell.width,
                             cell.height, compute_projected_image_cell_aspect(projector_aspect), Span<uint8_t>(coverage));

  assets::texture_asset_t picture;
  picture.width    = static_cast<int32_t>(cell.width);
  picture.height   = static_cast<int32_t>(cell.height);
  picture.channels = 4;
  picture.pixels.resize(coverage.size() * 4);

  const assets::texture_asset_t* texture = image.texture != assets::texture_asset::Missing ? &get_picture_texture(image) : nullptr;
  for (uint32_t y = 0; y < cell.height; ++y)
    for (uint32_t x = 0; x < cell.width; ++x)
    {
      const size_t   index = static_cast<size_t>(y) * cell.width + x;
      const uint32_t light = 255u - coverage[index];
      uint8_t*       pixel = &picture.pixels[index * 4];
      pixel[3]             = 255;
      if (texture == nullptr)
      {
        pixel[0] = pixel[1] = pixel[2] = static_cast<uint8_t>(light);
        continue;
      }
      const float texel_x = (static_cast<float>(cell.x + x) + 0.5f) * static_cast<float>(texture->width) /
                                static_cast<float>(width) - 0.5f;
      const float texel_y = (static_cast<float>(cell.y + y) + 0.5f) * static_cast<float>(texture->height) /
                                static_cast<float>(height) - 0.5f;
      const Array<uint8_t, 3> texel = sample_texture_bilinear(*texture, texel_x, texel_y);
      for (uint32_t channel = 0; channel < 3; ++channel)
        pixel[channel] = static_cast<uint8_t>(texel[channel] * light / 255u);
    }
  return picture;
}

uint64_t compute_projected_image_picture_hash(const entities::Projected_Image_Entity& image, float projector_aspect)
{
  uint64_t   digest = 0xCBF29CE484222325ull;
  const auto mix    = [&digest](uint64_t value, uint32_t bytes)
  {
    for (uint32_t byte = 0; byte < bytes; ++byte)
    {
      digest ^= (value >> (byte * 8)) & 0xFFu;
      digest *= 0x100000001B3ull;
    }
  };
  for (const image_line_t& line : image.text.values)
  {
    mix(line.length, 1);
    for (uint32_t column = 0; column < line.length; ++column)
      mix(static_cast<uint8_t>(line.data[column]), 1);
  }
  mix(static_cast<uint64_t>(image.texture), 4);
  mix(std::bit_cast<uint32_t>(projector_aspect), 4);
  return digest;
}

const projector_mask_t& find_or_compute_projector_mask_for_image(projector_mask_cache_t&                  cache,
                                                                  const entities::Projected_Image_Entity& image,
                                                                  float                                   projector_aspect)
{
  const uint64_t picture_hash = compute_projected_image_picture_hash(image, projector_aspect);
  for (projector_mask_t& mask : cache.masks)
    if (mask.image == image.entity_id)
    {
      if (mask.picture_hash != picture_hash)
        mask = compute_projector_mask_from_image(image, projector_aspect, picture_hash);
      return mask;
    }
  cache.masks.push_back(compute_projector_mask_from_image(image, projector_aspect, picture_hash));
  return cache.masks.back();
}

} // namespace shared
