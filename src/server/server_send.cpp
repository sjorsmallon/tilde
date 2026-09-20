#include "server_send.hpp"

#include "../shared/network/bitstream.hpp"
#include "../shared/network/cvar_mirror.hpp"
#include "../shared/network/entity_snapshot.hpp"
#include "../shared/network/reliable_stream.hpp"
#include "../shared/network/server_transport_layer.hpp"
#include "../shared/replay_recorder.hpp"
#include "log.hpp"
#include "send_protobuf_message.hpp"
#include "server_context.hpp"
#include "server_messages.hpp"

#include <vector>

namespace server
{

// Bots have no client, so nothing else on the wire says where one thinks it is
// going; this is the only way to see a path in the viewport.
static void send_bot_debug(server_context_t &context)
{
  if (context.world.bots.empty())
    return;

  game::S2C_BotDebug bot_debug_message;
  for (const auto &bot : context.world.bots)
  {
    auto *entry = bot_debug_message.add_bots();
    entry->set_slot(bot.player_slot);
    entry->set_goal(static_cast<int>(bot.goal));
    entry->set_type(static_cast<int>(bot.type));
    entry->set_path_index(bot.path_index);
    for (const auto &wp : bot.path)
    {
      auto *v = entry->add_path();
      v->set_x(wp.x);
      v->set_y(wp.y);
      v->set_z(wp.z);
    }
  }

  for (connected_client_t row : connected_clients(context))
  {
    ::send_protobuf_message(context, row.transport.address, bot_debug_message);
  }
}

static void broadcast_changed_cvar_values(server_context_t &context)
{
  shared::cvar_values_message_t changed =
      shared::collect_changed_mirrored_cvars(*context.cvars,
                                             context.last_broadcast_cvars);
  if (changed.values.empty())
    return;

  for (connected_client_t row : connected_clients(context))
    send_cvar_values(context, row.slot, changed);

  // just copy the whole struct.
  context.last_broadcast_cvars = *context.cvars;

  for (const shared::cvar_value_t &value : changed.values)
    log_terminal("Mirroring '{}' = {} to connected clients",
                 cvars::cvar_info(value.id).name, value.text);
}

static void send_reliable_blocks_if_there_are_any(server_context_t &context)
{
  for (connected_client_t row : connected_clients(context))
  {
    network::Reliable_Stream& stream = row.transport.reliable_stream;

    if (network::reliable_outbound_has_overflowed(stream))
    {
      log_error("slot {} has {} bytes of unconfirmed reliable data (cap {}); it "
                "has stopped acking while we kept queueing",
                row.slot, network::reliable_pending_bytes(stream),
                network::RELIABLE_OUTBOUND_CAP_IN_BYTES);
      disconnect_client(context, row.slot, "overflowed its reliable stream.");
      continue;
    }

    if (context.cvars->sv_reliable_debug && stream.block_length == 0 &&
        network::reliable_pending_bytes(stream) != 0)
    {
      network::visit_pending_reliable_records(
          stream, [&](network::uint8 message_type, network::uint32 length,
                      size_t offset) {
            log_terminal("[reliable] slot {}: record type {} at +{} ({} bytes)",
                         row.slot, static_cast<int>(message_type), offset, length);
          });
    }

    network::send_reliable_block(context.transport_layer, context.socket, row.slot);
  }
}

void send_to_clients(server_context_t &context)
{
  send_bot_debug(context);

  network::snapshot_frame_t& frame = context.replication.snapshot_history.slot_for(context.tick_number);
  frame.clear();
  frame.tick = context.tick_number;

  frame.copy_replicated_entities_from(context.world.session.entity_system);

  // Serialize and send to each client with per-client delta compression
  for (connected_client_t row : connected_clients(context))
  {
    const int32_t slot = row.slot;

    // if the client doesn't have the map ready, they don't need deltas or full updates.
    if (!row.client.map_ready) continue;

    auto writer = network::Bit_Writer{};

    // what's the diff against the snapshot the client holds?
    const network::snapshot_frame_t* baseline =
        context.replication.snapshot_history.find(context.clients[slot].held_snapshot_tick);

    network::serialize_snapshot(writer, frame, baseline);

    // create and send package
    game::S2C_EntityPackage package;
    package.set_server_tick(context.tick_number);
    package.set_latest_processed_input_number(context.clients[slot].latest_processed_input_number);
    network::set_snapshot_baseline(package, baseline);

    package.set_entity_data(writer.buffer.data(), writer.buffer.size());
    ::send_protobuf_message(context, row.transport.address, package);
  }

  // send the effects in a batch.
  std::vector<network::uint8> effect_batch_bytes;
  if (!context.outgoing.effects.empty())
  {
    //@NOTE(SJM): why is this finish necessary?
    context.outgoing.effects.finish();

    game::S2C_EffectBatch batch;
    batch.set_effect_data(context.outgoing.effects.writer.buffer.data(),
                          context.outgoing.effects.writer.buffer.size());
    batch.set_server_tick(context.tick_number);

    for (connected_client_t row : connected_clients(context))
    {
      if (!row.client.map_ready) continue;
      ::send_protobuf_message(context, row.transport.address, batch);
    }

    effect_batch_bytes.resize(batch.ByteSizeLong());
    batch.SerializeToArray(effect_batch_bytes.data(), static_cast<int>(effect_batch_bytes.size()));
  }

  // send gameplay events in a batch. note that this is _reliable_ transfer
  // because events cause gameplay state to change. cosmetic events nobody
  // cares about. entity updates need neither: each is a delta against a tick the client says it holds.
  std::vector<network::uint8> event_batch_bytes;
  if (!context.outgoing.events.empty())
  {

    context.outgoing.events.finish();

    game::S2C_GameEventBatch batch;
    batch.set_event_data(context.outgoing.events.writer.buffer.data(),
                         context.outgoing.events.writer.buffer.size());
    batch.set_server_tick(context.tick_number);

    event_batch_bytes.resize(batch.ByteSizeLong());
    batch.SerializeToArray(event_batch_bytes.data(),
                           static_cast<int>(event_batch_bytes.size()));

    for (connected_client_t row : connected_clients(context))
      network::queue_reliable_message(
          row.transport.reliable_stream,
          static_cast<network::uint8>(network::Message_Type::S2C_GameEventBatch),
          event_batch_bytes);
  }

  shared::record_replay_tick(context.world.replay_recorder, frame,
                             Span<const uint8_t>(effect_batch_bytes),
                             Span<const uint8_t>(event_batch_bytes), *context.cvars);

  // in theory redundant but just so we don't have stale shit to send.
  clear_outgoing(context);

  broadcast_changed_cvar_values(context);

  // if we had reliable data to send, do so.
  send_reliable_blocks_if_there_are_any(context);
}

} // namespace server
