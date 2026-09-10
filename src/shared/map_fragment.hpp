#pragma once

#include "map.hpp"
#include "span.hpp"

// ============================================================================
// Cutting a piece out of a map, and stamping one back in.
//
// A PREFAB IS A MAP. Same grammar, same reader, same writer -- prefab_def.md is
// the design and map_format_def.md ss7 is the file's own side of it. That one
// decision is what makes four operations out of the two functions here:
//
//   copy selection  ->  extract_map_subset
//   paste / place   ->  stamp_map
//   save prefab     ->  save_map(path, subset)
//   load prefab     ->  try_load_map(path)
//
// So the editor's clipboard IS a map_t, and Ctrl+C / Ctrl+V gains connections
// as a side effect rather than as a feature of its own.
//
// Nothing in here knows about transactions, selection or the editor: it is
// map_t in, map_t out. The undo entry is the caller's to push.
// ============================================================================

namespace shared
{

// Where prefabs live: beside maps/, deliberately outside resources/. A prefab
// is an authoring artifact that never reaches a client, so it is not in the
// asset manifest and is not packaged.
inline constexpr const char* PREFAB_DIRECTORY = "prefabs";
inline constexpr const char* PREFAB_EXTENSION = ".prefab";

// One row that the boundary of a selection cuts through -- a row the copy would
// silently lose. Both directions, because both are lost and the author cares
// about both:
//
//   OUTBOUND: a selected sender whose target is outside (the button is
//             selected, the light it switches is not);
//   INBOUND:  an outside sender aimed at a selected entity (a trigger elsewhere
//             fires into the selected light).
//
// `index` is into map_t::connections, so the caller can act on the ROW -- draw
// it, or add the outside end to the selection -- rather than parse a sentence
// back apart.
struct crossing_connection_t
{
  size_t index = 0;
  // False for the inbound case. The sender is what decides whether the row is
  // copied at all, so this is also "would extraction have looked at this row".
  bool         sender_is_inside = false;
  entity_uid_t outside_uid      = null_entity_uid;
};

// Every row this selection cuts through, in map order. Empty means the subset
// is wiring-complete and nothing is lost by saving it.
//
// This is the SAME walk extract_map_subset decides with, so the warning the
// editor shows and the rows the file actually keeps cannot disagree. `Activator`
// and `Self` targets never cross -- they name no uid to cross with.
[[nodiscard]] std::vector<crossing_connection_t>
find_crossing_connections(const map_t& map, Span<const entity_uid_t> uids);

// The named objects, their wiring, and the materials they use, as a map of
// their own. Positions are rebased so the subset's ANCHOR -- bottom-centre of
// its bounds, the clipboard's convention -- sits at the origin, which is what
// makes the file's origin the place the author's cursor lands.
//
// Uids are KEPT: a subset's uids are already unique, and keeping them is what
// lets a row inside the subset go on naming the same objects. They are made
// fresh at the STAMP, not here.
//
// A row with an end outside the selection is dropped (find_crossing_connections
// is the list). `attached_cvars`, the navmesh and the lightmap are never
// copied: a prefab is objects and wiring, not game settings and not a bake.
[[nodiscard]] map_t extract_map_subset(const map_t& map, Span<const entity_uid_t> uids);

// What a stamp did, so the caller can select what it placed and push one undo
// entry for it.
struct stamp_result_t
{
  // The destination uids, in stamp order: entities first, then geometry.
  std::vector<entity_uid_t> uids;
  // Source uid -> destination uid, for anything that has to follow the copy.
  uid_remap_t remap;
  // Rows the source held that could not be rewritten. Zero for a fragment that
  // came out of extract_map_subset, which already dropped them.
  size_t dropped_connection_count = 0;
};

// Copies every member of `source` into `destination` at fresh uids, offset by
// `position`, and rewrites its wiring onto them. `source` is a fragment whose
// anchor is its origin, so `position` is where that anchor lands.
//
// Materials are matched by PATH through destination.material_index_for, never
// by index -- an index is only meaningful against the table it was minted from.
// Index 0 is the exception and is deliberately not remapped: entry 0 is the map
// DEFAULT, which is a property of the destination, not a path the source owns.
//
// A source with a non-empty `attached_cvars` is REFUSED with a line and nothing
// is stamped: those are the map's game settings, and a prefab has no business
// carrying them into someone else's map.
stamp_result_t stamp_map(map_t& destination, const map_t& source, const linalg::vec3& position);

} // namespace shared
