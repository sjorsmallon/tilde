// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/effects/effects.def by def_gen. Do not edit.
//
// The receiving side's seam. Every handler below is REFERENCED here and
// DEFINED by hand, so a missing, misspelled or wrongly typed one fails at
// LINK time, naming the symbol. That link step is the assert: there is no
// registration, so "forgot to register" is not representable and the only
// surviving failure is "forgot to write it".
#include "effects/generated/effects_generated.hpp"
#include "event_handlers.hpp"

#include "log.hpp"

namespace client
{

// Channel Effect. One file per member under src/client/effects/.
namespace effects
{
void on_rocket_explosion(client_context_t& context, const shared::Rocket_Explosion& value);
void on_bullet_impact(client_context_t& context, const shared::Bullet_Impact& value);
void on_footstep(client_context_t& context, const shared::Footstep& value);
void on_jump(client_context_t& context, const shared::Jump& value);
void on_land(client_context_t& context, const shared::Land& value);
void on_flesh_impact(client_context_t& context, const shared::Flesh_Impact& value);
} // namespace effects

// The switch lives here rather than beside the codec because it is what
// references the consumers. Each payload is a stack local on its way to
// one call -- no vector, no tagged union, no member access to get wrong.
void dispatch_received_effects(client_context_t& context, network::Bit_Reader& reader,
                                 bool log_received)
{
  const uint32_t count = reader.read_bits(16);

  for (uint32_t index = 0; index < count; ++index)
  {
    const uint32_t kind = reader.read_bits(16);
    if (kind >= shared::EFFECT_TYPE_COUNT)
    {
      // The payload behind an unknown kind has an unknown length, so
      // every record after it is unreadable too.
      log_error("dispatch_received_effects: record {} of {} carries an "
                "unknown kind {}; dropping the rest of the batch", index, count, kind);
      return;
    }

    switch ((shared::effect_type)kind)
    {
      case shared::effect_type::Rocket_Explosion:
      {
        const std::optional<shared::Rocket_Explosion> payload = shared::try_read_rocket_explosion(reader);
        if (!payload)
        {
          // A field the tables cannot represent leaves the reader
          // mid-record, so the rest of the batch is gone with it.
          log_error("dispatch_received_effects: record {} of {} did not "
                    "decode; dropping the rest of the batch", index, count);
          return;
        }
        if (log_received)
          log_terminal("[event received] {}", shared::to_text(*payload));
        effects::on_rocket_explosion(context, *payload);
        break;
      }
      case shared::effect_type::Bullet_Impact:
      {
        const std::optional<shared::Bullet_Impact> payload = shared::try_read_bullet_impact(reader);
        if (!payload)
        {
          // A field the tables cannot represent leaves the reader
          // mid-record, so the rest of the batch is gone with it.
          log_error("dispatch_received_effects: record {} of {} did not "
                    "decode; dropping the rest of the batch", index, count);
          return;
        }
        if (log_received)
          log_terminal("[event received] {}", shared::to_text(*payload));
        effects::on_bullet_impact(context, *payload);
        break;
      }
      case shared::effect_type::Footstep:
      {
        const std::optional<shared::Footstep> payload = shared::try_read_footstep(reader);
        if (!payload)
        {
          // A field the tables cannot represent leaves the reader
          // mid-record, so the rest of the batch is gone with it.
          log_error("dispatch_received_effects: record {} of {} did not "
                    "decode; dropping the rest of the batch", index, count);
          return;
        }
        if (log_received)
          log_terminal("[event received] {}", shared::to_text(*payload));
        effects::on_footstep(context, *payload);
        break;
      }
      case shared::effect_type::Jump:
      {
        const std::optional<shared::Jump> payload = shared::try_read_jump(reader);
        if (!payload)
        {
          // A field the tables cannot represent leaves the reader
          // mid-record, so the rest of the batch is gone with it.
          log_error("dispatch_received_effects: record {} of {} did not "
                    "decode; dropping the rest of the batch", index, count);
          return;
        }
        if (log_received)
          log_terminal("[event received] {}", shared::to_text(*payload));
        effects::on_jump(context, *payload);
        break;
      }
      case shared::effect_type::Land:
      {
        const std::optional<shared::Land> payload = shared::try_read_land(reader);
        if (!payload)
        {
          // A field the tables cannot represent leaves the reader
          // mid-record, so the rest of the batch is gone with it.
          log_error("dispatch_received_effects: record {} of {} did not "
                    "decode; dropping the rest of the batch", index, count);
          return;
        }
        if (log_received)
          log_terminal("[event received] {}", shared::to_text(*payload));
        effects::on_land(context, *payload);
        break;
      }
      case shared::effect_type::Flesh_Impact:
      {
        const std::optional<shared::Flesh_Impact> payload = shared::try_read_flesh_impact(reader);
        if (!payload)
        {
          // A field the tables cannot represent leaves the reader
          // mid-record, so the rest of the batch is gone with it.
          log_error("dispatch_received_effects: record {} of {} did not "
                    "decode; dropping the rest of the batch", index, count);
          return;
        }
        if (log_received)
          log_terminal("[event received] {}", shared::to_text(*payload));
        effects::on_flesh_impact(context, *payload);
        break;
      }
    }
  }
}

} // namespace client
