#pragma once

// How a ghost reaches a client (coop_ghost_plan.md §2E): the way the map does.
//
//   S2C_GhostAvailable  reliable stream   "the ghost for this party is <hash>"; hash 0 is "there is none"
//   C2S_RequestGhost    reliable stream   the client's cache did not hash to that
//   S2C_GhostData       paced transfer    the .ghost file's bytes
//
// Bitstream-native, decoded in the game layer, like map_transfer.hpp.

#include "../span.hpp"
#include "bitstream.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace shared
{

// FNV-1a over the .ghost file's bytes, never 0: 0 is "no ghost".
[[nodiscard]] uint32_t compute_ghost_hash(Span<const uint8_t> ghost_bytes);

// The ghost the server is announcing: the file of the category being run, as it is on disk.
struct ghost_announcement_t
{
  uint32_t             party_size = 0;
  uint32_t             hash       = 0;
  std::vector<uint8_t> bytes;
};

// Absent, unparseable or the wrong track count is an announcement with hash 0.
[[nodiscard]] ghost_announcement_t load_ghost_announcement(std::string_view map_path, uint32_t party_size);

struct ghost_available_message_t
{
  uint32_t party_size = 0;
  uint32_t ghost_hash = 0;
  uint32_t byte_count = 0;
};

void serialize_ghost_available(network::Bit_Writer& writer, const ghost_available_message_t& message);
[[nodiscard]] ghost_available_message_t deserialize_ghost_available(network::Bit_Reader& reader);

struct request_ghost_message_t
{
  uint32_t ghost_hash = 0;
};

void serialize_request_ghost(network::Bit_Writer& writer, const request_ghost_message_t& message);
[[nodiscard]] request_ghost_message_t deserialize_request_ghost(network::Bit_Reader& reader);

struct ghost_data_message_t
{
  uint32_t             party_size = 0;
  uint32_t             ghost_hash = 0;
  std::vector<uint8_t> bytes;
};

void serialize_ghost_data(network::Bit_Writer& writer, const ghost_data_message_t& message);

// Empty when the declared byte count is more than the payload holds.
[[nodiscard]] std::optional<ghost_data_message_t> try_deserialize_ghost_data(network::Bit_Reader& reader);

} // namespace shared
