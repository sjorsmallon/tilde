#include "../client_context.hpp"
#include "../held_snapshot.hpp"
#include "../message_handlers.hpp"
#include "../../shared/replay/replay_recorder.hpp"

#include <optional>
#include <utility>

namespace client::messages
{

// The snapshot at the datagram's tick: decoded against the history we hold and
// made the newest held snapshot. One we drop leaves no trace. Recorded here, so
// a recording holds the tick's snapshot before the messages that followed it.
void on_entity_package(client_context_t &context, const shared::Entity_Package &value)
{
  std::optional<decoded_snapshot_t> decoded = try_decode_snapshot(context, value);
  if (!decoded)
    return;

  shared::record_replay_tick(context.replay_recorder, decoded->frame, {}, {}, *context.cvars);
  advance_newest_held_snapshot(context, std::move(*decoded));
}

} // namespace client::messages
