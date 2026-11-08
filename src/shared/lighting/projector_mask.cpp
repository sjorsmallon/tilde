#include "projector_mask.hpp"

#include "assets/asset.hpp"
#include "geometry/convex_hull.hpp"
#include "lighting.hpp"
#include "log.hpp"
#include "shadow_volume.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

namespace
{

std::vector<bool> compute_dark_cells(const assets::texture_asset_t& picture, uint32_t columns, uint32_t rows,
                                     float dark_threshold)
{
  const uint32_t    width  = static_cast<uint32_t>(picture.width);
  const uint32_t    height = static_cast<uint32_t>(picture.height);
  const float       dark_sum_limit = dark_threshold * 255.f * 3.f;
  std::vector<bool> dark(static_cast<size_t>(columns) * rows, false);
  for (uint32_t row = 0; row < rows; ++row)
  {
    const uint32_t texel_row_begin = row * height / rows;
    const uint32_t texel_row_end   = std::max((row + 1) * height / rows, texel_row_begin + 1);
    for (uint32_t column = 0; column < columns; ++column)
    {
      const uint32_t texel_column_begin = column * width / columns;
      const uint32_t texel_column_end   = std::max((column + 1) * width / columns, texel_column_begin + 1);
      uint64_t       sum                = 0;
      uint64_t       texels             = 0;
      for (uint32_t y = texel_row_begin; y < texel_row_end; ++y)
        for (uint32_t x = texel_column_begin; x < texel_column_end; ++x)
        {
          const uint8_t* texel = &picture.pixels[(static_cast<size_t>(y) * width + x) * 4];
          sum += texel[0] + texel[1] + texel[2];
          ++texels;
        }
      dark[static_cast<size_t>(row) * columns + column] =
          static_cast<float>(sum) < dark_sum_limit * static_cast<float>(texels);
    }
  }
  return dark;
}

} // namespace

bool cover_dark_cells_with_rectangles(std::vector<bool>& dark, uint32_t columns, uint32_t rows,
                                      std::vector<projector_mask_rectangle_t>& out)
{
  const auto cell = [&](uint32_t column, uint32_t row) -> std::vector<bool>::reference
  { return dark[static_cast<size_t>(row) * columns + column]; };

  const size_t first = out.size();
  for (uint32_t row = 0; row < rows; ++row)
    for (uint32_t column = 0; column < columns; ++column)
    {
      if (!cell(column, row))
        continue;
      if (out.size() - first >= MAX_PROJECTOR_MASK_RECTANGLES)
        return true;
      uint32_t column_end = column + 1;
      while (column_end < columns && cell(column_end, row))
        ++column_end;
      uint32_t row_end = row + 1;
      for (; row_end < rows; ++row_end)
      {
        bool whole_span_dark = true;
        for (uint32_t c = column; c < column_end && whole_span_dark; ++c)
          whole_span_dark = cell(c, row_end);
        if (!whole_span_dark)
          break;
      }
      for (uint32_t r = row; r < row_end; ++r)
        for (uint32_t c = column; c < column_end; ++c)
          cell(c, r) = false;
      out.push_back({.u0 = static_cast<float>(column) / static_cast<float>(columns),
                     .v0 = static_cast<float>(row) / static_cast<float>(rows),
                     .u1 = static_cast<float>(column_end) / static_cast<float>(columns),
                     .v1 = static_cast<float>(row_end) / static_cast<float>(rows)});
    }
  return false;
}

bool compute_projector_shadow_footprint(entity_uid_t projector, const scene_light_t& light,
                                        const linalg::mat4f& picture_matrix, Span<const shadow_volume_t> volumes,
                                        std::vector<bool>& cells, std::vector<projector_mask_rectangle_t>& out)
{
  constexpr uint32_t CELLS = PROJECTOR_MASK_GRID_LIMIT;
  cells.assign(static_cast<size_t>(CELLS) * CELLS, false);

  const Array<linalg::vec3f, 4> corners  = compute_projector_frame_corners(light, 1.f);
  const linalg::vec3f           top_left = corners[3];
  const linalg::vec3f           across   = corners[2] - corners[3];
  const linalg::vec3f           down     = corners[0] - corners[3];

  bool any_dark = false;
  for (const shadow_volume_t& volume : volumes)
  {
    if (volume.light != projector || !volume.caster_casts_from_lens)
      continue;

    uint32_t column_begin = 0;
    uint32_t row_begin    = 0;
    uint32_t column_end   = CELLS;
    uint32_t row_end      = CELLS;
    {
      float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
      bool  behind_the_lens = false;
      for (int corner = 0; corner < 8; ++corner)
      {
        const linalg::vec3f point{(corner & 1) ? volume.caster_bounds.max.x : volume.caster_bounds.min.x,
                                  (corner & 2) ? volume.caster_bounds.max.y : volume.caster_bounds.min.y,
                                  (corner & 4) ? volume.caster_bounds.max.z : volume.caster_bounds.min.z};
        const linalg::vec4  clip = picture_matrix * linalg::vec4{point.x, point.y, point.z, 1.f};
        if (clip.w <= 0.f)
        {
          behind_the_lens = true;
          break;
        }
        u0 = std::min(u0, (clip.x / clip.w) * 0.5f + 0.5f);
        v0 = std::min(v0, (clip.y / clip.w) * 0.5f + 0.5f);
        u1 = std::max(u1, (clip.x / clip.w) * 0.5f + 0.5f);
        v1 = std::max(v1, (clip.y / clip.w) * 0.5f + 0.5f);
      }
      if (!behind_the_lens)
      {
        if (u1 <= 0.f || u0 >= 1.f || v1 <= 0.f || v0 >= 1.f)
          continue;
        column_begin = static_cast<uint32_t>(std::floor(std::clamp(u0, 0.f, 1.f) * CELLS));
        row_begin    = static_cast<uint32_t>(std::floor(std::clamp(v0, 0.f, 1.f) * CELLS));
        column_end   = std::min(CELLS, static_cast<uint32_t>(std::ceil(std::clamp(u1, 0.f, 1.f) * CELLS)));
        row_end      = std::min(CELLS, static_cast<uint32_t>(std::ceil(std::clamp(v1, 0.f, 1.f) * CELLS)));
      }
    }

    for (uint32_t row = row_begin; row < row_end; ++row)
      for (uint32_t column = column_begin; column < column_end; ++column)
      {
        const float         u     = (static_cast<float>(column) + 0.5f) / static_cast<float>(CELLS);
        const float         v     = (static_cast<float>(row) + 0.5f) / static_cast<float>(CELLS);
        const linalg::vec3f point = top_left + across * u + down * v;
        bool                inside = true;
        for (uint32_t plane = 0; plane < volume.ring_plane_count && inside; ++plane)
          inside = linalg::dot(volume.side_planes[plane].normal, point - volume.side_planes[plane].point) <= 0.f;
        if (inside)
        {
          cells[static_cast<size_t>(row) * CELLS + column] = true;
          any_dark                                         = true;
        }
      }
  }

  if (!any_dark)
    return false;
  return cover_dark_cells_with_rectangles(cells, CELLS, CELLS, out);
}

