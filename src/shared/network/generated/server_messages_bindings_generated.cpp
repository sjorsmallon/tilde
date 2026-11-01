// Generated from /Users/sjors/Desktop/tilde/src/shared/network/messages.def by def_gen. Do not edit.
//
// The server's receiving seam. Every handler below is REFERENCED here and
// DEFINED by hand, so a missing, misspelled or wrongly typed one fails at
// LINK time, naming the symbol.
#include "network/generated/messages_generated.hpp"
#include "message_handlers.hpp"

#include "log.hpp"

namespace server
{

// One file per message under src/server/messages/.
namespace messages
{
void on_console_line(server_context_t& context, int32_t slot, const shared::Console_Line& value);
void on_connect(server_context_t& context, int32_t slot, const shared::Connect& value);
void on_disconnect(server_context_t& context, int32_t slot, const shared::Disconnect& value);
void on_map_data_request(server_context_t& context, int32_t slot, const shared::Map_Data_Request& value);
void on_ghost_request(server_context_t& context, int32_t slot, const shared::Ghost_Request& value);
void on_client_input(server_context_t& context, int32_t slot, const shared::Client_Input& value);
} // namespace messages

// A payload is one or more finished streams back to back, each starting on a
// byte; every record is dispatched in arrival order.
void dispatch_received_messages(server_context_t& context, int32_t slot, network::Bit_Reader& reader,
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
        log_error("dispatch_received_messages: record {} of {} from slot {} carries an unknown kind {}; dropping the rest", index, count, slot, kind);
        return;
      }

      switch ((shared::Message_Type)kind)
      {
        case shared::Message_Type::Console_Line:
        {
          const std::optional<shared::Console_Line> payload = shared::try_read_console_line(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, true))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_console_line(context, slot, *payload);
          break;
        }
        case shared::Message_Type::Connect:
        {
          const std::optional<shared::Connect> payload = shared::try_read_connect(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, false))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_connect(context, slot, *payload);
          break;
        }
        case shared::Message_Type::Disconnect:
        {
          const std::optional<shared::Disconnect> payload = shared::try_read_disconnect(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, false))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_disconnect(context, slot, *payload);
          break;
        }
        case shared::Message_Type::Map_Data_Request:
        {
          const std::optional<shared::Map_Data_Request> payload = shared::try_read_map_data_request(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, true))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_map_data_request(context, slot, *payload);
          break;
        }
        case shared::Message_Type::Ghost_Request:
        {
          const std::optional<shared::Ghost_Request> payload = shared::try_read_ghost_request(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, true))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_ghost_request(context, slot, *payload);
          break;
        }
        case shared::Message_Type::Client_Input:
        {
          const std::optional<shared::Client_Input> payload = shared::try_read_client_input(reader);
          if (!payload)
          {
            log_error("dispatch_received_messages: record {} of {} did not decode; dropping the rest", index, count);
            return;
          }
          if (shared::message_log_filter_admits(log_filter, false))
            log_terminal("[message received] {}", shared::to_text(*payload));
          messages::on_client_input(context, slot, *payload);
          break;
        }
        case shared::Message_Type::Rocket_Detonated:
        case shared::Message_Type::Player_Died:
        case shared::Message_Type::Player_Spawned:
        case shared::Message_Type::Objective_Reached:
        case shared::Message_Type::Rocket_Explosion:
        case shared::Message_Type::Footstep:
        case shared::Message_Type::Jump:
        case shared::Message_Type::Land:
        case shared::Message_Type::Shot_Impact:
        case shared::Message_Type::Jump_Pad_Launch:
        case shared::Message_Type::Ping:
        case shared::Message_Type::Pilot_Impact:
        case shared::Message_Type::Server_Text:
        case shared::Message_Type::Accept:
        case shared::Message_Type::Reject:
        case shared::Message_Type::Change_Map:
        case shared::Message_Type::Cvar_Values:
        case shared::Message_Type::Ghost_Available:
        case shared::Message_Type::Entity_Package:
        case shared::Message_Type::Shot_Debug:
        case shared::Message_Type::Bot_Debug:
          log_error("dispatch_received_messages: {} is not sent to the server; dropping the rest", shared::to_string((shared::Message_Type)kind));
          return;
      }
    }

    reader.align();
  }
}

} // namespace server
