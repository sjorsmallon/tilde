#pragma once

#include "../../shared/map.hpp"
#include "../../shared/map_connection.hpp"

#include <string>

#include <cstddef>

namespace client
{

class Transaction_System;

// The viewport half of "target by click": the panel arms it, the Selection tool
// resolves the next click into a uid and swallows it. It lives on the TOOL and
// is passed in, not held here -- a static would survive a tool switch and a map
// load, and an armed pick that outlived the panel that armed it would rewrite a
// row the author is no longer looking at.
struct connection_pick_t
{
  bool   armed = false;
  size_t row   = 0; // index into map_t::connections
};

// The selected entity's WIRING, over the map's one connection table. Draws
// nothing when the selection is geometry or resolves to no entity -- geometry
// has no type, so it emits nothing and accepts nothing.
void draw_connection_panel(shared::map_t &map, shared::entity_uid_t selected_uid,
                           Transaction_System &transactions, connection_pick_t &pick);

// How a row's receiver is SPELLED, for the author: "!activator", "!self", or
// the entity's label. Public because the viewport's connection lines label a
// role-targeted row with it, and two spellings of one role would disagree.
[[nodiscard]] std::string describe_connection_target(const shared::map_t &map,
                                                     const shared::connection_t &row);

// Write a clicked target into one row, as its own undo entry. The panel's own
// edits commit through a different path (an ImGui drag is many frames), so this
// is the one the viewport uses.
void commit_picked_connection_target(shared::map_t &map, Transaction_System &transactions,
                                     size_t row, shared::entity_uid_t target);

} // namespace client
