#pragma once

#include "../../shared/map.hpp"
#include "../../shared/map_connection.hpp"
#include "../../shared/span.hpp"
#include "uid_pick.hpp"

#include <string>

#include <cstddef>
#include <vector>

namespace client
{

class Transaction_System;

// The selected entity's WIRING, over the map's one connection table, drawn
// into the current window (the sidebar's Connections tab). Draws nothing when
// the selection is geometry or resolves to no entity -- geometry has no type,
// so it emits nothing and accepts nothing.
void draw_connection_panel(shared::map_t &map, shared::entity_uid_t selected_uid,
                           Transaction_System &transactions, uid_pick_t &pick);

struct connection_counts_t
{
  size_t outbound = 0;
  size_t inbound  = 0;
};

// Rows this entity sends, and rows that name it by uid: the inbound list's rule.
[[nodiscard]] connection_counts_t count_connections_of(const shared::map_t &map, shared::entity_uid_t uid);

// How a row's receiver is SPELLED, for the author: "!activator", "!self", or
// the entity's label. Public because the viewport's connection lines label a
// role-targeted row with it, and two spellings of one role would disagree.
[[nodiscard]] std::string describe_connection_target(const shared::map_t &map,
                                                     const shared::connection_t &row);

// Write a clicked target into every named row, as ONE undo entry. The panel's
// own edits commit through a different path (an ImGui drag is many frames), so
// this is the one the viewport uses.
void commit_picked_connection_target(shared::map_t &map, Transaction_System &transactions,
                                     Span<const size_t> rows, shared::entity_uid_t target);

} // namespace client
