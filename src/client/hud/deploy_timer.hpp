#pragma once

// How long until the weapon being raised is up, drawn as a banner.
//
// A POLLED value, not a pushed one, which is what keeps it out of
// announcement.hpp next door. An announcement is a discrete occurrence with a
// lifetime of its own -- it is set once, it counts itself down, and its model
// is what gets drawn. A deploy countdown is a continuous read of a clock that
// already exists (prediction_t::seconds_until_local_deploy_complete), so
// pushing it into a banner would be a second copy of that clock, free to
// disagree with the gate the shot is actually judged against. See the
// "continuous values are polled from the truth" rule in CLAUDE.md.
//
// So there is no state here and nothing to advance: the caller hands over the
// seconds it already holds, and zero draws nothing.
//
// It reads the CLIENT's predicted clock rather than the server's
// Inventory::deploy_complete_time, which is deliberate and is the only honest
// option: that deadline is sub-tick and server-only, and a replicated copy
// would be a round trip behind the keypress the player is watching the number
// for.

#include "../renderer.hpp"
#include "../ui/font.hpp"

namespace client::hud
{

// Nothing is drawn while `seconds_remaining` is at or below zero. Debug-shaped
// on purpose -- this is what cl_show_deploy_timer exists to look at, and a
// shipped HUD would show the draw animation instead of a number.
void draw_deploy_timer(renderer::ui_draw_list_t &list, const ui::ui_font_t &font,
                       linalg::vec2 screen, float display_scale, float seconds_remaining);

} // namespace client::hud
