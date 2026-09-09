#pragma once

#include "../../shared/array.hpp"
#include "../../shared/entities/generated/entities_generated.hpp"
#include "../../shared/entity_uid.hpp"
#include "../../shared/span.hpp"

#include <optional>
#include <unordered_set>
#include <vector>

// The map's entities as a list, with an eye per row and per type.
//
// HIDING IS EDITOR STATE AND NEVER MAP DATA, and the reason is the transaction
// system: every map_t edit goes through it, so a `hidden` field in the map
// either pushes an undo entry (Ctrl+Z unhides, and eats the edit the author
// meant to undo) or punches a hole in the one rule that makes undo
// trustworthy. `geometry_surface_t::visible` is a different thing wearing the
// same word -- a RENDER property, beside shader_type and roughness, meaning the
// surface does not draw in the GAME. Two visibility concepts one eye-icon apart
// is how an authoring convenience ends up shipping in a level.
//
// It is not persisted. Hide state that survives a reload is also hide state
// that leaves you staring at a map with something missing and no reason on
// screen, which is why the hidden COUNT is always on the panel. If it turns out
// to be missed, the home is an editor state file keyed by map path (the
// last_map.txt shape), never a sidecar beside the map: the two sidecars this
// project has hold derived bake products that SHIP with the map, and this is
// the opposite of all three.
//
// THE SET IS NEVER PRUNED, and that is correctness rather than laziness.
// Iterate the MAP and ask whether a uid is hidden, never iterate the set -- a
// uid whose entity is gone is then inert, and deleting a hidden entity and
// undoing brings it back STILL HIDDEN, which a prune pass would break. Safe
// because `map_t::next_uid` is monotonic within a map, so a stale uid can never
// name a different entity. The one sync point is a map LOAD, which resets the
// uid space: clear it there.

namespace shared { struct map_t; }

namespace client
{

struct entity_visibility_t
{
  // Per ENTITY, by uid.
  std::unordered_set<shared::entity_uid_t> hidden_entities;

  // Per TYPE, which needs no syncing at all, being keyed by an enum rather than
  // by a uid. The outliner's group header IS this control -- a separate type
  // filter would be a second place saying one thing.
  Enum_Array<entities::entity_type, bool> hidden_types;

  // Flattened once per frame for editor_context_t::hidden_objects, the way
  // objects_without_collision is: a Span the tools read, out of the one pass
  // that already knows both halves.
  std::vector<shared::entity_uid_t> hidden_this_frame;

  [[nodiscard]] bool is_hidden(entities::entity_type type, shared::entity_uid_t uid) const
  {
    return hidden_types[type] || hidden_entities.count(uid) != 0;
  }

  [[nodiscard]] bool anything_hidden() const;

  void show_all();

  // Rebuilds `hidden_this_frame` from the map. Call once per frame before the
  // context is handed to a tool.
  void refresh(const shared::map_t& map);
};

// The panel. Returns the uid of a clicked row, which the caller selects through
// editor_context_t::requested_selection -- the outliner does not own the
// selection any more than the connection list does.
[[nodiscard]] std::optional<shared::entity_uid_t>
draw_entity_outliner(const shared::map_t& map, entity_visibility_t& visibility);

} // namespace client
