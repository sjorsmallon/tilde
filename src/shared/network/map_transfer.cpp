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

void serialize_map_loaded(network::Bit_Writer &writer,
                          const map_loaded_message_t &msg)
{
  network::write_var_uint(writer, msg.content_hash);
}

map_loaded_message_t deserialize_map_loaded(network::Bit_Reader &reader)
{
  map_loaded_message_t msg{};
  msg.content_hash = network::read_var_uint(reader);
  return msg;
}

// --- Compiled map package ---

// Package container tag. Bump PACKAGE_VERSION on any layout change (e.g. a new
// baked sidecar) so an old client rejects a newer blob instead of misreading it.
static constexpr uint32_t PACKAGE_MAGIC   = 0x504B4720; // "PKG "
static constexpr uint32_t PACKAGE_VERSION = 1;

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
    write_f32(w, v.pos.x);
    write_f32(w, v.pos.y);
    write_f32(w, v.pos.z);
  }

  for (const auto &p : nav.polygons)
  {
    network::write_var_uint(w, static_cast<uint32_t>(p.verts.size()));
    for (int32_t vert : p.verts)
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
    v.pos.x = read_f32(r);
    v.pos.y = read_f32(r);
    v.pos.z = read_f32(r);
  }

  nav.polygons.resize(polygon_count);
  for (auto &p : nav.polygons)
  {
    uint32_t n = network::read_var_uint(r);
    p.verts.resize(n);
    p.neighbors.resize(n);
    for (uint32_t k = 0; k < n; ++k)
      p.verts[k] = read_i32(r);
    for (uint32_t k = 0; k < n; ++k)
      p.neighbors[k] = read_i32(r);
    p.island = read_i32(r);
  }
}

map_package_t build_map_package(const map_t &map)
{
  map_package_t package;
  package.map_name    = map.name;
  package.entity_text = serialize_map_to_string(map);
  package.navmesh     = map.navmesh;
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
