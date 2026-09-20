#include "ghost_transfer.hpp"

#include "../ghost.hpp"
#include "../log.hpp"
#include "quantization.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace shared
{

uint32_t compute_ghost_hash(Span<const uint8_t> ghost_bytes)
{
  uint32_t hash = 2166136261u;
  for (const uint8_t byte : ghost_bytes)
  {
    hash ^= byte;
    hash *= 16777619u;
  }
  return hash == 0 ? 1 : hash;
}

ghost_announcement_t load_ghost_announcement(std::string_view map_path, uint32_t party_size)
{
  ghost_announcement_t announcement{.party_size = party_size};
  if (party_size == 0 || map_path.empty())
    return announcement;

  const std::string path = ghost_path_for(map_path, party_size);
  if (!std::filesystem::exists(path))
    return announcement;

  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    log_error("ghost: could not open {}, so no ghost is announced", path);
    return announcement;
  }
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

  const Span<const uint8_t>    span(bytes.data(), static_cast<uint32_t>(bytes.size()));
  const std::optional<ghost_t> ghost = try_parse_ghost(span, path);
  if (!ghost)
    return announcement;
  if (ghost->tracks.size() != party_size)
  {
    log_error("ghost {}: holds {} tracks, the category is {} players, so it is not announced", path,
              ghost->tracks.size(), party_size);
    return announcement;
  }

  announcement.hash  = compute_ghost_hash(span);
  announcement.bytes = std::move(bytes);
  return announcement;
}

void serialize_ghost_available(network::Bit_Writer& writer, const ghost_available_message_t& message)
{
  network::write_var_uint(writer, message.party_size);
  network::write_var_uint(writer, message.ghost_hash);
  network::write_var_uint(writer, message.byte_count);
}

ghost_available_message_t deserialize_ghost_available(network::Bit_Reader& reader)
{
  ghost_available_message_t message;
  message.party_size = network::read_var_uint(reader);
  message.ghost_hash = network::read_var_uint(reader);
  message.byte_count = network::read_var_uint(reader);
  return message;
}

void serialize_request_ghost(network::Bit_Writer& writer, const request_ghost_message_t& message)
{
  network::write_var_uint(writer, message.ghost_hash);
}

request_ghost_message_t deserialize_request_ghost(network::Bit_Reader& reader)
{
  return request_ghost_message_t{.ghost_hash = network::read_var_uint(reader)};
}

void serialize_ghost_data(network::Bit_Writer& writer, const ghost_data_message_t& message)
{
  network::write_var_uint(writer, message.party_size);
  network::write_var_uint(writer, message.ghost_hash);
  network::write_var_uint(writer, static_cast<uint32_t>(message.bytes.size()));
  writer.write_bytes(message.bytes.data(), message.bytes.size());
}

std::optional<ghost_data_message_t> try_deserialize_ghost_data(network::Bit_Reader& reader)
{
  ghost_data_message_t message;
  message.party_size        = network::read_var_uint(reader);
  message.ghost_hash        = network::read_var_uint(reader);
  const uint32_t byte_count = network::read_var_uint(reader);
  if (byte_count > reader.size)
  {
    log_error("ghost data declares {} bytes in a payload of {}", byte_count, reader.size);
    return std::nullopt;
  }
  message.bytes.resize(byte_count);
  reader.read_bytes(message.bytes.data(), byte_count);
  return message;
}

} // namespace shared
