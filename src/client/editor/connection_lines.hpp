#pragma once

#include "../../shared/entity_uid.hpp"
#include "../../shared/map_connection.hpp"
#include "../../shared/span.hpp"
#include "editor_types.hpp"

#include <cstddef>
#include <optional>
#include <cstdint>


namespace shared { struct map_t; }

namespace client
{

// What the viewport shows. Editor state, held by the tool and driven by a radio
// button: no cvar, because nothing outside this one tool reads it and it has no
// reason to survive the process.
enum class connection_line_mode_t : uint8_t
{
  Off = 0,
  Selection, // only rows whose sender or target is selected
  All,
};

const char* to_string(connection_line_mode_t mode);

// Every row the mode admits, projected through `view`. Takes the whole map for
// draw_entity_icons' reason: the pass is per FRAME and a per-row entry point
// would be a second place that has to agree about culling.
// `highlighted_row` is the row under the cursor in the Connections list, drawn
// whatever the mode says -- pointing at a row in the list IS the ask to see that
// wire, and it costs no selection change to answer. SIZE_MAX for none.
//
// `refusals` is validate_map_connections' answer for THIS map, passed in rather
// than recomputed: the tool already runs the check for its readout, and two runs
// are two answers free to disagree about which row is red.
void draw_connection_lines(const shared::map_t& map, const viewport_state_t& view,
                           connection_line_mode_t                  mode,
                           Span<const shared::entity_uid_t>         selection,
                           Span<const shared::connection_refusal_t> refusals,
                           Span<const shared::entity_uid_t>         hidden,
                           size_t                                   highlighted_row,
                           float                                    time_seconds);

// The map's WHOLE wiring as a list, drawn as a section of the Map Info panel --
// connections are map data, so they sit beside the map's cvars and not inside a
// tool. Returns the sender of a clicked row, which is what the caller selects:
// outputs are edited from the entity that has them.
//
// `hovered_row` is written every frame, SIZE_MAX when the cursor is on no row,
// and is what draw_connection_lines then draws bright -- pointing at a row IS
// the ask to see that wire, and it costs no selection change to answer.
[[nodiscard]] std::optional<shared::entity_uid_t>
draw_connection_overview(const shared::map_t& map,
                         Span<const shared::connection_refusal_t> refusals,
                         connection_line_mode_t& mode, size_t& hovered_row);

} // namespace client
