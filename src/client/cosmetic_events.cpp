#include "cosmetic_events.hpp"

#include "../shared/log.hpp"

#include <array>
#include <cassert>

namespace client
{

namespace
{

constexpr size_t handler_table_size = 64;

// Fixed-size table keyed by the underlying enum value. Closed enum + a max
// guard means we never need a map; lookup is O(1) and grep finds every slot.
std::array<effect_handler_fn, handler_table_size> g_handlers{};

} // namespace

// Forward-declared per-effect handlers; each lives in its own translation
// unit under src/client/effects/. Listed here so register_all_effect_handlers
// is the single grep-target for "what effects exist?".
namespace effects
{
void on_rocket_explosion(client_context_t &context,
                         const shared::effect_data_t &data);
void on_footstep(client_context_t &context,
                 const shared::effect_data_t &data);
void on_jump(client_context_t &context,
             const shared::effect_data_t &data);
void on_land(client_context_t &context,
             const shared::effect_data_t &data);
void on_bullet_impact(client_context_t &context,
                      const shared::effect_data_t &data);
}

void register_effect_handler(shared::effect_type_t type,
                             effect_handler_fn handler)
{
  size_t index = static_cast<size_t>(type);
  if (index >= g_handlers.size())
  {
    log_error("register_effect_handler: effect_type_t {} exceeds table size {}",
              static_cast<int>(type), g_handlers.size());
    assert(false);
    return;
  }
  if (g_handlers[index] != nullptr)
  {
    log_error("register_effect_handler: effect_type_t {} already registered",
              static_cast<int>(type));
    assert(false);
    return;
  }
  g_handlers[index] = handler;
}

void register_all_effect_handlers()
{
  static bool registered = false;
  if (registered) return;
  registered = true;

  register_effect_handler(shared::effect_type_t::ROCKET_EXPLOSION,
                          &effects::on_rocket_explosion);
  register_effect_handler(shared::effect_type_t::FOOTSTEP,
                          &effects::on_footstep);
  register_effect_handler(shared::effect_type_t::JUMP, &effects::on_jump);
  register_effect_handler(shared::effect_type_t::LAND, &effects::on_land);
  register_effect_handler(shared::effect_type_t::BULLET_IMPACT, &effects::on_bullet_impact);
  // BULLET_IMPACT: handler lands in a later PR.
}

void dispatch_received_effects(client_context_t &context,
                               const std::vector<shared::dispatched_effect_t> &effects)
{
  for (const auto &effect : effects)
  {
    size_t index = static_cast<size_t>(effect.type);
    if (index >= g_handlers.size() || g_handlers[index] == nullptr)
    {
      log_error("dispatch_received_effects: no handler registered for effect_type_t {}",
                shared::to_string(effect.type));
      assert(false);
      continue;
    }
    g_handlers[index](context, effect.data);
  }
}

} // namespace client
