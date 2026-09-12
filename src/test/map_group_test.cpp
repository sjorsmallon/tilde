// Groups: a uid list beside the entities that the selection reads.
//
// What this pins is what makes "click one, get all" trustworthy:
//
//   1. EXCLUSIVE. group_objects pulls a member out of its old group and
//      dissolves what shrinks below two, so no operation can leave one object
//      in two groups.
//   2. INERT, not pruned. A deleted member stays in the list (undo brings it
//      back grouped); expand_to_group answers only the live ones.
//   3. The FILE. A group round-trips through the map text with its uid, its
//      name and its members, the dead member is skipped at the write, and
//      next_uid clears the group's uid on the way back in.
//   4. The LOAD CLEAN. prune_map_groups drops a missing member, refuses a
//      second claim on one uid, and dissolves what is left with one member.
//   5. The CARRIERS. Extract keeps the intersection, stamp mints fresh uids and
//      remaps the members onto them, twice gives two groups.

#include "log.hpp"
#include "map.hpp"
#include "map_fragment.hpp"
#include "map_group.hpp"

#include <algorithm>
#include <cstdio>

using namespace shared;

static int fail(const std::string &message)
{
  log_error("map_group_test: {}", message);
  return 1;
}

static entity_uid_t add_at(map_t &map, entities::entity_type type, const linalg::vec3 &position)
{
  std::shared_ptr<entities::Entity> entity = make_entity(type);
  if (!entity)
    return null_entity_uid;
  entity->position = position;
  return map.add_entity(std::move(entity));
}

static bool has_member(const map_group_t &group, entity_uid_t uid)
{
  return std::find(group.members.begin(), group.members.end(), uid) != group.members.end();
}

