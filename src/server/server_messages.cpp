#include "server_messages.hpp"

#include "log.hpp"
#include "message_stream.hpp"
#include "network/server_transport_layer.hpp"
#include "server_context.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace server
{

namespace
{

// A pascal_string_t refuses what does not fit; a map name or path that long is
// a broken install, said once rather than truncated into a different name.
template <network::uint8 N> void set_or_die(network::pascal_string_t<N>& field, const std::string& text,
                                            const char* what)
{
  if (!field.set(text.c_str()))
    fatal_error("{} '{}' is longer than the {} characters the wire carries", what, text, (int)N);
}

} // namespace

std::string current_map_wire_id(const server_context_t &context)
{
  return std::filesystem::path(context.world.current_map_path).filename().generic_string();
}

shared::Server_Map current_server_map(const server_context_t &context)
{
  shared::Server_Map map;
  map.generation   = context.map_generation;
  map.content_hash = context.world.map_content_hash;
  set_or_die(map.path, current_map_wire_id(context), "map path");
  set_or_die(map.name, context.world.session.map_name, "map name");
  return map;
}

void send_cvar_values(server_context_t &context, int32_t slot, Span<const shared::Cvar_Value> values)
{
  shared::write_cvar_value_messages(context.outgoing.reliable[slot], values);
}

void send_server_text(server_context_t &context, int32_t slot, std::string_view text)
{
  constexpr size_t bound = decltype(shared::Server_Text::text)::capacity;
  do
  {
    const std::string_view piece = text.substr(0, bound);
    text.remove_prefix(piece.size());

    shared::Server_Text message;
    (void)message.text.set(std::string(piece).c_str());
    shared::write_server_text(context.outgoing.reliable[slot], message);
  } while (!text.empty());
}

void broadcast_server_text_message(server_context_t &context,
                                   std::string_view text)
{
  int recipient_count = 0;
  for (connected_client_t row : connected_clients(context))
  {
    ++recipient_count;
    send_server_text(context, row.slot, text);
  }

  log_terminal("[BROADCAST -> {} client(s)] {}", recipient_count, text);
}

void send_reject_to_unslotted_peer(server_context_t &context, const network::Address &address,
                                   std::string_view reason, uint32_t server_schema_hash,
                                   uint32_t server_asset_table_hash)
{
  shared::Reject reject;
  (void)reject.reason.set(std::string(reason.substr(0, decltype(reject.reason)::capacity)).c_str());
  reject.server_schema_hash      = server_schema_hash;
  reject.server_asset_table_hash = server_asset_table_hash;

  shared::message_stream_t stream;
  shared::write_reject(stream, reject);
  stream.finish();

  std::vector<network::uint8> payload;
  shared::begin_server_message_payload(payload, context.tick_number);
  shared::append_message_stream(payload, stream);
  network::send_message_payload_to_unslotted_peer(context.transport_layer, context.socket, address, payload);
}

} // namespace server
