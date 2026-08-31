#include "lightmap_debug_image.hpp"
#include "lightmap_solve.hpp"

#include "log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace shared
{

namespace
{

struct rgb_t
{
  uint8_t r, g, b;
};

rgb_t chart_color(size_t chart_index)
{
  const uint32_t hash = (uint32_t)chart_index * 2654435761u;
  return {(uint8_t)(80 + (hash & 0x7f)), (uint8_t)(80 + ((hash >> 8) & 0x7f)),
          (uint8_t)(80 + ((hash >> 16) & 0x7f))};
}

struct page_image_t
{
  int size = 0;
  std::vector<uint8_t> pixels;

  void put(int x, int y, rgb_t color)
  {
    if (x < 0 || y < 0 || x >= size || y >= size) return;
    const size_t offset = ((size_t)y * (size_t)size + (size_t)x) * 3;
    pixels[offset + 0] = color.r;
    pixels[offset + 1] = color.g;
    pixels[offset + 2] = color.b;
  }

  void fill_rect(int x0, int y0, int width, int height, rgb_t color)
  {
    for (int y = y0; y < y0 + height; ++y)
      for (int x = x0; x < x0 + width; ++x)
        put(x, y, color);
  }

  void line(int x0, int y0, int x1, int y1, rgb_t color)
  {
    const int delta_x = std::abs(x1 - x0);
    const int delta_y = -std::abs(y1 - y0);
    const int step_x = x0 < x1 ? 1 : -1;
    const int step_y = y0 < y1 ? 1 : -1;
    int error = delta_x + delta_y;

    while (true)
    {
      put(x0, y0, color);
      if (x0 == x1 && y0 == y1) break;
      const int doubled_error = 2 * error;
      if (doubled_error >= delta_y) { error += delta_y; x0 += step_x; }
      if (doubled_error <= delta_x) { error += delta_x; y0 += step_y; }
    }
  }
};

} // namespace

bool try_write_lightmap_debug_png(const std::vector<lightmap_chart_t> &charts,
                                  const lightmap_atlas_t &atlas,
                                  const lightmap_bake_settings_t &settings,
                                  const std::string &path_prefix)
{
  if (atlas.size_in_texels <= 0 || atlas.page_count <= 0)
  {
    log_error("[lightmap] cannot write a debug image for an atlas of {} pages at {} "
              "texels.", atlas.page_count, atlas.size_in_texels);
    return false;
  }

  bool wrote_every_page = true;

  for (int page = 0; page < atlas.page_count; ++page)
  {
    page_image_t image;
    image.size = atlas.size_in_texels;
    image.pixels.assign((size_t)image.size * (size_t)image.size * 3, 16);

    for (size_t chart_index = 0; chart_index < charts.size(); ++chart_index)
    {
      const lightmap_chart_t &chart = charts[chart_index];
      if (chart.page != page) continue;

      const rgb_t color = chart_color(chart_index);
      const rgb_t dim = {(uint8_t)(color.r / 3), (uint8_t)(color.g / 3),
                         (uint8_t)(color.b / 3)};

      image.fill_rect(chart.atlas_rect.min_x, chart.atlas_rect.min_y, chart.atlas_rect.width,
                      chart.atlas_rect.height, dim);

      const int gutter = settings.gutter_in_texels;
      image.fill_rect(chart.atlas_rect.min_x + gutter, chart.atlas_rect.min_y + gutter,
                      chart.atlas_rect.width - 2 * gutter,
                      chart.atlas_rect.height - 2 * gutter, color);

      if (chart.polygon.size() < 2 || chart.world_units_per_texel <= 0.f) continue;

      for (size_t i = 0; i < chart.polygon.size(); ++i)
      {
        const linalg::vec2 from =
            chart_space_to_atlas_texel(chart, settings, chart.polygon[i]);
        const linalg::vec2 to = chart_space_to_atlas_texel(
            chart, settings, chart.polygon[(i + 1) % chart.polygon.size()]);
        image.line((int)std::lround(from.x), (int)std::lround(from.y),
                   (int)std::lround(to.x), (int)std::lround(to.y), {255, 255, 255});
      }
    }

    const std::string path = path_prefix + "_page" + std::to_string(page) + ".png";
    if (!stbi_write_png(path.c_str(), image.size, image.size, 3, image.pixels.data(),
                        image.size * 3))
    {
      log_error("[lightmap] could not write the debug image '{}'.", path);
      wrote_every_page = false;
      continue;
    }
    log_terminal("[lightmap] wrote {} ({}x{})", path, image.size, image.size);
  }

  return wrote_every_page;
}

// The pages are HDR and a PNG is not, so this is a VIEW rather than a copy:
// Reinhard tone map at the caller's exposure, then encode sRGB. Both halves are
// needed -- without the tone map every texel past 1.0 clips to white and a
// blown-out bake looks identical to a correct one, and without the sRGB encode
// the midtones read far darker than what the shader will put on screen.
bool try_write_lightmap_pages_png(const lightmap_pages_t &pages,
                                  const std::string &path_prefix, float exposure)
{
  if (pages.size_in_texels <= 0 || pages.page_count <= 0)
  {
    log_error("[lightmap] cannot write {} page(s) at {} texels.", pages.page_count,
              pages.size_in_texels);
    return false;
  }

  const auto to_display_byte = [exposure](float linear) {
    const float exposed = std::max(linear, 0.f) * exposure;
    const float tone_mapped = exposed / (1.f + exposed);
    const float encoded = tone_mapped <= 0.0031308f
                              ? tone_mapped * 12.92f
                              : 1.055f * std::pow(tone_mapped, 1.f / 2.4f) - 0.055f;
    return (uint8_t)std::clamp((int)std::lround(encoded * 255.f), 0, 255);
  };

  bool wrote_every_page = true;
  std::vector<uint8_t> image;

  for (int page = 0; page < pages.page_count; ++page)
  {
    image.assign((size_t)pages.size_in_texels * (size_t)pages.size_in_texels * 3, 0);

    for (int y = 0; y < pages.size_in_texels; ++y)
    {
      for (int x = 0; x < pages.size_in_texels; ++x)
      {
        const linalg::vec3 texel = pages.load(page, x, y);
        const size_t offset = ((size_t)y * (size_t)pages.size_in_texels + (size_t)x) * 3;
        image[offset + 0] = to_display_byte(texel.x);
        image[offset + 1] = to_display_byte(texel.y);
        image[offset + 2] = to_display_byte(texel.z);
      }
    }

    const std::string path = path_prefix + "_page" + std::to_string(page) + ".png";
    if (!stbi_write_png(path.c_str(), pages.size_in_texels, pages.size_in_texels, 3,
                        image.data(), pages.size_in_texels * 3))
    {
      log_error("[lightmap] could not write '{}'.", path);
      wrote_every_page = false;
      continue;
    }
    log_terminal("[lightmap] wrote {} ({}x{})", path, pages.size_in_texels,
                 pages.size_in_texels);
  }

  return wrote_every_page;
}

} // namespace shared
