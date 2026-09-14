#pragma once

#include "map.hpp"
#include "span.hpp"

// ============================================================================
// Copying a piece out of a map, and pasting one back in.
//
// A PREFAB IS A MAP. Same grammar, same reader, same writer -- prefab_def.md is
// the design and map_format_def.md ss7 is the file's own side of it. That one
// decision is what makes four operations out of the two functions here:
//
//   copy selection  ->  copy_map_piece
//   paste / place   ->  paste_map_piece
//   save prefab     ->  save_map(path, piece)
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
struct connection_with_an_end_outside_t
{
  size_t index = 0;
  // False for the inbound case. The sender is what decides whether the row is
  // copied at all, so this is also "would the copy have looked at this row".
  bool         sender_is_inside = false;
  entity_uid_t outside_uid      = null_entity_uid;
  // True when copy_map_piece KEEPS this row as an `Unbound` slot rather
  // than dropping it: outbound, the `Uid` target is the outside end, and no
  // override payload names anything outside. This is the same predicate
  // the copy applies, so the popup's "kept" and the file agree.
  bool kept_as_unbound = false;
};

// Every row with one end inside `selection` and one end outside it, in map
// order. Empty means the piece is wiring-complete and nothing is lost by saving
// it. "The selection" is whatever uid set the caller passes -- nothing in here
// knows the editor has one.
//
// This is the SAME walk copy_map_piece decides with, so the warning the editor
// shows and the rows the file actually keeps cannot disagree. `Activator` and
// `Self` targets are never reported -- they name no uid to be outside with.
[[nodiscard]] std::vector<connection_with_an_end_outside_t>
find_connections_with_an_end_outside_the_selection(const map_t&             map,
                                                   Span<const entity_uid_t> selection);

// The named objects, their wiring, and the materials they use, as a map of
// their own. Positions are rebased so the piece's ANCHOR -- bottom-centre of
// its bounds, the clipboard's convention -- sits at the origin, which is what
// makes the file's origin the place the author's cursor lands.
//
// Uids are KEPT: a piece's uids are already unique, and keeping them is what
// lets a row inside the piece go on naming the same objects. They are made
// fresh at the STAMP, not here.
//
// A row whose `Uid` TARGET is outside the selection is kept as an `Unbound`
// slot, the outside uid becoming its grouping key, so a paste can ask for the
// target once; any other row with an end outside is dropped
// (find_connections_with_an_end_outside_the_selection is the list, and `kept_as_unbound` says which).
// `attached_cvars`, the navmesh and the lightmap are never copied: a prefab is
// objects and wiring, not game settings and not a bake.
[[nodiscard]] map_t copy_map_piece(const map_t& map, Span<const entity_uid_t> uids);

// What a paste did, so the caller can select what it placed and push one undo
// entry for it.
struct paste_result_t
{
  // The destination uids, in paste order: entities first, then geometry.
  std::vector<entity_uid_t> uids;
  // Source uid -> destination uid, for anything that has to follow the copy.
  uid_remap_t remap;
  // Rows the source held that could not be rewritten. Zero for a piece that
  // came out of copy_map_piece, which already dropped them.
  size_t dropped_connection_count = 0;
  // Entity-typed fields of the pasted copies that named something outside the
  // piece and were set to null_entity_uid: a uid from another map names
  // nobody here, and leaving it is a reference to whatever happens to hold it.
  size_t cleared_reference_count = 0;
};

// Copies every member of `source` into `destination` at fresh uids, offset by
// `position`, and rewrites its wiring onto them. `source` is a piece whose
// anchor is its origin, so `position` is where that anchor lands.
//
// Materials are matched by PATH through destination.material_index_for, never
// by index -- an index is only meaningful against the table it was minted from.
// Index 0 is the exception and is deliberately not remapped: entry 0 is the map
// DEFAULT, which is a property of the destination, not a path the source owns.
//
// A source with a non-empty `attached_cvars` is REFUSED with a line and nothing
// is pasted: those are the map's game settings, and a prefab has no business
// carrying them into someone else's map.
paste_result_t paste_map_piece(map_t& destination, const map_t& source, const linalg::vec3& position);

} // namespace shared
