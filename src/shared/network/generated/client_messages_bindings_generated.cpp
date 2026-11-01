// Generated from /Users/sjors/Desktop/tilde/src/shared/network/messages.def by def_gen. Do not edit.
//
// The client's receiving seam. Every handler below is REFERENCED here and
// DEFINED by hand, so a missing, misspelled or wrongly typed one fails at
// LINK time, naming the symbol.
#include "network/generated/messages_generated.hpp"
#include "message_handlers.hpp"

#include "log.hpp"

namespace client
{

// One file per message under src/client/messages/.
namespace messages
{
void on_rocket_detonated(client_context_t& context, const shared::Rocket_Detonated& value);
void on_player_died(client_context_t& context, const shared::Player_Died& value);
void on_player_spawned(client_context_t& context, const shared::Player_Spawned& value);
void on_objective_reached(client_context_t& context, const shared::Objective_Reached& value);
void on_rocket_explosion(client_context_t& context, const shared::Rocket_Explosion& value);
void on_footstep(client_context_t& context, const shared::Footstep& value);
void on_jump(client_context_t& context, const shared::Jump& value);
void on_land(client_context_t& context, const shared::Land& value);
void on_shot_impact(client_context_t& context, const shared::Shot_Impact& value);
void on_jump_pad_launch(client_context_t& context, const shared::Jump_Pad_Launch& value);
void on_ping(client_context_t& context, const shared::Ping& value);
void on_pilot_impact(client_context_t& context, const shared::Pilot_Impact& value);
void on_server_text(client_context_t& context, const shared::Server_Text& value);
void on_accept(client_context_t& context, const shared::Accept& value);
void on_reject(client_context_t& context, const shared::Reject& value);
void on_change_map(client_context_t& context, const shared::Change_Map& value);
void on_cvar_values(client_context_t& context, const shared::Cvar_Values& value);
void on_ghost_available(client_context_t& context, const shared::Ghost_Available& value);
void on_entity_package(client_context_t& context, const shared::Entity_Package& value);
void on_shot_debug(client_context_t& context, const shared::Shot_Debug& value);
void on_bot_debug(client_context_t& context, const shared::Bot_Debug& value);
} // namespace messages

// A payload is one or more finished streams back to back, each starting on a
// byte; every record is dispatched in arrival order.
void dispatch_received_messages(client_context_t& context, network::Bit_Reader& reader,
                                shared::message_log_filter_t log_filter)
{
  while (reader.bits_remaining() >= (size_t)shared::MESSAGE_STREAM_COUNT_BITS)
  {
    const uint32_t count = reader.read_bits(shared::MESSAGE_STREAM_COUNT_BITS);

    for (uint32_t index = 0; index < count; ++index)
    {
      const uint32_t kind = reader.read_bits(16);
      if (kind >= shared::MESSAGE_TYPE_COUNT)
      {
        log_error("dispatch_received_messages: record {} of {} carries an unknown kind {}; dropping the rest", index, count, kind);
        return;
      }

      switch ((shared::Message_Type)kind)
      {
        case shared::Message_Type::Rocket_Detonated:
        {
          const std::optional<shared::Rocket_Detonated> payload = shared::try_read_rocket_detonated(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, true))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_rocket_detonated(context, *payload);
          break;
        }
        case shared::Message_Type::Player_Died:
        {
          const std::optional<shared::Player_Died> payload = shared::try_read_player_died(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, true))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_player_died(context, *payload);
          break;
        }
        case shared::Message_Type::Player_Spawned:
        {
          const std::optional<shared::Player_Spawned> payload = shared::try_read_player_spawned(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, true))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_player_spawned(context, *payload);
          break;
        }
        case shared::Message_Type::Objective_Reached:
        {
          const std::optional<shared::Objective_Reached> payload = shared::try_read_objective_reached(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, true))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_objective_reached(context, *payload);
          break;
        }
        case shared::Message_Type::Rocket_Explosion:
        {
          const std::optional<shared::Rocket_Explosion> payload = shared::try_read_rocket_explosion(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, false))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_rocket_explosion(context, *payload);
          break;
        }
        case shared::Message_Type::Footstep:
        {
          const std::optional<shared::Footstep> payload = shared::try_read_footstep(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, false))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_footstep(context, *payload);
          break;
        }
        case shared::Message_Type::Jump:
        {
          const std::optional<shared::Jump> payload = shared::try_read_jump(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, false))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_jump(context, *payload);
          break;
        }
        case shared::Message_Type::Land:
        {
          const std::optional<shared::Land> payload = shared::try_read_land(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, false))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_land(context, *payload);
          break;
        }
        case shared::Message_Type::Shot_Impact:
        {
          const std::optional<shared::Shot_Impact> payload = shared::try_read_shot_impact(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, false))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_shot_impact(context, *payload);
          break;
        }
        case shared::Message_Type::Jump_Pad_Launch:
        {
          const std::optional<shared::Jump_Pad_Launch> payload = shared::try_read_jump_pad_launch(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, false))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_jump_pad_launch(context, *payload);
          break;
        }
        case shared::Message_Type::Ping:
        {
          const std::optional<shared::Ping> payload = shared::try_read_ping(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, false))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_ping(context, *payload);
          break;
        }
        case shared::Message_Type::Pilot_Impact:
        {
          const std::optional<shared::Pilot_Impact> payload = shared::try_read_pilot_impact(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, false))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_pilot_impact(context, *payload);
          break;
        }
        case shared::Message_Type::Server_Text:
        {
          const std::optional<shared::Server_Text> payload = shared::try_read_server_text(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, true))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_server_text(context, *payload);
          break;
        }
        case shared::Message_Type::Accept:
        {
          const std::optional<shared::Accept> payload = shared::try_read_accept(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, false))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_accept(context, *payload);
          break;
        }
        case shared::Message_Type::Reject:
        {
          const std::optional<shared::Reject> payload = shared::try_read_reject(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, false))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_reject(context, *payload);
          break;
        }
        case shared::Message_Type::Change_Map:
        {
          const std::optional<shared::Change_Map> payload = shared::try_read_change_map(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, true))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_change_map(context, *payload);
          break;
        }
        case shared::Message_Type::Cvar_Values:
        {
          const std::optional<shared::Cvar_Values> payload = shared::try_read_cvar_values(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, true))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_cvar_values(context, *payload);
          break;
        }
        case shared::Message_Type::Ghost_Available:
        {
          const std::optional<shared::Ghost_Available> payload = shared::try_read_ghost_available(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, true))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_ghost_available(context, *payload);
          break;
        }
        case shared::Message_Type::Entity_Package:
        {
          const std::optional<shared::Entity_Package> payload = shared::try_read_entity_package(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, false))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_entity_package(context, *payload);
          break;
        }
        case shared::Message_Type::Shot_Debug:
        {
          const std::optional<shared::Shot_Debug> payload = shared::try_read_shot_debug(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, false))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_shot_debug(context, *payload);
          break;
        }
        case shared::Message_Type::Bot_Debug:
        {
          const std::optional<shared::Bot_Debug> payload = shared::try_read_bot_debug(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, false))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_bot_debug(context, *payload);
          break;
        }
        case shared::Message_Type::Console_Line:
        case shared::Message_Type::Connect:
        case shared::Message_Type::Disconnect:
        case shared::Message_Type::Map_Data_Request:
        case shared::Message_Type::Ghost_Request:
        case shared::Message_Type::Client_Input:
          log_error("dispatch_received_messages: {} is not sent to the client; dropping the rest", shared::to_string((shared::Message_Type)kind));
          return;
      }
    }

    reader.align();
  }
}

} // namespace client