projector_mask_t compute_projector_mask_from_picture(const assets::texture_asset_t& picture,
                                                     assets::texture_asset texture, float dark_threshold)
{
  projector_mask_t mask;
  mask.texture = texture;
  if (picture.width <= 0 || picture.height <= 0 || picture.channels != 4 ||
      picture.pixels.size() < static_cast<size_t>(picture.width) * static_cast<size_t>(picture.height) * 4)
    return mask;
  mask.columns = std::min<uint32_t>(static_cast<uint32_t>(picture.width), PROJECTOR_MASK_GRID_LIMIT);
  mask.rows    = std::min<uint32_t>(static_cast<uint32_t>(picture.height), PROJECTOR_MASK_GRID_LIMIT);

  std::vector<bool> dark = compute_dark_cells(picture, mask.columns, mask.rows, dark_threshold);
  mask.capped            = cover_dark_cells_with_rectangles(dark, mask.columns, mask.rows, mask.rectangles);
  if (mask.capped)
    log_warning("[projector] the picture '{}' has more dark than {} rectangles cover; the rest is dark but not solid",
                assets::to_string(texture), MAX_PROJECTOR_MASK_RECTANGLES);
  return mask;
}

const projector_mask_t& find_or_compute_projector_mask(projector_mask_cache_t& cache, assets::texture_asset texture)
{
  for (const projector_mask_t& mask : cache.masks)
    if (mask.texture == texture)
      return mask;
  projector_mask_t mask;
  mask.texture = texture;
  if (texture != assets::texture_asset::Missing)
  {
    const assets::asset_handle_t<assets::texture_asset_t> handle = assets::get_texture(texture);
    const assets::texture_asset_t*                        picture = handle.valid() ? assets::get(handle) : nullptr;
    if (picture != nullptr)
      mask = compute_projector_mask_from_picture(*picture, texture, PROJECTOR_MASK_DARK_THRESHOLD);
  }
  cache.masks.push_back(std::move(mask));
  return cache.masks.back();
}

linalg::vec3f projector_frame_point(const shadow_light_t& light, float u, float v, float distance)
{
  const Array<linalg::vec3f, 4>& corners      = light.frame_corners_at_unit_distance;
  const linalg::vec3f            top_left     = corners[3];
  const linalg::vec3f            across       = corners[2] - corners[3];
  const linalg::vec3f            down         = corners[0] - corners[3];
  const linalg::vec3f            at_unit      = top_left + across * u + down * v;
  return light.apex + (at_unit - light.apex) * distance;
}

float projector_mask_glass_distance(const shadow_light_t& light)
{
  const float half_height_at_unit =
      0.5f * linalg::length(light.frame_corners_at_unit_distance[3] - light.frame_corners_at_unit_distance[0]);
  float distance = PROJECTOR_MASK_GLASS_HALF_HEIGHT / std::max(half_height_at_unit, 1e-4f);
  if (light.range > 0.f)
    distance = std::min(distance, light.range * 0.5f);
  return distance;
}

std::optional<collision_piece_t> try_build_projector_mask_piece(const shadow_light_t&             light,
                                                                const projector_mask_rectangle_t& rectangle)
{
  const float            near_distance = projector_mask_glass_distance(light);
  const float            far_distance  = near_distance * 1.25f;
  Array<linalg::vec3, 8> corners;
  uint32_t               count = 0;
  for (const float distance : {near_distance, far_distance})
  {
    corners[count++] = projector_frame_point(light, rectangle.u0, rectangle.v0, distance);
    corners[count++] = projector_frame_point(light, rectangle.u1, rectangle.v0, distance);
    corners[count++] = projector_frame_point(light, rectangle.u1, rectangle.v1, distance);
    corners[count++] = projector_frame_point(light, rectangle.u0, rectangle.v1, distance);
  }
  const std::optional<convex_polyhedron_t> hull = try_build_convex_hull(corners);
  if (!hull)
    return std::nullopt;
  return collision_piece_t{.bounds        = compute_point_bounds(hull->vertices),
                           .planes        = compute_polyhedron_planes(*hull),
                           .face_polygons = compute_polyhedron_face_polygons(*hull)};
}

} // namespace shared
