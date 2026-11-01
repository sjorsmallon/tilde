#include "../message_handlers.hpp"

#include "../../shared/log.hpp"
#include "../../shared/network/map_transfer.hpp"
#include "../../shared/network/server_transport_layer.hpp"
#include "../server_context.hpp"

#include <chrono>

namespace server::messages
{

// The client lacks the compiled package of the map at this generation: build
// it once per load, then stream it as a paced transfer. A request naming an
// older generation is about a map no longer running; the client re-requests
// once it follows the Change_Map that bumped it.
void on_map_data_request(server_context_t &context, int32_t slot, const shared::Map_Data_Request &value)
{
  if (value.map_generation != context.map_generation)
  {
    log_terminal("slot {} asked for the package of '{}' at generation {}, now {}; ignored", slot,
                 value.map_name.c_str(), value.map_generation, context.map_generation);
    return;
  }

  world_t &world = context.world;
  if (world.map_data_message_bytes.empty())
  {
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    const shared::map_data_message_t message =
        shared::make_map_data_message(world.current_map, world.session.map_name);

    network::Bit_Writer writer;
    shared::serialize_map_data(writer, message);
    world.map_data_message_bytes = std::move(writer.buffer);

    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;
    log_terminal("Built map package '{}': {} bytes, {} compressed, hash {:#x}, in {:.2f}s.",
                 message.map_name, message.package_size_in_bytes, message.compressed_package.size(),
                 message.package_hash, elapsed.count());
  }

  network::begin_paced_transfer(context.transport_layer, slot, world.map_data_message_bytes,
                                network::Payload_Kind::Map_Transfer);

  log_terminal("Queued map package '{}' ({} bytes, {} fragments) for slot {} (requested '{}').",
               world.session.map_name, world.map_data_message_bytes.size(),
               context.transport_layer.clients[slot].outbound_transfer.fragments.size(), slot,
               value.map_name.c_str());
}

} // namespace server::messages
