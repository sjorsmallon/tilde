#pragma once

// replay_def.md §8: the scrub panel. Every control it has is a write to
// replay_playback_t or to a cvar the console commands write too, so the panel
// and `replay_seek` / `replay_speed` / `replay_pause` cannot disagree.

namespace client
{

struct client_context_t;

// Nothing when no replay is playing, or when cl_replay_panel is off.
void draw_replay_panel(client_context_t& context);

} // namespace client
