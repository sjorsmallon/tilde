#pragma once

#include "../../shared/game_events.hpp"
#include "../client_context.hpp"

namespace client::kill_feed
{

// Phase 2 stub: logs the event so we can confirm end-to-end delivery. The
// real ImGui kill-feed renderer bolts into this same function in a later PR
// (see plan §"Phase 4"). Per-event signatures rather than a generic
// game_event_t so each consumer reads only the fields it cares about.
void on_rocket_detonated(client_context_t &context,
                         const shared::rocket_detonated_payload_t &payload);

// Phase 4 stub: logs the death. ROCKET_DETONATED is detonation-shaped (it
// fires once per rocket regardless of casualties), whereas PLAYER_DIED is
// per-victim — splash kills of multiple players in one blast produce one
// PLAYER_DIED each. Consumers that care about "who killed whom" should
// subscribe here, not derive it from ROCKET_DETONATED.
void on_player_died(client_context_t &context,
                    const shared::player_died_payload_t &payload);

// Phase 4 stub: logs the (re)spawn. Same event for connect-time spawn AND
// timed respawn after death — the kill feed can show "X joined" / "X
// respawned" identically (the schema-replicated player state tells us if
// they've existed on the client before, but right now we just log either
// way).
void on_player_spawned(client_context_t &context,
                       const shared::player_spawned_payload_t &payload);

} // namespace client::kill_feed
