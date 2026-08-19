// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/events/events.def by def_gen. Do not edit.
//
// The receiving side's seam. Every handler below is REFERENCED here and
// DEFINED by hand, so a missing, misspelled or wrongly typed one fails at
// LINK time, naming the symbol. That link step is the assert: there is no
// registration, so "forgot to register" is not representable and the only
// surviving failure is "forgot to write it".
#include "events/generated/events_generated.hpp"
#include "event_handlers.hpp"

#include "log.hpp"

namespace client
{

// Channel Game_Event. One file per member under src/client/game_events/.
namespace game_events
{
void on_rocket_detonated(client_context_t& context, const shared::Rocket_Detonated& value);
void on_player_died(client_context_t& context, const shared::Player_Died& value);
void on_player_spawned(client_context_t& context, const shared::Player_Spawned& value);
void on_round_phase_changed(client_context_t& context, const shared::Round_Phase_Changed& value);
} // namespace game_events

// The switch lives here rather than beside the codec because it is what
// references the consumers. Each payload is a stack local on its way to
// one call -- no vector, no tagged union, no member access to get wrong.
void dispatch_received_game_events(client_context_t& context, network::Bit_Reader& reader,
                                 bool log_received)
{
  const uint32_t count = reader.read_bits(16);

  for (uint32_t index = 0; index < count; ++index)
  {
    const uint32_t kind = reader.read_bits(16);
    if (kind >= shared::GAME_EVENT_TYPE_COUNT)
    {
      // The payload behind an unknown kind has an unknown length, so
      // every record after it is unreadable too.
      log_error("dispatch_received_game_events: record {} of {} carries an "
                "unknown kind {}; dropping the rest of the batch", index, count, kind);
      return;
    }

    switch ((shared::game_event_type)kind)
    {
      case shared::game_event_type::Rocket_Detonated:
      {
        const std::optional<shared::Rocket_Detonated> payload = shared::try_read_rocket_detonated(reader);
        if (!payload)
        {
          // A field the tables cannot represent leaves the reader
          // mid-record, so the rest of the batch is gone with it.
          log_error("dispatch_received_game_events: record {} of {} did not "
                    "decode; dropping the rest of the batch", index, count);
          return;
        }
        if (log_received)
          log_terminal("[event received] {}", shared::to_text(*payload));
        game_events::on_rocket_detonated(context, *payload);
        break;
      }
      case shared::game_event_type::Player_Died:
      {
        const std::optional<shared::Player_Died> payload = shared::try_read_player_died(reader);
        if (!payload)
        {
          // A field the tables cannot represent leaves the reader
          // mid-record, so the rest of the batch is gone with it.
          log_error("dispatch_received_game_events: record {} of {} did not "
                    "decode; dropping the rest of the batch", index, count);
          return;
        }
        if (log_received)
          log_terminal("[event received] {}", shared::to_text(*payload));
        game_events::on_player_died(context, *payload);
        break;
      }
      case shared::game_event_type::Player_Spawned:
      {
        const std::optional<shared::Player_Spawned> payload = shared::try_read_player_spawned(reader);
        if (!payload)
        {
          // A field the tables cannot represent leaves the reader
          // mid-record, so the rest of the batch is gone with it.
          log_error("dispatch_received_game_events: record {} of {} did not "
                    "decode; dropping the rest of the batch", index, count);
          return;
        }
        if (log_received)
          log_terminal("[event received] {}", shared::to_text(*payload));
        game_events::on_player_spawned(context, *payload);
        break;
      }
      case shared::game_event_type::Round_Phase_Changed:
      {
        const std::optional<shared::Round_Phase_Changed> payload = shared::try_read_round_phase_changed(reader);
        if (!payload)
        {
          // A field the tables cannot represent leaves the reader
          // mid-record, so the rest of the batch is gone with it.
          log_error("dispatch_received_game_events: record {} of {} did not "
                    "decode; dropping the rest of the batch", index, count);
          return;
        }
        if (log_received)
          log_terminal("[event received] {}", shared::to_text(*payload));
        game_events::on_round_phase_changed(context, *payload);
        break;
      }
    }
  }
}

} // namespace client
