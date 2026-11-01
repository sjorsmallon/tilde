#include "entities/generated/entities_tables_generated.hpp"
#include "../message_handlers.hpp"

#include "../../shared/assets/asset_id.hpp"
#include "../../shared/log.hpp"
#include "../entity_lifecycle.hpp"
#include "../server_context.hpp"
#include "../server_messages.hpp"
#include "../systems/game_rules_system.hpp"

#include <format>
#include <optional>
#include <string>

namespace server::messages
{

namespace
{

network::pascal_string_t<32> sanitized_player_name(const char* requested_name, int32_t slot)
{
  std::string filtered_name;
  for (const char* character = requested_name; *character != '\0'; ++character)
  {
    if (static_cast<unsigned char>(*character) >= 0x20 && static_cast<unsigned char>(*character) != 0x7f)
      filtered_name.push_back(*character);
  }

  if (filtered_name.empty())
    filtered_name = std::format("Player {}", slot);

  network::pascal_string_t<32> name;
  name.set(filtered_name.c_str());
  return name;
}

} // namespace

// A peer with no slot asking for one; the sender is context.incoming.unslotted_sender.
void on_connect(server_context_t &context, int32_t slot, const shared::Connect &value)
{
  if (slot >= 0)
  {
    log_warning("slot {} sent a second Connect; ignored", slot);
    return;
  }
  const network::Address sender = context.incoming.unslotted_sender;

  if (value.schema_hash != entities::SCHEMA_HASH)
  {
    log_error("Refusing connection from {}: schema hash mismatch (client {:#010x}, server {:#010x}). "
              "Both sides must be built from the same .def files.",
              value.player_name.c_str(), value.schema_hash, entities::SCHEMA_HASH);
    send_reject_to_unslotted_peer(context, sender,
                                  std::format("Schema mismatch: client {:#010x}, server {:#010x} -- rebuild "
                                              "against the same .def files",
                                              value.schema_hash, entities::SCHEMA_HASH),
                                  entities::SCHEMA_HASH, 0);
    return;
  }

  // An asset id on the wire names a file by position, so the trees must agree.
  const uint32_t server_asset_table_hash = assets::asset_table_hash();
  if (value.asset_table_hash != server_asset_table_hash)
  {
    log_error("Refusing connection from {}: asset table hash mismatch (client {:#010x}, server {:#010x}). "
              "Both sides must run against the same resources/ tree.",
              value.player_name.c_str(), value.asset_table_hash, server_asset_table_hash);
    send_reject_to_unslotted_peer(context, sender,
                                  std::format("Asset table mismatch: client {:#010x}, server {:#010x} -- the "
                                              "two resources/ trees differ",
                                              value.asset_table_hash, server_asset_table_hash),
                                  0, server_asset_table_hash);
    return;
  }

  int32_t free_slot = invalid_slot_idx;
  for (int32_t candidate = 0; candidate < network::sv_max_client_count; ++candidate)
  {
    if (!context.transport_layer.clients[candidate].occupied)
    {
      free_slot = candidate;
      break;
    }
  }
  if (free_slot == invalid_slot_idx)
  {
    send_reject_to_unslotted_peer(context, sender, "Server is full. Please try again later.", 0, 0);
    return;
  }

  const network::pascal_string_t<32> name = sanitized_player_name(value.player_name.c_str(), free_slot);
  connect_client(context, free_slot, sender, name);

  shared::Accept accept;
  accept.client_slot     = free_slot;
  accept.server_tickrate = static_cast<uint32_t>(context.cvars->sv_tickrate);
  accept.map             = current_server_map(context);
  shared::write_accept(context.outgoing.datagram[free_slot], accept);

  const std::vector<shared::Cvar_Value> mirrored = shared::collect_mirrored_cvars(*context.cvars);
  send_cvar_values(context, free_slot, mirrored);

  broadcast_server_text_message(context, std::format("{} joined the server (slot {})", name.c_str(), free_slot));

  if (try_find_rules_entity(context) != nullptr && current_mode(context).admit_on_connect)
    try_admit_player(context, free_slot, std::nullopt);
}

} // namespace server::messages
