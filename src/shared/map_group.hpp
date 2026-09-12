#pragma once

#include "entity_uid.hpp"
#include "map_connection.hpp"
#include "span.hpp"

#include <string>
#include <vector>

// ============================================================================
// Groups: "click one, get all".
//
// A group is a LIST OF UIDS beside the entities, exactly the shape a connection
// row is and for the same reasons: geometry is not an entity, so a group field
// on Entity could never hold a brush, and a uid is stable in the file, so the
// list means the same objects tomorrow. It is map data the SELECTION reads --
// a click on a member takes the members -- and nothing below the editor ever
// asks about it: build_session does not carry it and the server never sees it.
//
// FLAT and EXCLUSIVE. An object belongs to at most one group and a group holds
// objects, never groups. Both are what keep a click meaning one thing; nesting
// is the same object having two groups, an inner and an outer, and then every
// pick needs an "enter level" gesture. group_objects enforces the exclusion,
// prune_map_groups refuses it out of a file.
//
// A member that stops existing is INERT, not pruned: deleting a member and
// undoing brings it back still grouped, which a prune on delete would break.
// The one place the list is cleaned is a load (prune_map_groups) and a save
// (the writer skips what is not there), the material table's rule.
// ============================================================================

namespace shared
{

struct map_t;

struct map_group_t
{
  // From the map's ONE uid space, so the outliner and undo address a group the
  // way they address everything else.
  entity_uid_t              uid = null_entity_uid;
  std::string               name;
  std::vector<entity_uid_t> members;

  bool operator==(const map_group_t&) const = default;
};

// The group this object belongs to, or null.
[[nodiscard]] const map_group_t* find_group_of(const map_t& map, entity_uid_t member);
[[nodiscard]] map_group_t*       find_group_of(map_t& map, entity_uid_t member);

[[nodiscard]] const map_group_t* find_group_by_uid(const map_t& map, entity_uid_t group_uid);

// What a click on `uid` selects: the group's members when it has one, the uid
// alone when it does not. Appended without duplicates, which is what lets a
// marquee call it once per hit.
void expand_to_group(const map_t& map, entity_uid_t uid, std::vector<entity_uid_t>& out);

// Whether two uid lists name the same set -- the click-through test: a plain
// click on a member of a group that is already exactly the selection narrows
// to the member. Compared against the group's LIVE members (expand_to_group),
// never its stored list, which may hold an inert deleted one.
[[nodiscard]] bool uid_sets_equal(Span<const entity_uid_t> left, Span<const entity_uid_t> right);

// Makes a group of the named objects under a fresh uid and returns it. Pulls
// every member out of whatever group it was in first, and dissolves a group
// that is left with fewer than two members -- so the operation cannot produce
// an overlap. Refuses with a line and null_entity_uid when fewer than two of
// the uids are objects the map has.
entity_uid_t group_objects(map_t& map, Span<const entity_uid_t> uids, std::string name);

// A group with a KNOWN uid: the file reader and the carriers, which have a uid
// to keep or a remap to honour. Bumps next_uid past it like the other with_uid
// adders.
void add_group_with_uid(map_t& map, map_group_t group);

// Dissolves the group; its members become loose objects. False when no group
// has that uid.
bool ungroup(map_t& map, entity_uid_t group_uid);

// Rewrites the members through a remap, DROPPING the ones it does not map --
// the intersection rule: a copy of part of a group is a group of that part.
// Returns how many survived.
size_t remap_group_members(map_group_t& group, const uid_remap_t& remap);

// The load-time clean: drops a member the map does not have, a member already
// claimed by an earlier group, and then any group left with fewer than two
// members, one line each. Returns how many lines it had to say.
size_t prune_map_groups(map_t& map);

} // namespace shared
