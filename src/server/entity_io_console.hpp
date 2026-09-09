#pragma once

#include "../shared/entities/generated/entity_io_generated.hpp"

#include <string>
#include <string_view>
#include <vector>

// ============================================================================
// The console's half of entity I/O: turning `field=value ...` text into an
// action payload. entity_io_def.md ss11 step 6c.
//
// Split out of the ent_fire handler so it can be pinned without a server: the
// grammar has a real edge (a v3 writes as "1 0 0", so a value contains spaces)
// and getting it wrong sends the field's DEFAULT rather than failing, which is
// the silent kind of wrong.
// ============================================================================

namespace server
{

// The tail is parsed against the ACTION'S OWN FIELD TABLE, which is what lets a
// value contain spaces -- splitting on whitespace would cut `velocity=0 0 400`
// into three. The table is the lookahead instead:
//
//   parameters := pair*
//   pair       := name '=' value
//   name       := a field the action declares; any other identifier before an
//                 '=' is ordinary text belonging to the value it sits in
//   value      := every character up to the next `name=` boundary, or the end
//                 of the input, with surrounding whitespace trimmed
//
// So `velocity=0 0 400 keep_velocity=true` is two pairs and needs no quoting,
// and a value containing '=' is unambiguous as long as what precedes that '='
// is not one of the action's field names.
//
// `data.tag` selects the field table and must already be set; the payload is
// written in place, so a field no pair names keeps whatever it already held.
// Returns one sentence per thing that could not be read -- text before the
// first pair, and a value the field's own parser refused. Empty means the whole
// tail was consumed.
[[nodiscard]] std::vector<std::string> parse_action_parameters(entities::action_data_t& data,
                                                               std::string_view parameters);

} // namespace server
