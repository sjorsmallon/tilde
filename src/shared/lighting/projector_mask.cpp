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

projector_mask_grid_t compute_projector_mask_grid_for_aspect(float aspect)
{
  if (!(aspect > 0.f))
    fatal_error("[mask] a projector's aspect is its width over its height, not {}", aspect);
  const float    limit = static_cast<float>(PROJECTOR_MASK_GRID_LIMIT);
  const uint32_t short_side =
      std::max<uint32_t>(1u, static_cast<uint32_t>(std::lround(aspect >= 1.f ? limit / aspect : limit * aspect)));
  return aspect >= 1.f ? projector_mask_grid_t{.columns = PROJECTOR_MASK_GRID_LIMIT, .rows = short_side}
                       : projector_mask_grid_t{.columns = short_side, .rows = PROJECTOR_MASK_GRID_LIMIT};
}

std::vector<bool> compute_dark_cells_of_picture(const assets::texture_asset_t& picture, uint32_t columns,
                                                uint32_t rows, float dark_threshold)
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

bool cover_dark_cells_with_rectangles(std::vector<bool>& dark, uint32_t columns, uint32_t rows,
                                      std::vector<projector_mask_quad_t>& out)
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
      out.push_back(make_projector_mask_quad(static_cast<float>(column) / static_cast<float>(columns),
                                             static_cast<float>(row) / static_cast<float>(rows),
                                             static_cast<float>(column_end) / static_cast<float>(columns),
                                             static_cast<float>(row_end) / static_cast<float>(rows)));
    }
  return false;
}

bool compute_projector_shadow_footprint(entity_uid_t projector, const scene_light_t& light,
                                        const linalg::mat4f& picture_matrix, Span<const shadow_volume_t> volumes,
                                        std::vector<bool>& cells, std::vector<projector_mask_quad_t>& out)
{
  const projector_mask_grid_t grid    = compute_projector_mask_grid_for_aspect(light.aspect);
  const uint32_t              COLUMNS = grid.columns;
  const uint32_t              ROWS    = grid.rows;
  cells.assign(static_cast<size_t>(COLUMNS) * ROWS, false);

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
    uint32_t column_end   = COLUMNS;
    uint32_t row_end      = ROWS;
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
        column_begin = static_cast<uint32_t>(std::floor(std::clamp(u0, 0.f, 1.f) * COLUMNS));
        row_begin    = static_cast<uint32_t>(std::floor(std::clamp(v0, 0.f, 1.f) * ROWS));
        column_end   = std::min(COLUMNS, static_cast<uint32_t>(std::ceil(std::clamp(u1, 0.f, 1.f) * COLUMNS)));
        row_end      = std::min(ROWS, static_cast<uint32_t>(std::ceil(std::clamp(v1, 0.f, 1.f) * ROWS)));
      }
    }

    for (uint32_t row = row_begin; row < row_end; ++row)
      for (uint32_t column = column_begin; column < column_end; ++column)
      {
        const float         u     = (static_cast<float>(column) + 0.5f) / static_cast<float>(COLUMNS);
        const float         v     = (static_cast<float>(row) + 0.5f) / static_cast<float>(ROWS);
        const linalg::vec3f point = top_left + across * u + down * v;
        bool                inside = true;
        for (uint32_t plane = 0; plane < volume.ring_plane_count && inside; ++plane)
          inside = linalg::dot(volume.side_planes[plane].normal, point - volume.side_planes[plane].point) <= 0.f;
        if (inside)
        {
          cells[static_cast<size_t>(row) * COLUMNS + column] = true;
          any_dark                                           = true;
        }
      }
  }

  if (!any_dark)
    return false;
  return cover_dark_cells_with_rectangles(cells, COLUMNS, ROWS, out);
}

projector_mask_t compute_projector_mask_from_cells(std::vector<bool> dark, uint32_t columns, uint32_t rows)
{
  projector_mask_t mask;
  mask.columns = columns;
  mask.rows    = rows;
  if (dark.size() != static_cast<size_t>(columns) * rows)
    fatal_error("[projector] a {} by {} grid has {} cells, not {}", columns, rows,
                static_cast<size_t>(columns) * rows, dark.size());
  mask.capped = cover_dark_cells_with_rectangles(dark, columns, rows, mask.quads);
  return mask;
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

std::optional<collision_piece_t> try_build_projector_mask_piece(const shadow_light_t&        light,
                                                                const projector_mask_quad_t& quad)
{
  // The slab's shape is known, so it is written down rather than hulled: two caps square to the axis and a side
  // through the apex and each edge, outward, wound counter-clockwise seen from outside as a hull's faces are.
  const float near_distance = projector_mask_glass_distance(light);
  const float far_distance  = near_distance * 1.25f;

  Array<uint32_t, 4> kept;
  uint32_t           kept_count = 0;
  for (uint32_t index = 0; index < 4; ++index)
  {
    const linalg::vec2f& corner = quad.corners[index];
    bool                 repeated = false;
    for (uint32_t other = 0; other < kept_count; ++other)
      repeated |= linalg::length(corner - quad.corners[kept[other]]) < 1e-6f;
    if (!repeated)
      kept[kept_count++] = index;
  }
  if (kept_count < 3)
    return std::nullopt;

  Array<linalg::vec3, 4> near_corners;
  Array<linalg::vec3, 4> far_corners;
  Array<linalg::vec3, 8> every_corner;
  linalg::vec3           centre = {0.f, 0.f, 0.f};
  for (uint32_t index = 0; index < kept_count; ++index)
  {
    const linalg::vec2f& corner = quad.corners[kept[index]];
    near_corners[index]         = projector_frame_point(light, corner.u, corner.v, near_distance);
    far_corners[index]          = projector_frame_point(light, corner.u, corner.v, far_distance);
    every_corner[index]         = near_corners[index];
    every_corner[kept_count + index] = far_corners[index];
    centre = centre + (near_corners[index] + far_corners[index]) * (0.5f / static_cast<float>(kept_count));
  }

  collision_piece_t piece;
  piece.bounds = compute_point_bounds(Span<const linalg::vec3>(every_corner.data, kept_count * 2));
  const auto add_face = [&](std::vector<linalg::vec3> polygon, linalg::vec3 normal) -> bool
  {
    if (linalg::length(normal) < 1e-6f)
      return false;
    normal = linalg::normalize(normal);
    if (linalg::dot(normal, polygon[0] - centre) < 0.f)
      normal = normal * -1.f;
    if (linalg::dot(linalg::cross(polygon[1] - polygon[0], polygon[2] - polygon[0]), normal) < 0.f)
      std::reverse(polygon.begin(), polygon.end());
    piece.planes.push_back(Plane{.point = polygon[0], .normal = normal});
    piece.face_polygons.push_back(std::move(polygon));
    return true;
  };

  std::vector<linalg::vec3> near_cap(near_corners.begin(), near_corners.begin() + kept_count);
  std::vector<linalg::vec3> far_cap(far_corners.begin(), far_corners.begin() + kept_count);
  if (!add_face(std::move(near_cap), light.direction) || !add_face(std::move(far_cap), light.direction))
    return std::nullopt;
  for (uint32_t index = 0; index < kept_count; ++index)
  {
    const uint32_t next = (index + 1) % kept_count;
    if (!add_face({near_corners[index], near_corners[next], far_corners[next], far_corners[index]},
                  linalg::cross(near_corners[next] - near_corners[index], far_corners[index] - near_corners[index])))
      return std::nullopt;
  }
  return piece;
}

} // namespace shared
