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
//             + light count (the resolve table) + one uid each
//   charts    object_uid + plane(point, normal) + origin
//             + tangent_u + tangent_v + world_units_per_texel
//             + page + atlas_rect(min_x, min_y, width, height)
//             + LIGHTMAP_LIGHTS_PER_CHART light slots
//   pages     irradiance texels, page-major, bytes_per_texel(format) each
//   vis       visibility format + byte count + that many bytes
//
// The chart POLYGON is deliberately absent: it is the bake's own coverage test
// and nothing downstream reads it. Mesh generation projects through the stored
// basis instead, which is what makes it immune to a face being re-wound by a
// hull rebuild.
constexpr uint32_t LIGHTMAP_MAGIC = 0x504D4C54; // "TLMP"
// 2: the pages became RGB9E5 -- four bytes a texel where version 1 had one, and
//    the binary visibility format that made that one byte no longer exists.
// 3: the per-light visibility -- a second page set, the resolve table naming
//    what each baked slot is, and the four slots a chart kept.
constexpr uint32_t LIGHTMAP_VERSION = 3;

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
  write((uint32_t)lightmap.irradiance_pages.format);
  write((uint32_t)lightmap.charts.size());
  write((uint32_t)lightmap.light_uids.size());

  for (entity_uid_t light_uid : lightmap.light_uids) write((uint32_t)light_uid);

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
    for (int16_t slot : chart.light_slots) write(slot);
  }

  if (!lightmap.irradiance_pages.bytes.empty())
    out.write(reinterpret_cast<const char *>(lightmap.irradiance_pages.bytes.data()),
              (std::streamsize)lightmap.irradiance_pages.bytes.size());

  // The visibility set carries its own format and its own byte count rather than
  // deriving them from the atlas: it is the one part of a bake that can be
  // absent (an unbaked map's, a bake from before it existed), and a length of
  // zero is how that is said.
  write((uint32_t)lightmap.visibility_pages.format);
  write((uint32_t)lightmap.visibility_pages.bytes.size());
  if (!lightmap.visibility_pages.bytes.empty())
    out.write(reinterpret_cast<const char *>(lightmap.visibility_pages.bytes.data()),
              (std::streamsize)lightmap.visibility_pages.bytes.size());

  if (!out.good())
  {
    log_error("[lightmap] writing {} failed part way through; the sidecar is "
              "incomplete and will not load.", path);
    return;
  }

  log_terminal("[lightmap] wrote {} -- {} charts across {} page(s) of {} texels, {} "
               "baked light(s).", path, lightmap.charts.size(),
               lightmap.atlas.page_count, lightmap.atlas.size_in_texels,
               lightmap.light_uids.size());
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
  uint32_t format = 0, chart_count = 0, light_count = 0;
  read(atlas_size);
  read(page_count);
  read(format);
  read(chart_count);
  read(light_count);

  lightmap.light_uids.resize(light_count);
  for (entity_uid_t &light_uid : lightmap.light_uids)
  {
    uint32_t stored = 0;
    read(stored);
    light_uid = stored;
  }

  lightmap.atlas.size_in_texels = atlas_size;
  lightmap.atlas.page_count = page_count;
  lightmap.irradiance_pages.format = (lightmap_pixel_format_t)format;

  const int texel_size = bytes_per_texel(lightmap.irradiance_pages.format);
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

    for (int16_t &slot : chart.light_slots)
    {
      read(slot);
      // A slot past the table is a file that disagrees with itself, and the one
      // thing it must never do is index it anyway.
      if (slot >= (int16_t)light_count) slot = LIGHTMAP_NO_LIGHT_SLOT;
    }
  }

  lightmap.irradiance_pages.size_in_texels = atlas_size;
  lightmap.irradiance_pages.page_count = page_count;
  lightmap.irradiance_pages.bytes.resize(lightmap.irradiance_pages.texel_count() *
                                         (size_t)texel_size);
  if (!lightmap.irradiance_pages.bytes.empty())
    in.read(reinterpret_cast<char *>(lightmap.irradiance_pages.bytes.data()),
            (std::streamsize)lightmap.irradiance_pages.bytes.size());

  uint32_t visibility_format = 0, visibility_byte_count = 0;
  read(visibility_format);
  read(visibility_byte_count);

  lightmap.visibility_pages.format = (lightmap_pixel_format_t)visibility_format;
  lightmap.visibility_pages.size_in_texels = atlas_size;
  lightmap.visibility_pages.page_count = page_count;
  lightmap.visibility_pages.bytes.resize(visibility_byte_count);
  if (visibility_byte_count > 0)
    in.read(reinterpret_cast<char *>(lightmap.visibility_pages.bytes.data()),
            (std::streamsize)visibility_byte_count);

  if (!in.good())
  {
    log_error("[lightmap] {} ended early -- {} charts and {} page byte(s) expected. "
              "Ignoring it; rebake.", path, chart_count, lightmap.irradiance_pages.bytes.size());
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
