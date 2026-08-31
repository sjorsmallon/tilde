#include "lightmap_sidecar.hpp"
#include "log.hpp"

#include <fstream>

namespace shared
{

namespace
{

// Layout, all little-endian, everything fixed-width:
//
//   header    magic 'TLMP' + version + map_content_hash
//             + settings (texels_per_world_unit, gutter, max_chart_extent,
//                         atlas_size)
//             + atlas size_in_texels + page_count
//             + pixel format
//             + chart count
//   charts    object_uid + plane(point, normal) + origin
//             + tangent_u + tangent_v + world_units_per_texel
//             + page + atlas_rect(min_x, min_y, width, height)
//   pages     texels, page-major, bytes_per_texel(format) each
//
// The chart POLYGON is deliberately absent: it is the bake's own coverage test
// and nothing downstream reads it. Mesh generation projects through the stored
// basis instead, which is what makes it immune to a face being re-wound by a
// hull rebuild.
constexpr uint32_t LIGHTMAP_MAGIC = 0x504D4C54; // "TLMP"
// 2: the pages became RGB9E5 -- four bytes a texel where version 1 had one, and
// the binary visibility format that made that one byte no longer exists.
constexpr uint32_t LIGHTMAP_VERSION = 2;

std::string lightmap_path_for(const std::string &map_path)
{
  const size_t dot = map_path.rfind('.');
  if (dot != std::string::npos)
    return map_path.substr(0, dot) + ".lightmap";
  return map_path + ".lightmap";
}

} // namespace

void save_lightmap_sidecar(const std::string &map_path, const lightmap_t &lightmap,
                           uint32_t map_content_hash)
{
  const std::string path = lightmap_path_for(map_path);

  std::ofstream out(path, std::ios::binary);
  if (!out.is_open())
  {
    log_error("[lightmap] could not open {} for writing; the bake is not saved.", path);
    return;
  }

  const auto write = [&](const auto &value) {
    out.write(reinterpret_cast<const char *>(&value), sizeof(value));
  };
  const auto write_vec3 = [&](const linalg::vec3 &value) {
    write(value.x);
    write(value.y);
    write(value.z);
  };

  write(LIGHTMAP_MAGIC);
  write(LIGHTMAP_VERSION);
  write(map_content_hash);

  write(lightmap.settings.texels_per_world_unit);
  write((int32_t)lightmap.settings.gutter_in_texels);
  write((int32_t)lightmap.settings.max_chart_extent_in_texels);
  write((int32_t)lightmap.settings.atlas_size_in_texels);

  write((int32_t)lightmap.atlas.size_in_texels);
  write((int32_t)lightmap.atlas.page_count);
  write((uint32_t)lightmap.pages.format);
  write((uint32_t)lightmap.charts.size());

  for (const lightmap_chart_t &chart : lightmap.charts)
  {
    write((uint32_t)chart.object_uid);
    write_vec3(chart.plane.point);
    write_vec3(chart.plane.normal);
    write_vec3(chart.origin);
    write_vec3(chart.tangent_u);
    write_vec3(chart.tangent_v);
    write(chart.world_units_per_texel);
    write((int32_t)chart.page);
    write((int32_t)chart.atlas_rect.min_x);
    write((int32_t)chart.atlas_rect.min_y);
    write((int32_t)chart.atlas_rect.width);
    write((int32_t)chart.atlas_rect.height);
  }

  if (!lightmap.pages.bytes.empty())
    out.write(reinterpret_cast<const char *>(lightmap.pages.bytes.data()),
              (std::streamsize)lightmap.pages.bytes.size());

  if (!out.good())
  {
    log_error("[lightmap] writing {} failed part way through; the sidecar is "
              "incomplete and will not load.", path);
    return;
  }

  log_terminal("[lightmap] wrote {} -- {} charts across {} page(s) of {} texels.", path,
           lightmap.charts.size(), lightmap.atlas.page_count,
           lightmap.atlas.size_in_texels);
}

lightmap_t load_lightmap_sidecar(const std::string &map_path, uint32_t map_content_hash)
{
  const std::string path = lightmap_path_for(map_path);

  std::ifstream in(path, std::ios::binary);
  if (!in.is_open())
    return {};

  const auto read = [&](auto &value) {
    in.read(reinterpret_cast<char *>(&value), sizeof(value));
  };
  const auto read_vec3 = [&](linalg::vec3 &value) {
    read(value.x);
    read(value.y);
    read(value.z);
  };

  uint32_t magic = 0;
  uint32_t version = 0;
  uint32_t baked_from_hash = 0;
  read(magic);
  read(version);
  read(baked_from_hash);

  if (magic != LIGHTMAP_MAGIC)
  {
    log_error("[lightmap] {} does not start with 'TLMP'; ignoring it.", path);
    return {};
  }
  if (version != LIGHTMAP_VERSION)
  {
    log_error("[lightmap] {} is version {}, and this build reads version {}; rebake.",
              path, version, LIGHTMAP_VERSION);
    return {};
  }

  lightmap_t lightmap;

  int32_t gutter = 0, max_extent = 0, settings_atlas_size = 0;
  read(lightmap.settings.texels_per_world_unit);
  read(gutter);
  read(max_extent);
  read(settings_atlas_size);
  lightmap.settings.gutter_in_texels = gutter;
  lightmap.settings.max_chart_extent_in_texels = max_extent;
  lightmap.settings.atlas_size_in_texels = settings_atlas_size;

  int32_t atlas_size = 0, page_count = 0;
  uint32_t format = 0, chart_count = 0;
  read(atlas_size);
  read(page_count);
  read(format);
  read(chart_count);

  lightmap.atlas.size_in_texels = atlas_size;
  lightmap.atlas.page_count = page_count;
  lightmap.pages.format = (lightmap_pixel_format_t)format;

  const int texel_size = bytes_per_texel(lightmap.pages.format);
  if (texel_size <= 0)
  {
    log_error("[lightmap] {} declares pixel format {}, which this build cannot read; "
              "rebake.", path, format);
    return {};
  }

  lightmap.charts.resize(chart_count);
  for (lightmap_chart_t &chart : lightmap.charts)
  {
    uint32_t object_uid = 0;
    read(object_uid);
    chart.object_uid = object_uid;

    read_vec3(chart.plane.point);
    read_vec3(chart.plane.normal);
    read_vec3(chart.origin);
    read_vec3(chart.tangent_u);
    read_vec3(chart.tangent_v);
    read(chart.world_units_per_texel);

    int32_t page = 0, min_x = 0, min_y = 0, width = 0, height = 0;
    read(page);
    read(min_x);
    read(min_y);
    read(width);
    read(height);
    chart.page = page;
    chart.atlas_rect = {min_x, min_y, width, height};
  }

  lightmap.pages.size_in_texels = atlas_size;
  lightmap.pages.page_count = page_count;
  lightmap.pages.bytes.resize(lightmap.pages.texel_count() * (size_t)texel_size);
  if (!lightmap.pages.bytes.empty())
    in.read(reinterpret_cast<char *>(lightmap.pages.bytes.data()),
            (std::streamsize)lightmap.pages.bytes.size());

  if (!in.good())
  {
    log_error("[lightmap] {} ended early -- {} charts and {} page byte(s) expected. "
              "Ignoring it; rebake.", path, chart_count, lightmap.pages.bytes.size());
    return {};
  }

  set_lightmap_geometry_id(lightmap);

  // Loaded ANYWAY on a mismatch, and loudly. See the header for why refusing is
  // the worse failure.
  if (baked_from_hash != map_content_hash)
    log_warning("[lightmap] {} was baked from a different version of this map "
                "(0x{:08x} vs 0x{:08x}). Faces that moved will draw unlit; rebake.",
                path, baked_from_hash, map_content_hash);

  return lightmap;
}

} // namespace shared
