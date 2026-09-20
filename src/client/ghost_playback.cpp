#include "ghost_playback.hpp"

#include "client_context.hpp"

#include "../shared/log.hpp"
#include "../shared/map.hpp"
#include "../shared/network/ghost_transfer.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace client
{

namespace
{

std::string cached_ghost_path(const client_context_t& context, uint32_t party_size)
{
  return shared::ghost_path_for(
      shared::resolve_map_path(client_maps_directory(), context.world.session.map_name), party_size);
}

std::vector<uint8_t> read_file_bytes(const std::string& path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
    return {};
  return std::vector<uint8_t>{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// The bytes already hash to what the server announced.
void adopt_ghost(client_context_t& context, Span<const uint8_t> bytes, uint32_t party_size,
                 std::string_view source)
{
  context.world.ghost = shared::try_parse_ghost(bytes, source);
  if (!context.world.ghost)
    return;

  if (context.world.ghost->tracks.size() != party_size)
  {
    log_error("ghost {}: holds {} tracks, the server announced a {}-player ghost", source,
              context.world.ghost->tracks.size(), party_size);
    context.world.ghost.reset();
    return;
  }

  if (context.world.ghost->map_content_hash != context.world.map_content_hash)
    log_warning("ghost {}: recorded on another version of this map, it may run through walls", source);
  log_terminal("ghost: loaded {} ({}, {} ticks at {} Hz)", source, shared::ghost_party_name(*context.world.ghost),
               context.world.ghost->run_ticks, context.world.ghost->tickrate_hz);
}

void apply_ghost_announcement(client_context_t& context, const shared::ghost_available_message_t& announced)
{
  context.world.ghost.reset();
  context.world.announced_ghost_hash = announced.ghost_hash;
  if (announced.ghost_hash == 0)
    return;

  const std::string          path   = cached_ghost_path(context, announced.party_size);
  const std::vector<uint8_t> cached = read_file_bytes(path);
  const Span<const uint8_t>  span(cached.data(), static_cast<uint32_t>(cached.size()));
  if (!cached.empty() && shared::compute_ghost_hash(span) == announced.ghost_hash)
  {
    adopt_ghost(context, span, announced.party_size, path);
    return;
  }

  log_terminal("ghost: no cached copy of the {}-player ghost {:#x} ({} bytes); requesting it",
               announced.party_size, announced.ghost_hash, announced.byte_count);
  network::Bit_Writer writer;
  shared::serialize_request_ghost(writer, {.ghost_hash = announced.ghost_hash});
  network::queue_reliable_client_message(context.transport_layer,
                                         static_cast<network::uint8>(network::Message_Type::C2S_RequestGhost),
                                         writer.buffer);
}

void apply_ghost_data(client_context_t& context, const shared::ghost_data_message_t& data)
{
  if (data.ghost_hash != context.world.announced_ghost_hash)
  {
    log_terminal("ghost: received {:#x}, but the announced ghost is {:#x} by now; dropped", data.ghost_hash,
                 context.world.announced_ghost_hash);
    return;
  }

  const Span<const uint8_t> span(data.bytes.data(), static_cast<uint32_t>(data.bytes.size()));
  if (shared::compute_ghost_hash(span) != data.ghost_hash)
  {
    log_error("ghost: the {} bytes received do not hash to {:#x}; dropped", data.bytes.size(), data.ghost_hash);
    return;
  }

  const std::string path = cached_ghost_path(context, data.party_size);
  std::error_code   directory_error;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), directory_error);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(reinterpret_cast<const char*>(data.bytes.data()), static_cast<std::streamsize>(data.bytes.size()));
  if (!file)
    log_error("ghost: could not cache {}; it is raced from memory and downloaded again next time", path);

  adopt_ghost(context, span, data.party_size, path);
}

} // namespace

void consume_ghost_messages(client_context_t& context, const network::Client_Inbox& inbox)
{
  for (const std::vector<network::uint8>& payload : inbox.ghost_available_messages)
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    apply_ghost_announcement(context, shared::deserialize_ghost_available(reader));
  }

  for (const std::vector<network::uint8>& payload : inbox.ghost_data_messages)
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    if (const std::optional<shared::ghost_data_message_t> data = shared::try_deserialize_ghost_data(reader))
      apply_ghost_data(context, *data);
  }
}

std::optional<double> try_ghost_run_tick(client_context_t& context)
{
  if (!context.world.ghost || !context.cvars->cl_ghost_show ||
      context.connection.phase != Connection_Phase::Connected)
    return std::nullopt;

  const entities::Match* match = try_find_match(context);
  if (match == nullptr || match->phase != entities::Round_Phase::Live)
    return std::nullopt;

  const shared::ghost_t& ghost    = *context.world.ghost;
  const double           tickrate = static_cast<double>(context.connection.server_tickrate);

  double run_ticks = context.replication.interpolation_cursor.tick - static_cast<double>(match->phase_start_tick);
  if (try_find_my_player(context) != nullptr && context.prediction.latest_input_number_processed_by_server >= 0)
  {
    visual_effects_t& visuals = context.visuals;
    if (!visuals.ghost_clock_latched || visuals.ghost_clock_phase_start_tick != match->phase_start_tick)
    {
      const int ticks_into_run = static_cast<int>(context.replication.latest_processed_tick) -
                                 static_cast<int>(match->phase_start_tick);
      visuals.ghost_clock_latched          = true;
      visuals.ghost_clock_phase_start_tick = match->phase_start_tick;
      visuals.ghost_clock_first_input =
          context.prediction.latest_input_number_processed_by_server - ticks_into_run;
    }
    run_ticks = static_cast<double>(context.prediction.input_number - 1 - visuals.ghost_clock_first_input) +
                static_cast<double>(context.prediction.physics_accumulator) * tickrate;
  }

  return run_ticks / tickrate * static_cast<double>(ghost.tickrate_hz);
}

} // namespace client
