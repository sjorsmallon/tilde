#pragma once

#include "../../shared/entities/generated/entities_generated.hpp"
#include "../../shared/span.hpp"
#include "../renderer.hpp"
#include "../ui/font.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace client::hud
{

// One drawn row. Flattened out of the entity on purpose: the draw half then
// needs no entity header, no client_context and no snapshot, which is what lets
// it be exercised with nothing but a baked font.
struct scoreboard_row_t
{
  std::string name;
  int32_t     kills    = 0;
  int32_t     deaths   = 0;
  int32_t     slot     = -1;
  bool        is_local = false;
  bool        is_alive = true;
};

// Fill `storage` from the snapshot's by-slot player view, in DRAW ORDER, and
// return the rows actually written.
//
// The sort is not cosmetic. `latest_player_entities` is an unordered_map, whose
// iteration order is free to change when it rehashes -- so drawing it in
// traversal order would let rows swap places mid-match for no reason the player
// could see. Ranked by kills, then by fewest deaths, and finally by slot, which
// is what makes the order TOTAL: without that last key two tied players still
// have no defined position relative to each other.
//
// `storage` is CAPACITY, not length, and that is the whole reason it is a Span
// rather than a vector. There is exactly one row per player, so a length is not
// something the caller could usefully pass: it would only be restating
// players.size(), and a restated number is free to disagree with the original.
// Handing over the whole buffer and getting the filled prefix back leaves no
// second copy of the count to be wrong. The one failure left is a real one --
// more players than the buffer holds -- and it is fatal.
[[nodiscard]] Span<scoreboard_row_t> collect_scoreboard_rows(
    const std::unordered_map<int32_t, entities::Player_Entity> &players,
    int32_t local_slot,
    Span<scoreboard_row_t> storage);

// Draw the panel centered on screen. An empty `rows` still draws the frame and
// its header: a board that vanishes when the last player leaves reads as a
// broken key, not as an empty server.
void draw_scoreboard(renderer::ui_draw_list_t &list, const ui::ui_font_t &font,
                     linalg::vec2 screen, float display_scale,
                     Span<const scoreboard_row_t> rows);

} // namespace client::hud
