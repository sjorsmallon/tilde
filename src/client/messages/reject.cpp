#include "entities/generated/entities_tables_generated.hpp"
#include "../client_context.hpp"
#include "../message_handlers.hpp"
#include "../../shared/assets/asset_id.hpp"
#include "../../shared/log.hpp"

namespace client::messages
{

// The server said no. A hash in the message is the mismatch it refused on,
// logged beside ours so both sides are in one line.
void on_reject(client_context_t &context, const shared::Reject &value)
{
  if (context.connection.phase != Connection_Phase::Connecting)
  {
    log_warning("a Reject arrived while not connecting ('{}'); ignored", value.reason.c_str());
    return;
  }

  if (value.server_schema_hash != 0 && value.server_schema_hash != entities::SCHEMA_HASH)
    log_error("Connection rejected -- schema hash mismatch (client {:#010x}, server {:#010x}). The two "
              "builds disagree about the .def files; rebuild both from the same ones. Server said: {}",
              entities::SCHEMA_HASH, value.server_schema_hash, value.reason.c_str());

  if (value.server_asset_table_hash != 0 && value.server_asset_table_hash != assets::asset_table_hash())
    log_error("Connection rejected -- asset table hash mismatch (client {:#010x}, server {:#010x}). The two "
              "resource trees differ, so an asset id would name different files on each side. Server said: {}",
              assets::asset_table_hash(), value.server_asset_table_hash, value.reason.c_str());

  log_terminal("Connection rejected: {}", value.reason.c_str());
  context.connection.phase = Connection_Phase::Disconnected;
}

} // namespace client::messages
