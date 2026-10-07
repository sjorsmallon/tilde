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

void draw_connection_panel(shared::map_t &map, shared::entity_uid_t selected_uid,
                           Transaction_System &transactions, uid_pick_t &pick);

struct connection_counts_t
{
  size_t outbound = 0;
  size_t inbound  = 0;
};

// Rows this entity sends, and rows that name it by uid.
[[nodiscard]] connection_counts_t count_connections_by_uid(const shared::map_t &map, shared::entity_uid_t uid);

// How a row's receiver is SPELLED, for the author: "!activator", "!self", or
// the entity's label. 
[[nodiscard]] std::string describe_connection_target(
  const shared::map_t &map,
  const shared::connection_t &row);


void commit_picked_connection_target(
  shared::map_t &map,
  Transaction_System &transactions,
  Span<const size_t> rows, 
  shared::entity_uid_t target);

} // namespace client
