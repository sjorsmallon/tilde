#include "map_transfer.hpp"

#include "../map.hpp"
#include "quantization.hpp"

namespace shared
{

// content_hash is a full-entropy 32-bit value, so it's written with the
// variable-length uint helper (which handles the whole uint32 range safely)
// rather than write_bits(.., 32) — the latter's 1 << 31 shift is UB on the
// signed int accumulator inside Bit_Reader.

void serialize_change_map(network::Bit_Writer &writer,
                          const change_map_message_t &msg)
{
  network::write_string(writer, msg.map_path);
  network::write_string(writer, msg.map_name);
  network::write_var_uint(writer, msg.content_hash);
}

change_map_message_t deserialize_change_map(network::Bit_Reader &reader)
{
  change_map_message_t msg{};
  network::read_string(reader, msg.map_path);
  network::read_string(reader, msg.map_name);
  msg.content_hash = network::read_var_uint(reader);
  return msg;
}

// --- Compiled map package ---

// Package container tag. Bump PACKAGE_VERSION on any layout change (e.g. a new
// baked sidecar) so an old client rejects a newer blob instead of misreading it.
static constexpr uint32_t PACKAGE_MAGIC   = 0x504B4720; // "PKG "
// 2: the package carries the baked lightmap beside the navmesh.
// 3: the lightmap carries its per-light visibility -- a second page set, the
//    resolve table, and each chart's light slots.
// 4: the lightmap's irradiance pages became the RESIDUAL -- a MEANING change
//    with no layout change behind it, which is what makes the bump load-bearing
//    rather than bookkeeping. A version-3 package parses cleanly and renders
//    every baked light twice (lightmap_sidecar.cpp says why).
// 5: the lightmap carries the path-traced indirect bounce as SH L1 -- two more
//    page sets, and all four now written uniformly with a format and a layer
//    count each (lighting_def.md gate 2 step 2).
// 6: the lightmap carries the irradiance probe volume and its spacing
//    (lighting_def.md gate 5).
// 7: a static mesh chart carries its unwrap (lightmap_sidecar.cpp version 7).
static constexpr uint32_t PACKAGE_VERSION = 7;

// Navmesh floats/indices are written as raw bytes (exact), matching the on-disk
// .navmesh sidecar's exactness — write_coord's 5-bit fraction would corrupt
// vertex positions and A* would path through walls. Counts use write_var_uint.
static void write_u32(network::Bit_Writer &w, uint32_t v)
{
  w.write_bytes(&v, sizeof(v));
}
static uint32_t read_u32(network::Bit_Reader &r)
{
  uint32_t v = 0;
  r.read_bytes(&v, sizeof(v));
  return v;
}
static void write_i32(network::Bit_Writer &w, int32_t v)
{
  w.write_bytes(&v, sizeof(v));
}
static int32_t read_i32(network::Bit_Reader &r)
{
  int32_t v = 0;
  r.read_bytes(&v, sizeof(v));
  return v;
}
static void write_f32(network::Bit_Writer &w, float v)
{
  w.write_bytes(&v, sizeof(v));
}
static float read_f32(network::Bit_Reader &r)
{
  float v = 0.f;
  r.read_bytes(&v, sizeof(v));
  return v;
}

static void serialize_navmesh(network::Bit_Writer &w, const navmesh_t &nav)
{
  network::write_var_uint(w, static_cast<uint32_t>(nav.vertices.size()));
  network::write_var_uint(w, static_cast<uint32_t>(nav.polygons.size()));

  for (const auto &v : nav.vertices)
  {
    write_f32(w, v.position.x);
    write_f32(w, v.position.y);
    write_f32(w, v.position.z);
  }

  for (const auto &p : nav.polygons)
  {
    network::write_var_uint(w, static_cast<uint32_t>(p.vertices.size()));
    for (int32_t vert : p.vertices)
      write_i32(w, vert);
    for (int32_t neighbor : p.neighbors)
      write_i32(w, neighbor);
    write_i32(w, p.island);
  }
}

static void deserialize_navmesh(network::Bit_Reader &r, navmesh_t &nav)
{
  uint32_t vertex_count  = network::read_var_uint(r);
  uint32_t polygon_count = network::read_var_uint(r);

  nav.vertices.resize(vertex_count);
  for (auto &v : nav.vertices)
  {
    v.position.x = read_f32(r);
    v.position.y = read_f32(r);
    v.position.z = read_f32(r);
  }

  nav.polygons.resize(polygon_count);
  for (auto &p : nav.polygons)
  {
    uint32_t n = network::read_var_uint(r);
    p.vertices.resize(n);
    p.neighbors.resize(n);
    for (uint32_t k = 0; k < n; ++k)
      p.vertices[k] = read_i32(r);
    for (uint32_t k = 0; k < n; ++k)
      p.neighbors[k] = read_i32(r);
    p.island = read_i32(r);
  }
}

// The pages are written as RAW BYTES, exactly as the sidecar stores them and for
// the same reason the navmesh's floats are: they are already quantized (RGB9E5,
// lightmap.hpp), and write_coord over them would corrupt the shared exponent.
//
// A chart's `polygon` is deliberately absent, matching the sidecar: it is the
// bake's own coverage test and nothing downstream reads it.
static void serialize_lightmap(network::Bit_Writer &w, const lightmap_t &lightmap)
{
  const auto write_vec3 = [&](const linalg::vec3 &value) {
    write_f32(w, value.x);
    write_f32(w, value.y);
    write_f32(w, value.z);
  };

  network::write_var_uint(w, static_cast<uint32_t>(lightmap.charts.size()));
  if (lightmap.charts.empty())
    return;

  write_f32(w, lightmap.settings.texels_per_world_unit);
  write_i32(w, lightmap.settings.gutter_in_texels);
  write_i32(w, lightmap.settings.max_chart_extent_in_texels);
  write_i32(w, lightmap.settings.atlas_size_in_texels);
  write_f32(w, lightmap.settings.probe_spacing_in_world_units);

  write_i32(w, lightmap.atlas.size_in_texels);
  write_i32(w, lightmap.atlas.page_count);

  network::write_var_uint(w, static_cast<uint32_t>(lightmap.light_uids.size()));
  for (entity_uid_t light_uid : lightmap.light_uids)
    write_u32(w, static_cast<uint32_t>(light_uid));

  for (const lightmap_chart_t &chart : lightmap.charts)
  {
    write_u32(w, static_cast<uint32_t>(chart.object_uid));
    write_vec3(chart.plane.point);
    write_vec3(chart.plane.normal);
    write_vec3(chart.origin);
    write_vec3(chart.tangent_u);
    write_vec3(chart.tangent_v);
    write_f32(w, chart.world_units_per_texel);
    write_i32(w, chart.page);
    write_i32(w, chart.atlas_rect.min_x);
    write_i32(w, chart.atlas_rect.min_y);
    write_i32(w, chart.atlas_rect.width);
    write_i32(w, chart.atlas_rect.height);
    for (int16_t slot : chart.light_slots)
      write_i32(w, slot);

    network::write_var_uint(w, static_cast<uint32_t>(chart.unwrap.vertices.size()));
    for (const unwrapped_vertex_t &vertex : chart.unwrap.vertices)
    {
      write_u32(w, vertex.xref);
      write_f32(w, vertex.uv.x);
      write_f32(w, vertex.uv.y);
    }
    network::write_var_uint(w, static_cast<uint32_t>(chart.unwrap.faces.size()));
    for (size_t t = 0; t < chart.unwrap.faces.size(); ++t)
    {
      write_u32(w, chart.unwrap.faces[t]);
      for (size_t corner = 0; corner < 3; ++corner)
        write_u32(w, chart.unwrap.indices[t * 3 + corner]);
    }
  }

  // The same four sets in the same order the sidecar writes them, and for the
  // same reason each carries its own layer count: SH L1 has three layers per
  // atlas page, so the atlas alone no longer sizes every set.
  const auto write_pages = [&](const lightmap_pages_t &pages) {
    write_u32(w, static_cast<uint32_t>(pages.format));
    write_i32(w, pages.page_count);
    network::write_var_uint(w, static_cast<uint32_t>(pages.bytes.size()));
    if (!pages.bytes.empty())
      w.write_bytes(pages.bytes.data(), pages.bytes.size());
  };

  write_pages(lightmap.irradiance_pages);
  write_pages(lightmap.visibility_pages);
  write_pages(lightmap.indirect_l0_pages);
  write_pages(lightmap.indirect_l1_pages);

  write_vec3(lightmap.probes.grid.origin);
  write_f32(w, lightmap.probes.grid.spacing);
  write_i32(w, lightmap.probes.grid.count.x);
  write_i32(w, lightmap.probes.grid.count.y);
  write_i32(w, lightmap.probes.grid.count.z);
  const auto write_bytes = [&](const std::vector<uint8_t> &bytes) {
    network::write_var_uint(w, static_cast<uint32_t>(bytes.size()));
    if (!bytes.empty())
      w.write_bytes(bytes.data(), bytes.size());
  };
  write_bytes(lightmap.probes.l0_bytes);
  write_bytes(lightmap.probes.l1_bytes);
}

static void deserialize_lightmap(network::Bit_Reader &r, lightmap_t &lightmap)
{
  const auto read_vec3 = [&](linalg::vec3 &value) {
    value.x = read_f32(r);
    value.y = read_f32(r);
    value.z = read_f32(r);
  };

  const uint32_t chart_count = network::read_var_uint(r);
  if (chart_count == 0)
  {
    lightmap = {};
    return;
  }

  lightmap.settings.texels_per_world_unit      = read_f32(r);
  lightmap.settings.gutter_in_texels           = read_i32(r);
  lightmap.settings.max_chart_extent_in_texels = read_i32(r);
  lightmap.settings.atlas_size_in_texels       = read_i32(r);
  lightmap.settings.probe_spacing_in_world_units = read_f32(r);

  lightmap.atlas.size_in_texels = read_i32(r);
  lightmap.atlas.page_count     = read_i32(r);

  lightmap.light_uids.resize(network::read_var_uint(r));
  for (entity_uid_t &light_uid : lightmap.light_uids)
    light_uid = read_u32(r);

  lightmap.charts.resize(chart_count);
  for (lightmap_chart_t &chart : lightmap.charts)
  {
    chart.object_uid = read_u32(r);
    read_vec3(chart.plane.point);
    read_vec3(chart.plane.normal);
    read_vec3(chart.origin);
    read_vec3(chart.tangent_u);
    read_vec3(chart.tangent_v);
    chart.world_units_per_texel = read_f32(r);
    chart.page                  = read_i32(r);
    chart.atlas_rect.min_x      = read_i32(r);
    chart.atlas_rect.min_y      = read_i32(r);
    chart.atlas_rect.width      = read_i32(r);
    chart.atlas_rect.height     = read_i32(r);

    for (int16_t &slot : chart.light_slots)
    {
      slot = static_cast<int16_t>(read_i32(r));
      // Same guard the sidecar reader carries, and for the same reason: a slot
      // past the table must resolve to nothing rather than index it.
      if (slot >= static_cast<int16_t>(lightmap.light_uids.size()))
        slot = LIGHTMAP_NO_LIGHT_SLOT;
    }

    chart.unwrap.vertices.resize(network::read_var_uint(r));
    for (unwrapped_vertex_t &vertex : chart.unwrap.vertices)
    {
      vertex.xref = read_u32(r);
      vertex.uv.x = read_f32(r);
      vertex.uv.y = read_f32(r);
    }
    const uint32_t triangle_count = network::read_var_uint(r);
    chart.unwrap.faces.resize(triangle_count);
    chart.unwrap.indices.resize(static_cast<size_t>(triangle_count) * 3);
    for (size_t t = 0; t < triangle_count; ++t)
    {
      chart.unwrap.faces[t] = read_u32(r);
      for (size_t corner = 0; corner < 3; ++corner)
        chart.unwrap.indices[t * 3 + corner] = read_u32(r);
    }
  }

  const auto read_pages = [&](lightmap_pages_t &pages) {
    pages.format         = static_cast<lightmap_pixel_format_t>(read_u32(r));
    pages.page_count     = read_i32(r);
    pages.size_in_texels = lightmap.atlas.size_in_texels;
    pages.bytes.resize(network::read_var_uint(r));
    if (!pages.bytes.empty())
      r.read_bytes(pages.bytes.data(), pages.bytes.size());
  };

  read_pages(lightmap.irradiance_pages);
  read_pages(lightmap.visibility_pages);
  read_pages(lightmap.indirect_l0_pages);
  read_pages(lightmap.indirect_l1_pages);

  read_vec3(lightmap.probes.grid.origin);
  lightmap.probes.grid.spacing = read_f32(r);
  lightmap.probes.grid.count.x = read_i32(r);
  lightmap.probes.grid.count.y = read_i32(r);
  lightmap.probes.grid.count.z = read_i32(r);
  const auto read_bytes = [&](std::vector<uint8_t> &bytes) {
    bytes.resize(network::read_var_uint(r));
    if (!bytes.empty())
      r.read_bytes(bytes.data(), bytes.size());
  };
  read_bytes(lightmap.probes.l0_bytes);
  read_bytes(lightmap.probes.l1_bytes);

  // Same guard the sidecar reader carries: a volume whose bytes do not fit its
  // grid is dropped whole rather than indexed.
  const size_t probe_count = lightmap.probes.grid.probe_count();
  if (lightmap.probes.l0_bytes.size() != probe_count * 4 ||
      lightmap.probes.l1_bytes.size() != probe_count * 4 * (size_t)SH_L1_LAYERS_PER_PAGE)
  {
    log_error("[map_transfer] the package's probe volume of {} probe(s) carries {} + {} "
              "byte(s); dropping it.",
              probe_count, lightmap.probes.l0_bytes.size(),
              lightmap.probes.l1_bytes.size());
    lightmap.probes = {};
  }

  // The id the generated-mesh cache compares, recomputed rather than shipped:
  // it is a content hash of what was just read, so sending it would be a second
  // copy free to disagree with the charts it describes.
  set_lightmap_geometry_id(lightmap);
}

map_package_t build_map_package(const map_t &map)
{
  map_package_t package;
  package.map_name    = map.name;
  package.entity_text = serialize_map_to_string(map);
  package.navmesh     = map.navmesh;
  package.lightmap    = map.lightmap;
  return package;
}

std::vector<uint8_t> serialize_map_package(const map_package_t &package)
{
  network::Bit_Writer writer;
  write_u32(writer, PACKAGE_MAGIC);
  write_u32(writer, PACKAGE_VERSION);
  network::write_string(writer, package.map_name);
  network::write_string(writer, package.entity_text);
  serialize_navmesh(writer, package.navmesh);
  serialize_lightmap(writer, package.lightmap);
  return writer.buffer;
}

bool deserialize_map_package(const std::vector<uint8_t> &bytes,
                             map_package_t &out_package)
{
  network::Bit_Reader reader(bytes.data(), bytes.size());
  uint32_t magic   = read_u32(reader);
  uint32_t version = read_u32(reader);
  if (magic != PACKAGE_MAGIC || version != PACKAGE_VERSION)
    return false;

  network::read_string(reader, out_package.map_name);
  network::read_string(reader, out_package.entity_text);
  deserialize_navmesh(reader, out_package.navmesh);
  deserialize_lightmap(reader, out_package.lightmap);

  // read past the end (truncated blob) => reject rather than yield garbage.
  return reader.bit_index <= static_cast<int>(bytes.size() * 8);
}

uint32_t compute_map_package_hash(const std::vector<uint8_t> &package_bytes)
{
  uint32_t hash = 2166136261u; // FNV-1a offset basis
  for (uint8_t byte : package_bytes)
  {
    hash ^= byte;
    hash *= 16777619u; // FNV prime
  }
  return hash;
}

// --- Streaming control messages ---

void serialize_request_map_data(network::Bit_Writer &writer,
                                const request_map_data_message_t &msg)
{
  network::write_string(writer, msg.map_name);
}

request_map_data_message_t
deserialize_request_map_data(network::Bit_Reader &reader)
{
  request_map_data_message_t msg{};
  network::read_string(reader, msg.map_name);
  return msg;
}

void serialize_map_data(network::Bit_Writer &writer,
                        const map_data_message_t &msg)
{
  network::write_string(writer, msg.map_name);
  network::write_var_uint(writer, msg.package_hash);
  writer.write_bit(msg.compressed);
  network::write_var_uint(writer, static_cast<uint32_t>(msg.bytes.size()));
  writer.write_bytes(msg.bytes.data(), msg.bytes.size());
}

map_data_message_t deserialize_map_data(network::Bit_Reader &reader)
{
  map_data_message_t msg{};
  network::read_string(reader, msg.map_name);
  msg.package_hash = network::read_var_uint(reader);
  msg.compressed   = reader.read_bit();
  uint32_t size    = network::read_var_uint(reader);
  msg.bytes.resize(size);
  reader.read_bytes(msg.bytes.data(), size);
  return msg;
}

} // namespace shared
