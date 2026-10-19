// Groups: a uid list beside the entities that the selection reads.
//
// What this pins is what makes "click one, get all" trustworthy:
//
//   1. EXCLUSIVE. group_objects pulls a member out of its old group and
//      dissolves what shrinks to nothing, so no operation can leave one object
//      in two groups; a group of one stays. A rename refuses an empty name and
//      an unknown uid.
//   2. INERT, not pruned. A deleted member stays in the list (undo brings it
//      back grouped); expand_to_group answers only the live ones.
//   3. The FILE. A group round-trips through the map text with its uid, its
//      name and its members, the dead member is skipped at the write, and
//      next_uid clears the group's uid on the way back in.
//   4. The LOAD CLEAN. prune_map_groups drops a missing member, refuses a
//      second claim on one uid, and dissolves what is left with no member.
//   5. The CARRIERS. Copy keeps the intersection, paste mints fresh uids and
//      remaps the members onto them, twice gives two groups.

#include "entities/generated/entities_core_generated.hpp"
#include "log.hpp"
#include "map/map.hpp"
#include "map/map_piece.hpp"
#include "map/map_group.hpp"

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
  if (find_group_containing_member(map, floor) == nullptr || find_group_containing_member(map, floor)->uid != first)
    return fail("a brush member is not found by find_group_containing_member");

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
  if (find_group_by_uid(map, first) == nullptr || find_group_by_uid(map, first)->members.size() != 1)
    return fail("the first group, left with the floor alone, should stay as a group of one");
  if (find_group_by_uid(map, second) == nullptr || !has_member(*find_group_by_uid(map, second), b))
    return fail("the second group, left with b alone, should stay as a group of one");
  if (map.groups.size() != 3 || find_group_containing_member(map, floor) == nullptr)
    return fail("the third group and the two groups of one should remain");
  if (!ungroup(map, first) || !ungroup(map, second) || map.groups.size() != 1 ||
      find_group_containing_member(map, floor) != nullptr || find_group_containing_member(map, b) != nullptr)
    return fail("ungrouping the groups of one did not leave their members loose");
  {
    const entity_uid_t floor_alone[] = {floor};
    const entity_uid_t floor_group = group_objects(map, floor_alone, "floor_alone");
    if (floor_group == null_entity_uid || map.groups.size() != 2)
      return fail("a group of one object was refused");
    if (!ungroup(map, floor_group))
      return fail("the group of one did not ungroup");
  }

  const entity_uid_t lonely[] = {9999, 9998};
  if (group_objects(map, lonely, "lonely") != null_entity_uid)
    return fail("a group with one real member was not refused");

  if (!try_rename_group(map, third, "renamed") || find_group_by_uid(map, third)->name != "renamed")
    return fail("renaming the third group did not take");
  if (try_rename_group(map, third, "") || find_group_by_uid(map, third)->name != "renamed")
    return fail("an empty group name was not refused");
  if (try_rename_group(map, 9999, "nobody"))
    return fail("renaming a group that does not exist was not refused");
  if (!try_rename_group(map, third, "third"))
    return fail("renaming the third group back did not take");

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
    if (!has_member(*find_group_containing_member(scratch, a), c))
      return fail("a deleted member must stay in the group's list, inert, for undo");
  }

  // ------------------------------------------------------------- 3. the file
  {
    // A group of {b, floor} where b is then deleted: written with its one live
    // member, floor alone. A group of {b} alone is not written at all. The
    // third group round-trips whole.
    map_t saved = map;
    const entity_uid_t bf[] = {b, floor};
    const entity_uid_t bf_group = group_objects(saved, bf, "b_and_floor");
    const entity_uid_t b_alone[] = {b};
    (void)group_objects(saved, b_alone, "will_not_be_written");
    saved.remove_object(b);

    const std::string text     = serialize_map_to_string(saved);
    map_t             reloaded = parse_map_from_string(text);

    if (reloaded.groups.size() != 2 || find_group_by_uid(reloaded, bf_group) == nullptr ||
        find_group_by_uid(reloaded, bf_group)->members.size() != 1)
      return fail("a group with one live member at save time should survive as a group of one, "
                  "and one with none should not be written");

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
    map_group_t nothing_left;
    nothing_left.uid     = dirty.next_uid++;
    nothing_left.name    = "nothing_left";
    nothing_left.members = {4343};
    dirty.groups.push_back(nothing_left);

    const size_t lines = prune_map_groups(dirty);
    if (lines != 4)
      return fail(std::format("prune should say four things (missing, duplicate, missing, dissolved), said {}",
                              lines));
    if (dirty.groups.size() != 2 || dirty.groups[0].name != "missing" ||
        dirty.groups[0].members.size() != 1 || !has_member(dirty.groups[0], a) ||
        dirty.groups[1].name != "claims_a" || dirty.groups[1].members.size() != 2 ||
        has_member(dirty.groups[1], a))
      return fail("prune left the wrong groups: the first keeps a alone; the second loses a; the third dissolves");
  }

  // ------------------------------------------------------------- 5. the carriers
  {
    // Extract {a, floor} out of a map whose group is {a, c}: one member comes
    // along, so a group of that one. Extract {floor}: no member, no group.
    // Extract {a, c, floor}: the group comes whole.
    const entity_uid_t partial[] = {a, floor};
    const map_t        one = copy_map_piece(map, partial);
    if (one.groups.size() != 1 || one.groups[0].members.size() != 1 || one.groups[0].members[0] != a)
      return fail("a piece holding one member of a group should carry a group of that one");
    const entity_uid_t none[] = {floor};
    if (!copy_map_piece(map, none).groups.empty())
      return fail("a piece holding no member of a group should carry no group");

    const entity_uid_t all[] = {a, c, floor};
    const map_t        piece = copy_map_piece(map, all);
    if (piece.groups.size() != 1 || piece.groups[0].members.size() != 2)
      return fail("a piece holding the whole group should carry it");

    map_t destination;
    destination.name = "destination.source";
    // Past every source uid, so "still names a source uid" below can only mean
    // a member that was not remapped, never a fresh uid that happens to match.
    destination.next_uid = 100;
    const entity_uid_t anchor_entity =
        add_at(destination, entities::entity_type::Player_Spawn_Entity, {0.f, 0.f, 0.f});
    (void)anchor_entity;

    const paste_result_t once  = paste_map_piece(destination, piece, {1000.f, 0.f, 0.f});
    const paste_result_t twice = paste_map_piece(destination, piece, {2000.f, 0.f, 0.f});
    if (destination.groups.size() != 2)
      return fail("two stamps of a grouped piece should give two groups");
    if (destination.groups[0].uid == destination.groups[1].uid)
      return fail("the two pasted groups share a uid");
    for (const map_group_t &group : destination.groups)
    {
      if (group.members.size() != 2)
        return fail("a pasted group lost members");
      for (entity_uid_t member : group.members)
      {
        if (!destination.has_object(member))
          return fail("a pasted group names a uid the destination does not have");
        if (member == a || member == c)
          return fail("a pasted group still names a source uid");
      }
    }
    const map_group_t *first_stamp = find_group_containing_member(destination, once.remap.at(a));
    if (first_stamp == nullptr || !has_member(*first_stamp, once.remap.at(c)))
      return fail("the first paste's group does not hold the first paste's copies");
    if (find_group_containing_member(destination, twice.remap.at(a)) == first_stamp)
      return fail("the second paste's copies landed in the first paste's group");
    if (destination.next_uid <= destination.groups[1].uid)
      return fail("the destination's next_uid did not clear the pasted group uids");
  }

  // --------------------------------------------- 8. dissolve when every member is gone
  {
    map_t deleted;
    const entity_uid_t x = add_at(deleted, entities::entity_type::Point_Light_Entity, {0.f, 0.f, 0.f});
    const entity_uid_t y = add_at(deleted, entities::entity_type::Point_Light_Entity, {64.f, 0.f, 0.f});
    const entity_uid_t z = add_at(deleted, entities::entity_type::Point_Light_Entity, {128.f, 0.f, 0.f});
    const entity_uid_t w = add_at(deleted, entities::entity_type::Point_Light_Entity, {192.f, 0.f, 0.f});
    const entity_uid_t xy[] = {x, y};
    const entity_uid_t zw[] = {z, w};
    (void)group_objects(deleted, xy, "xy");
    const entity_uid_t zw_group = group_objects(deleted, zw, "zw");

    deleted.remove_entity(x);
    if (dissolve_groups_with_no_live_member(deleted) != 0 || deleted.groups.size() != 2)
      return fail("a group with one live member left was dissolved");
    deleted.remove_entity(y);
    if (dissolve_groups_with_no_live_member(deleted) != 1 || deleted.groups.size() != 1 ||
        deleted.groups[0].uid != zw_group)
      return fail("a group whose every member is gone was not dissolved, or the wrong one was");
  }

  // ---------------------------------------------------------- 9. the recipe
  {
    map_t with_recipe = map;
    primitive_recipe_t recipe;
    recipe.kind = primitive_kind_t::Spiral_Stairs;
    recipe.quarter_turns = 3;
    recipe.stair_steps = 11;
    recipe.stair_rise = 12.5f;
    recipe.spiral_inner_radius = 40.f;
    recipe.spiral_tread_width = 72.f;
    recipe.spiral_degrees_per_step = 22.5f;
    recipe.spiral_clockwise = true;
    with_recipe.groups[0].recipe = recipe;

    const std::string text = serialize_map_to_string(with_recipe);
    map_t back = parse_map_from_string(text);
    if (back.groups.size() != 1 || !back.groups[0].recipe)
      return fail("a group's recipe did not survive the round trip");
    if (*back.groups[0].recipe != recipe)
      return fail("the round-tripped recipe differs from what was written");
    const size_t kind_at = text.find("spiral_stairs");
    if (kind_at == std::string::npos)
      return fail("a spiral's primitive block should name its kind");
    const std::string primitive_block = text.substr(kind_at, text.find("member", kind_at) - kind_at);
    if (primitive_block.find("axis") != std::string::npos)
      return fail("a spiral's primitive block should carry no axis");

    with_recipe.groups[0].recipe->kind = primitive_kind_t::Arch;
    with_recipe.groups[0].recipe->axis_index = 2;
    back = parse_map_from_string(serialize_map_to_string(with_recipe));
    if (!back.groups[0].recipe || back.groups[0].recipe->kind != primitive_kind_t::Arch ||
        back.groups[0].recipe->axis_index != 2 ||
        back.groups[0].recipe->stair_steps != primitive_recipe_t{}.stair_steps)
      return fail("an arch's block should carry its axis and only its own fields");

    primitive_recipe_t curve;
    curve.kind = primitive_kind_t::Curve;
    curve.quarter_turns = 1;
    curve.curve_points = {{0.f, 0.f, 0.f}, {96.5f, 0.f, 0.f}, {96.5f, 12.25f, -64.f}};
    curve.curve_width = 24.f;
    curve.curve_height = 80.f;
    curve.curve_samples_per_segment = 6;
    with_recipe.groups[0].recipe = curve;
    back = parse_map_from_string(serialize_map_to_string(with_recipe));
    if (!back.groups[0].recipe || *back.groups[0].recipe != *with_recipe.groups[0].recipe)
      return fail("a curve's block should carry its points, in order, and its three numbers");

    const entity_uid_t part[] = {a};
    map_t piece = copy_map_piece(with_recipe, part);
    if (piece.groups.size() != 1 || piece.groups[0].recipe)
      return fail("a copy of part of a primitive is a group of that part with no recipe");
    const entity_uid_t both[] = {a, c};
    piece = copy_map_piece(with_recipe, both);
    if (piece.groups.size() != 1 || !piece.groups[0].recipe)
      return fail("a copy of the whole primitive keeps its recipe");

    // ------------------------------------------------------- 10. the freeze
    if (drop_recipe_of_group_containing(with_recipe, b))
      return fail("a member of a group without a recipe has nothing to drop");
    if (drop_recipe_of_group_containing(with_recipe, 9999))
      return fail("an object in no group has nothing to drop");
    if (!drop_recipe_of_group_containing(with_recipe, a))
      return fail("a hand edit to a member drops its group's recipe");
    if (with_recipe.groups[0].recipe)
      return fail("the recipe should be gone after the freeze");
    if (drop_recipe_of_group_containing(with_recipe, a))
      return fail("a second drop has nothing to drop");
  }

  std::puts("map_group_test: all passed");
  return 0;
}
