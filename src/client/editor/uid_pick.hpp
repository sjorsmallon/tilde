#pragma once

#include "../../shared/entity_uid.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace client
{

// An entity FIELD as the destination of a pick: the uid of the entity holding
// it and the leaf's composed byte offset, which is how the inspector already
// addresses one. No name and no type -- the leaf that armed it was already
// checked to be FIELD_TYPE_ENTITY_UID.
struct field_pick_target_t
{
  shared::entity_uid_t entity = shared::null_entity_uid;
  uint32_t             offset = 0;
};

// The viewport half of "target by click": a panel arms it, the Selection tool
// resolves the next click into a uid, writes it HERE and swallows the click.
// Here is a connection row (the panel's target) or an entity field (the
// inspector's); `field` set means the latter and `row` is not read. It lives on
// the TOOL and is passed in, not held by a panel -- a static would survive a
// tool switch and a map load, and an armed pick that outlived the panel that
// armed it would rewrite a row the author is no longer looking at.
struct uid_pick_t
{
  bool   armed = false;
  size_t row   = 0; // index into map_t::connections
  std::optional<field_pick_target_t> field;
  // The other rows the SAME click fills: after a paste, every unbound row of
  // that stamp sharing `row`'s key, since they all aimed at one entity.
  std::vector<size_t> also_rows;
  // Unbound rows of the stamp still waiting for a click of their own; the next
  // group is armed when this one resolves. Escape drops them, leaving the rows
  // red where the panel's own Pick button still reaches them.
  std::vector<size_t> queued_rows;

  bool is_row_pick() const { return armed && !field.has_value(); }

  void arm_row(size_t index)
  {
    armed = true;
    row   = index;
    field.reset();
  }

  void arm_field(shared::entity_uid_t entity, uint32_t offset)
  {
    armed = true;
    field = field_pick_target_t{entity, offset};
    also_rows.clear();
  }

  void disarm()
  {
    armed = false;
    field.reset();
    also_rows.clear();
    queued_rows.clear();
  }
};

} // namespace client
