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
//             + chart count
//             + light count (the resolve table) + one uid each
//   charts    object_uid + plane(point, normal) + origin
//             + tangent_u + tangent_v + world_units_per_texel
//             + page + atlas_rect(min_x, min_y, width, height)
//             + LIGHTMAP_LIGHTS_PER_CHART light slots
//             + the unwrap: vertex count + (xref, u, v) each,
//               triangle count + (source face, three indices) each --
//               both counts zero on a brush chart
//   pages     FOUR page sets in this order -- irradiance, visibility, indirect
//             L0, indirect L1 -- each written the same way: format + layer count
//             + byte count + that many bytes, page-major.
//   probes    grid (origin, spacing, count) + L0 bytes + L1 bytes
//             + visibility slots + visibility bytes, each byte array behind a count
//   captures  spacing + count, then per capture: position + box min + box max
//             + probe index + open faces + overridden + cube (size, mip count,
//             byte count, bytes: mip-major, face, rows)
//
// A page set carries its own format and its own LAYER count rather than
// deriving either from the atlas: L1 has SH_L1_LAYERS_PER_PAGE layers per atlas
// page, and any of the four can be absent (a bake that traced nothing, an atlas
// with no light), which a byte count of zero is how to say.
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
// 4: the irradiance pages became the RESIDUAL. Every light a chart keeps is
//    shaded analytically at runtime against its stored visibility, so the atlas
//    holds only the lights ranked below the four (lighting_def.md ss14 step 6).
//    NOTHING about the layout moved, which is exactly why the bump is needed: a
//    version-3 file reads perfectly and renders every baked light TWICE, once
//    analytically and once out of an irradiance that is no longer residual.
//    Nobody can see that and conclude "stale sidecar" -- it looks like a bake
//    that is simply too bright.
// 5: the indirect bounce -- two more page sets (SH L1's L0 and its three
//    bias-encoded direction layers), and the four are now written uniformly with
//    a format and a layer count each. A version-4 file has no room for them and
//    is REFUSED rather than migrated: there is nothing to migrate to, since the
//    bounce it lacks can only come from a bake (lighting_def.md gate 2 step 2).
// 6: the irradiance probe volume (lighting_def.md gate 5) -- the probe spacing
//    joins the settings, and the grid and its two byte arrays follow the four
//    page sets. A version-5 file is REFUSED for gate 2's reason: the probes it
//    lacks can only come from a bake.
// 7: a static mesh chart carries its UNWRAP (lightmap_unwrap_plan.md step 3).
//    A brush's uvs are re-derived by projecting through the stored plane; an
//    xatlas chart has no plane, so its uvs are written down. A version-6 file
//    has charts keyed by plane that no mesh face can find any more, and is
//    REFUSED rather than read as a map whose props all draw unlit.
// 8: the probe volume carries a per-Mixed-light VISIBILITY (lighting_def.md
//    gate 9 step 4) -- four channel slots and one Unorm8x4 word per probe after
//    the two SH arrays. A version-7 file has no room for them and is REFUSED
//    for gate 5's reason: a visibility can only come from a bake.
// 9: the reflection captures (lighting_def.md gate 6 step 4) -- two more
//    settings (the capture spacing and the cube face size), then after the
//    probe volume the capture set: its snapped spacing and a count, and per
//    capture the position, the parallax box, the probe it sits on, the open
//    face bits, the override flag, and the cube's whole RGB9E5 mip chain behind
//    a size, a mip count and a byte count. A version-8 file is REFUSED: a
//    capture can only come from a bake.
constexpr uint32_t LIGHTMAP_VERSION = 9;

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
  write(lightmap.settings.probe_spacing_in_world_units);
  write(lightmap.settings.reflection_spacing_in_world_units);
  write((int32_t)lightmap.settings.reflection_size_in_texels);

  write((int32_t)lightmap.atlas.size_in_texels);
  write((int32_t)lightmap.atlas.page_count);
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

    write((uint32_t)chart.unwrap.vertices.size());
    for (const unwrapped_vertex_t &vertex : chart.unwrap.vertices)
    {
      write(vertex.xref);
      write(vertex.uv.x);
      write(vertex.uv.y);
    }
    write((uint32_t)chart.unwrap.faces.size());
    for (size_t t = 0; t < chart.unwrap.faces.size(); ++t)
    {
      write(chart.unwrap.faces[t]);
      for (size_t corner = 0; corner < 3; ++corner)
        write(chart.unwrap.indices[t * 3 + corner]);
    }
  }

  // One shape for all four, so a fifth page set is a call rather than a layout
  // decision -- and so the reader cannot get one of them subtly different from
  // the others.
  const auto write_pages = [&](const lightmap_pages_t &pages) {
    write((uint32_t)pages.format);
    write((int32_t)pages.page_count);
    write((uint32_t)pages.bytes.size());
    if (!pages.bytes.empty())
      out.write(reinterpret_cast<const char *>(pages.bytes.data()),
                (std::streamsize)pages.bytes.size());
  };

  write_pages(lightmap.irradiance_pages);
  write_pages(lightmap.visibility_pages);
  write_pages(lightmap.indirect_l0_pages);
  write_pages(lightmap.indirect_l1_pages);

  // The probe volume: its grid, then the two byte arrays each behind a count.
  // An empty volume writes a zero count for both and a zero grid.
  write_vec3(lightmap.probes.grid.origin);
  write(lightmap.probes.grid.spacing);
  write((int32_t)lightmap.probes.grid.count.x);
  write((int32_t)lightmap.probes.grid.count.y);
  write((int32_t)lightmap.probes.grid.count.z);
  const auto write_bytes = [&](const std::vector<uint8_t> &bytes) {
    write((uint32_t)bytes.size());
    if (!bytes.empty())
      out.write(reinterpret_cast<const char *>(bytes.data()), (std::streamsize)bytes.size());
  };
  write_bytes(lightmap.probes.l0_bytes);
  write_bytes(lightmap.probes.l1_bytes);
  for (const int16_t slot : lightmap.probes.visibility_slots) write((int32_t)slot);
  write_bytes(lightmap.probes.visibility_bytes);

  // The reflection captures: one shape per capture, the cube's whole mip chain
  // behind its own byte count so an unbaked capture is a count of zero.
  write(lightmap.reflections.spacing);
  write((uint32_t)lightmap.reflections.captures.size());
  for (const reflection_capture_t &capture : lightmap.reflections.captures)
  {
    write_vec3(capture.position);
    write_vec3(capture.box.min);
    write_vec3(capture.box.max);
    write(capture.probe_index);
    write((uint32_t)capture.open_faces);
    write((uint32_t)(capture.box_overridden ? 1 : 0));
    write((int32_t)capture.cube.size_in_texels);
    write((int32_t)capture.cube.mip_count);
    write_bytes(capture.cube.bytes);
  }

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
  read(lightmap.settings.probe_spacing_in_world_units);
  read(lightmap.settings.reflection_spacing_in_world_units);
  int32_t reflection_size = 0;
  read(reflection_size);
  lightmap.settings.reflection_size_in_texels = reflection_size;

  int32_t atlas_size = 0, page_count = 0;
  uint32_t chart_count = 0, light_count = 0;
  read(atlas_size);
  read(page_count);
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

    uint32_t vertex_count = 0;
    read(vertex_count);
    chart.unwrap.vertices.resize(vertex_count);
    for (unwrapped_vertex_t &vertex : chart.unwrap.vertices)
    {
      read(vertex.xref);
      read(vertex.uv.x);
      read(vertex.uv.y);
    }
    uint32_t triangle_count = 0;
    read(triangle_count);
    chart.unwrap.faces.resize(triangle_count);
    chart.unwrap.indices.resize((size_t)triangle_count * 3);
    for (size_t t = 0; t < triangle_count; ++t)
    {
      read(chart.unwrap.faces[t]);
      for (size_t corner = 0; corner < 3; ++corner)
        read(chart.unwrap.indices[t * 3 + corner]);
    }
  }

  // The mirror of the writer's, and a format this build cannot size is fatal to
  // the whole file rather than to one set: every page set after it is behind a
  // byte count read from a stream that is now at the wrong offset.
  bool every_page_set_is_readable = true;
  const auto read_pages = [&](lightmap_pages_t &pages) {
    uint32_t pages_format = 0, byte_count = 0;
    int32_t layer_count = 0;
    read(pages_format);
    read(layer_count);
    read(byte_count);

    pages.format = (lightmap_pixel_format_t)pages_format;
    pages.size_in_texels = atlas_size;
    pages.page_count = layer_count;

    if (byte_count > 0 && bytes_per_texel(pages.format) <= 0)
    {
      log_error("[lightmap] {} declares pixel format {}, which this build cannot "
                "read; rebake.", path, pages_format);
      every_page_set_is_readable = false;
      return;
    }

    pages.bytes.resize(byte_count);
    if (byte_count > 0)
      in.read(reinterpret_cast<char *>(pages.bytes.data()), (std::streamsize)byte_count);
  };

  read_pages(lightmap.irradiance_pages);
  read_pages(lightmap.visibility_pages);
  read_pages(lightmap.indirect_l0_pages);
  read_pages(lightmap.indirect_l1_pages);

  if (!every_page_set_is_readable) return {};

  read_vec3(lightmap.probes.grid.origin);
  read(lightmap.probes.grid.spacing);
  int32_t probe_count_x = 0, probe_count_y = 0, probe_count_z = 0;
  read(probe_count_x);
  read(probe_count_y);
  read(probe_count_z);
  lightmap.probes.grid.count = {probe_count_x, probe_count_y, probe_count_z};
  const auto read_bytes = [&](std::vector<uint8_t> &bytes) {
    uint32_t byte_count = 0;
    read(byte_count);
    bytes.resize(byte_count);
    if (byte_count > 0)
      in.read(reinterpret_cast<char *>(bytes.data()), (std::streamsize)byte_count);
  };
  read_bytes(lightmap.probes.l0_bytes);
  read_bytes(lightmap.probes.l1_bytes);
  for (int16_t &slot : lightmap.probes.visibility_slots)
  {
    int32_t stored = 0;
    read(stored);
    slot = (int16_t)stored;
    // A slot past the table resolves to nothing rather than indexing it.
    if (slot >= (int16_t)lightmap.light_uids.size()) slot = LIGHTMAP_NO_LIGHT_SLOT;
  }
  read_bytes(lightmap.probes.visibility_bytes);

  // A volume whose bytes do not fit its grid is a file that disagrees with
  // itself, and indexing it anyway reads past the end.
  const size_t probe_count = lightmap.probes.grid.probe_count();
  if (lightmap.probes.l0_bytes.size() != probe_count * 4 ||
      lightmap.probes.l1_bytes.size() != probe_count * 4 * (size_t)SH_L1_LAYERS_PER_PAGE ||
      lightmap.probes.visibility_bytes.size() != probe_count * 4)
  {
    log_error("[lightmap] {} holds a probe volume of {} probe(s) with {} + {} + {} byte(s); "
              "ignoring it. Rebake.",
              path, probe_count, lightmap.probes.l0_bytes.size(),
              lightmap.probes.l1_bytes.size(), lightmap.probes.visibility_bytes.size());
    return {};
  }

  read(lightmap.reflections.spacing);
  uint32_t capture_count = 0;
  read(capture_count);
  lightmap.reflections.captures.resize(capture_count);
  for (reflection_capture_t &capture : lightmap.reflections.captures)
  {
    read_vec3(capture.position);
    read_vec3(capture.box.min);
    read_vec3(capture.box.max);
    read(capture.probe_index);
    uint32_t open_faces = 0, overridden = 0;
    read(open_faces);
    read(overridden);
    capture.open_faces = (uint8_t)open_faces;
    capture.box_overridden = overridden != 0;
    int32_t cube_size = 0, mip_count = 0;
    read(cube_size);
    read(mip_count);
    capture.cube.size_in_texels = cube_size;
    capture.cube.mip_count = mip_count;
    read_bytes(capture.cube.bytes);
  }

  // A cube whose bytes do not fit its declared chain is the probe volume's
  // failure again, and it drops the whole SET: a capture table with one hole is
  // a lattice whose neighbours disagree about what is there.
  for (const reflection_capture_t &capture : lightmap.reflections.captures)
  {
    if (capture.cube.bytes_fit_declared_chain()) continue;
    log_error("[lightmap] {} holds a reflection capture of {} texel(s) a face, {} mip(s) and "
              "{} byte(s), which do not fit; ignoring every capture. Rebake.",
              path, capture.cube.size_in_texels, capture.cube.mip_count,
              capture.cube.bytes.size());
    lightmap.reflections = {};
    break;
  }

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