int main()
{
  map_t map;
  map.name = "map_group_test.source";

  const entity_uid_t a = add_at(map, entities::entity_type::Point_Light_Entity, {0.f, 64.f, 0.f});
  const entity_uid_t b = add_at(map, entities::entity_type::Trigger_Volume_Entity, {64.f, 64.f, 0.f});
  const entity_uid_t c = add_at(map, entities::entity_type::Player_Spawn_Entity, {128.f, 64.f, 0.f});
  brush_geometry_t floor_brush = make_box_brush({64.f, 8.f, 0.f}, {64.f, 8.f, 64.f});
  sync_face_surfaces(floor_brush);
  const entity_uid_t floor = map.add_geometry(floor_brush);

  if (a == null_entity_uid || b == null_entity_uid || c == null_entity_uid)
    return fail("a fixture entity would not be created");

  // ------------------------------------------------------------- 1. exclusive
  const entity_uid_t abf[] = {a, b, floor};
  const entity_uid_t first = group_objects(map, abf, "first");
  if (first == null_entity_uid || map.groups.size() != 1 || map.groups[0].members.size() != 3)
    return fail("grouping three objects did not make one group of three");
  if (first < map.entities.size() + map.geometry.size() || map.next_uid != first + 1)
    return fail("the group's uid did not come out of the map's uid space");
  if (find_group_of(map, floor) == nullptr || find_group_of(map, floor)->uid != first)
    return fail("a brush member is not found by find_group_of");

  const entity_uid_t bc[] = {b, c};
  const entity_uid_t second = group_objects(map, bc, "second");
  if (second == null_entity_uid || map.groups.size() != 2)
    return fail("a second group of {b, c} was not made");
  if (has_member(*find_group_by_uid(map, first), b))
    return fail("b is still in the first group after being grouped again");
  if (find_group_by_uid(map, first)->members.size() != 2)
    return fail("the first group should be left with {a, floor}");

  const entity_uid_t ac[] = {a, c};
  const entity_uid_t third = group_objects(map, ac, "third");
  if (third == null_entity_uid)
    return fail("a third group of {a, c} was not made");
  if (find_group_by_uid(map, first) != nullptr)
    return fail("the first group, left with one member, was not dissolved");
  if (find_group_by_uid(map, second) != nullptr)
    return fail("the second group, left with one member, was not dissolved");
  if (map.groups.size() != 1 || find_group_of(map, floor) != nullptr)
    return fail("only the third group should remain, and the floor should be loose");

  const entity_uid_t lonely[] = {a, 9999};
  if (group_objects(map, lonely, "lonely") != null_entity_uid)
    return fail("a group with one real member was not refused");

  // ------------------------------------------------------------- 2. inert
  std::vector<entity_uid_t> picked;
  expand_to_group(map, floor, picked);
  if (picked.size() != 1 || picked[0] != floor)
    return fail("a loose object should expand to itself");
  picked.clear();
  expand_to_group(map, a, picked);
  if (picked.size() != 2 || !uid_sets_equal(picked, ac))
    return fail("a member should expand to its group's members");

  {
    map_t scratch = map;
    scratch.remove_object(c);
    std::vector<entity_uid_t> live;
    expand_to_group(scratch, a, live);
    if (live.size() != 1 || live[0] != a)
      return fail("a deleted member must not be in what a click selects");
    if (!has_member(*find_group_of(scratch, a), c))
      return fail("a deleted member must stay in the group's list, inert, for undo");
  }

  // ------------------------------------------------------------- 3. the file
  {
    // A group of {a, b} where b is then deleted: written with one live member,
    // so not written at all. The third group round-trips whole.
    map_t saved = map;
    const entity_uid_t ab[] = {a, b};
    (void)group_objects(saved, ab, "will_not_be_written");
    const entity_uid_t bf[] = {b, floor};
    const entity_uid_t bf_group = group_objects(saved, bf, "b_and_floor");
    saved.remove_object(b);

    const std::string text     = serialize_map_to_string(saved);
    map_t             reloaded = parse_map_from_string(text);

    if (reloaded.groups.size() != 0)
      return fail("a group with one live member at save time should not survive the round trip");
    (void)bf_group;

    // And one that should.
    map_t   whole = map;
    const std::string whole_text = serialize_map_to_string(whole);
    map_t   whole_back           = parse_map_from_string(whole_text);
    if (whole_back.groups.size() != 1)
      return fail("the third group did not round-trip");
    const map_group_t &back = whole_back.groups[0];
    if (back.uid != third || back.name != "third" || back.members.size() != 2 ||
        !has_member(back, a) || !has_member(back, c))
      return fail("the round-tripped group lost its uid, name or members");
    if (whole_back.next_uid <= third)
      return fail("next_uid must clear the group's uid after a load");
    if (whole_text.find("groups") == std::string::npos ||
        whole_text.find("member") == std::string::npos)
      return fail("the text should carry a groups block with member blocks");
  }

  // ------------------------------------------------------------- 4. the load clean
  {
    map_t dirty = map;
    dirty.groups.clear();
    map_group_t missing;
    missing.uid     = dirty.next_uid++;
    missing.name    = "missing";
    missing.members = {a, 4242};
    dirty.groups.push_back(missing);
    map_group_t claims_a;
    claims_a.uid     = dirty.next_uid++;
    claims_a.name    = "claims_a";
    claims_a.members = {a, b, c};
    dirty.groups.push_back(claims_a);

    const size_t lines = prune_map_groups(dirty);
    if (lines != 3)
      return fail(std::format("prune should say three things (missing, duplicate, dissolved), said {}",
                              lines));
    if (dirty.groups.size() != 1 || dirty.groups[0].name != "claims_a" ||
        dirty.groups[0].members.size() != 2 || has_member(dirty.groups[0], a))
      return fail("prune left the wrong groups: the first keeps a, then dissolves; the second loses a");
  }

  // ------------------------------------------------------------- 5. the carriers
  {
    // Extract {a, floor} out of a map whose group is {a, c}: one member comes
    // along, so no group. Extract {a, c, floor}: the group comes whole.
    const entity_uid_t partial[] = {a, floor};
    const map_t        one = extract_map_subset(map, partial);
    if (!one.groups.empty())
      return fail("a subset holding one member of a group should carry no group");

    const entity_uid_t all[] = {a, c, floor};
    const map_t        fragment = extract_map_subset(map, all);
    if (fragment.groups.size() != 1 || fragment.groups[0].members.size() != 2)
      return fail("a subset holding the whole group should carry it");

    map_t destination;
    destination.name = "destination.source";
    // Past every source uid, so "still names a source uid" below can only mean
    // a member that was not remapped, never a fresh uid that happens to match.
    destination.next_uid = 100;
    const entity_uid_t anchor_entity =
        add_at(destination, entities::entity_type::Player_Spawn_Entity, {0.f, 0.f, 0.f});
    (void)anchor_entity;

    const stamp_result_t once  = stamp_map(destination, fragment, {1000.f, 0.f, 0.f});
    const stamp_result_t twice = stamp_map(destination, fragment, {2000.f, 0.f, 0.f});
    if (destination.groups.size() != 2)
      return fail("two stamps of a grouped fragment should give two groups");
    if (destination.groups[0].uid == destination.groups[1].uid)
      return fail("the two stamped groups share a uid");
    for (const map_group_t &group : destination.groups)
    {
      if (group.members.size() != 2)
        return fail("a stamped group lost members");
      for (entity_uid_t member : group.members)
      {
        if (!destination.has_object(member))
          return fail("a stamped group names a uid the destination does not have");
        if (member == a || member == c)
          return fail("a stamped group still names a source uid");
      }
    }
    const map_group_t *first_stamp = find_group_of(destination, once.remap.at(a));
    if (first_stamp == nullptr || !has_member(*first_stamp, once.remap.at(c)))
      return fail("the first stamp's group does not hold the first stamp's copies");
    if (find_group_of(destination, twice.remap.at(a)) == first_stamp)
      return fail("the second stamp's copies landed in the first stamp's group");
    if (destination.next_uid <= destination.groups[1].uid)
      return fail("the destination's next_uid did not clear the stamped group uids");
  }

  std::puts("map_group_test: all passed");
  return 0;
}
