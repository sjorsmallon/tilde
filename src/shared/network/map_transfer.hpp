#pragma once

#include "../navmesh.hpp"
#include "bitstream.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace shared
{

struct map_t; // fwd; build_map_package() lives in the .cpp (includes map.hpp)

// --- Map switching control messages ---
//
// These are bitstream-native (Bit_Writer / Bit_Reader), NOT protobuf. New
// networking code is written directly over the bitstream as we migrate off
// protobuf (see todo.md "Remove protobuf"). They ride the existing packet
// fragmenter via convert_to_packets() keyed on a dedicated Message_Type, and
// are decoded in the game layer (play_state / server_impl) rather than in the
// network reassembly, which stays ignorant of message semantics.

// Server -> all clients: switch to this map now. Carries the same (path, name,
// hash) triple as CmdAccept so the client can reference-load its own copy and
// verify it matches the server's. Byte streaming (a future S2C_MapData) is the
// fallback for when the client lacks the file; it lands with the packet
// reassembly reliability work (see todo.md).
struct change_map_message_t
{
  std::string map_path;     // loadable path/identifier (see CmdAccept TODO)
  std::string map_name;     // logical worldspawn name, for display
  uint32_t    content_hash; // FNV-1a of the map file bytes
};

void serialize_change_map(network::Bit_Writer &writer,
                          const change_map_message_t &msg);
change_map_message_t deserialize_change_map(network::Bit_Reader &reader);

// Client -> server: finished (re)loading the map. content_hash echoes the map
// the client actually has, so the server can confirm a match before it resumes
// streaming snapshots to this client.
struct map_loaded_message_t
{
  uint32_t content_hash;
};

void serialize_map_loaded(network::Bit_Writer &writer,
                          const map_loaded_message_t &msg);
map_loaded_message_t deserialize_map_loaded(network::Bit_Reader &reader);

// --- Compiled map package (the wire artifact / .bsp analogue) ---
//
// This is the COMPILED PACKAGE the server hosts and streams to clients, NOT the
// mapper-only .source (see todo.md "ARTIFACT MODEL"). It bundles the runtime
// entity data (serialize_map_to_string() text) with the baked sidecars a client
// needs to run the map but cannot cheaply recompute on load — the navmesh today,
// lightmaps/PVS later. Source never goes over the wire; this does.
//
// The container is a single self-describing byte blob (magic + version so a
// future format change is detectable) that the fragmenter ships as S2C_MapData.
struct map_package_t
{
  std::string map_name;    // logical worldspawn name, for display / cache key
  std::string entity_text; // serialize_map_to_string() output (runtime entities)
  navmesh_t   navmesh;     // baked sidecar; may be empty (navmesh.valid()==false)
};

// Pack the map's runtime entities + baked navmesh into the wire package blob.
// entity_text is taken from the canonical serialize_map_to_string(map).
map_package_t build_map_package(const map_t &map);

// Container (de)serialization. deserialize returns false on a bad magic/version
// or a truncated blob rather than silently yielding a half-built package.
std::vector<uint8_t> serialize_map_package(const map_package_t &package);
bool deserialize_map_package(const std::vector<uint8_t> &bytes,
                             map_package_t &out_package);

// FNV-1a over the package blob. This is the eventual WIRE identity hash
// (map_name, package_hash) — it covers entities AND baked data, unlike
// compute_map_content_hash() which hashes only the entity text.
uint32_t compute_map_package_hash(const std::vector<uint8_t> &package_bytes);

// Client -> server: I lack the compiled package for this map (cache miss or
// package_hash mismatch); please stream it. Sent instead of hard-erroring.
struct request_map_data_message_t
{
  std::string map_name;
};

void serialize_request_map_data(network::Bit_Writer &writer,
                                const request_map_data_message_t &msg);
request_map_data_message_t
deserialize_request_map_data(network::Bit_Reader &reader);

// Server -> client: here is the compiled package. `bytes` is the (optionally
// compressed) serialize_map_package() blob; `package_hash` is over the
// UNCOMPRESSED blob so the client can verify after decompressing. We ship
// compressed=false first (step 6 adds gzip).
struct map_data_message_t
{
  std::string          map_name;
  uint32_t             package_hash;
  bool                 compressed;
  std::vector<uint8_t> bytes;
};

void serialize_map_data(network::Bit_Writer &writer,
                        const map_data_message_t &msg);
map_data_message_t deserialize_map_data(network::Bit_Reader &reader);

} // namespace shared
