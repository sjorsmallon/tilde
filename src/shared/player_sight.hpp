#pragma once

#include "entities/generated/entities_core_generated.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "subtick.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace entities
{
struct Player_Entity;
}

namespace shared
{

struct Entity_System;

// The Player_Entity flags a trigger press flips, predicted the same way Movement is.
struct held_toggles_t
{
  bool reveal_light_on       = false;
  bool reveal_light_overhead = false;
  bool held_lens_on          = false;
};

struct lens_watch_screen_t
{
  float fov_degrees = 90.f;
  float aspect      = 16.f / 9.f;
};

// A player as something that looks and shines: everything the reveal cones and the lens watchers are computed
// from. The server reads every row from the entities; a client overwrites its own row from its prediction.
struct player_sight_t
{
  entity_uid_t                              uid                   = null_entity_uid;
  bool                                      alive                 = false;
  linalg::vec3f                             feet                  = {0.f, 0.f, 0.f};
  subtick_view_t                            view_entering_tick    = {};
  held_toggles_t                            toggles               = {};
  lens_watch_screen_t                       screen                = {};
  std::optional<entities::Reveal_Cone_Kind> wielded_reveal_kind;
  uint32_t                                  wielded_lens_channels = 0;
};

[[nodiscard]] player_sight_t player_sight_from_entity(const Entity_System& system, const entities::Player_Entity& player);
void player_sights_from_entities(const Entity_System& system, std::vector<player_sight_t>& out);

// The one rule for a lit player: alive, the toggle on, a revealer or eraser in hand.
[[nodiscard]] inline bool reveal_light_is_on(const player_sight_t& sight)
{
  return sight.alive && sight.toggles.reveal_light_on && sight.wielded_reveal_kind.has_value();
}

// The channel bits the lens held to this eye passes: 0 unless alive with the flag set and a lens in hand.
[[nodiscard]] inline uint32_t held_lens_channels(const player_sight_t& sight)
{
  return sight.alive && sight.toggles.held_lens_on ? sight.wielded_lens_channels : 0;
}

} // namespace shared
