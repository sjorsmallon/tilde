// Prefabs: a piece cut out of a map, and pasted back into another one.
//
// prefab_def.md step 2 is what this pins, and the four things it pins are the
// four ways "a prefab is just a map" can go wrong:
//
//   1. EXTRACT keeps what is inside the selection, drops the rows that cross
//      its boundary, and rebases positions so the piece's anchor is its
//      origin -- with the arrangement inside it untouched.
//   2. find_connections_with_an_end_outside_the_selection sees BOTH directions, and sees exactly the
//      rows the copy lost. The warning the editor shows and the file the
//      author gets cannot disagree, because both come out of one walk.
//   3. STAMP mints fresh uids and rewrites every uid the wiring names onto
//      them -- including one buried in an override payload, which is what
//      FIELD_TYPE_ENTITY_UID exists for. Stamping twice gives two independent
//      copies and appends the material ONCE.
//   4. A piece carrying cvar lines is refused whole rather than half-pasted.
//
// Plus the round trip: the piece through serialize/parse is a no-op, since
// the file the prefab lands in is the same file format a map lands in.

#include "asset.hpp"
#include "log.hpp"
#include "map.hpp"
#include "map_connection.hpp"
#include "map_piece.hpp"

#include <cmath>
#include <cstdio>

using namespace shared;

static int fail(const std::string &message)
{
  log_error("prefab_test: {}", message);
  return 1;
}

static bool nearly(float a, float b) { return std::fabs(a - b) < 0.001f; }

static bool nearly(const linalg::vec3 &a, const linalg::vec3 &b)
{
  return nearly(a.x, b.x) && nearly(a.y, b.y) && nearly(a.z, b.z);
}

// The rule copy_map_piece rebases by, restated here so the test measures the
// piece rather than trusting the function that made it.
static linalg::vec3 anchor_of(const map_t &map)
{
  bool          any = false;
  aabb_bounds_t bounds{{0, 0, 0}, {0, 0, 0}};

  for (const map_entity_t &entry : map.entities)
  {
    const aabb_bounds_t object = compute_object_bounds(map, entry.uid);
    bounds                     = any ? union_aabb(bounds, object) : object;
    any                        = true;
  }
  for (const map_geometry_t &entry : map.geometry)
  {
    const aabb_bounds_t object = compute_object_bounds(map, entry.uid);
    bounds                     = any ? union_aabb(bounds, object) : object;
    any                        = true;
  }

  if (!any)
    return {0, 0, 0};
  return {(bounds.min.x + bounds.max.x) * 0.5f, bounds.min.y,
          (bounds.min.z + bounds.max.z) * 0.5f};
}

static const connection_t *find_row(const map_t &map, entities::entity_action action)
{
  for (const connection_t &row : map.connections)
    if (row.data.tag == action)
      return &row;
  return nullptr;
}

static const brush_geometry_t *first_brush(const map_t &map)
{
  for (const map_geometry_t &entry : map.geometry)
    if (const brush_geometry_t *brush = std::get_if<brush_geometry_t>(&entry.value))
      return brush;
  return nullptr;
}

static entity_uid_t add_at(map_t &map, entities::entity_type type, const linalg::vec3 &position)
{
  std::shared_ptr<entities::Entity> entity = make_entity(type);
  if (!entity)
    return null_entity_uid;
  entity->position = position;
  return map.add_entity(std::move(entity));
}

static const char *TEST_MATERIAL = "resources/textures/prefab_test_material";

int main()
{
  // A weapon carries a Render mesh, and compute_object_bounds resolves it, so
  // the asset system has to be mounted the way a launcher mounts it.
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::mount_asset_source();
  assets::init();

  // ---------------------------------------------------------------- the source
  //
  // A trigger, a light and a spawn marker that belong together, one brush under
  // them, and a damageable OUTSIDE the group that the wiring reaches in both
  // directions.
  map_t source;
  source.name = "prefab_test.source";

  const entity_uid_t trigger =
      add_at(source, entities::entity_type::Trigger_Volume_Entity, {200.f, 64.f, -100.f});
  const entity_uid_t light =
      add_at(source, entities::entity_type::Point_Light_Entity, {240.f, 96.f, -100.f});
  const entity_uid_t spawn =
      add_at(source, entities::entity_type::Player_Spawn_Entity, {160.f, 64.f, -140.f});
  const entity_uid_t crate =
      add_at(source, entities::entity_type::Damageable_Entity, {900.f, 64.f, 900.f});
  // Two weapons naming an owner through an `entity` FIELD rather than a row:
  // one names a member, one names the crate outside.
  const entity_uid_t owned_weapon =
      add_at(source, entities::entity_type::Weapon_Entity, {220.f, 64.f, -120.f});
  const entity_uid_t stray_weapon =
      add_at(source, entities::entity_type::Weapon_Entity, {180.f, 64.f, -80.f});

  if (trigger == null_entity_uid || light == null_entity_uid || spawn == null_entity_uid ||
      crate == null_entity_uid || owned_weapon == null_entity_uid ||
      stray_weapon == null_entity_uid)
    return fail("a fixture entity would not be created");

  entities::entity_as<entities::Weapon_Entity>(source.find_by_uid(owned_weapon)->entity.get())
      ->owner_uid = trigger;
  entities::entity_as<entities::Weapon_Entity>(source.find_by_uid(stray_weapon)->entity.get())
      ->owner_uid = crate;

  brush_geometry_t floor_brush = make_box_brush({200.f, 8.f, -100.f}, {64.f, 8.f, 64.f});
  sync_face_surfaces(floor_brush);
  if (floor_brush.face_surfaces.empty())
    return fail("the fixture brush produced no faces");
  floor_brush.face_surfaces.front().material = source.material_index_for(TEST_MATERIAL);
  const entity_uid_t floor = source.add_geometry(floor_brush);

  // 1. INSIDE: the trigger switches the light on.
  {
    connection_t row;
    row.sender      = trigger;
    row.signal      = entities::entity_signal::Touched;
    row.target_kind = connection_target_t::Uid;
    row.target      = light;
    row.data.tag    = entities::entity_action::Enable;
    source.connections.push_back(row);
  }
  // 2. OUTBOUND: the trigger also kills something that is not in the group.
  {
    connection_t row;
    row.sender      = trigger;
    row.signal      = entities::entity_signal::Touched;
    row.target_kind = connection_target_t::Uid;
    row.target      = crate;
    row.data.tag    = entities::entity_action::Kill;
    source.connections.push_back(row);
  }
  // 2b. OUTBOUND again, at the SAME outside entity from another sender: the
  //     two share a key, so a paste asks for the target once.
  {
    connection_t row;
    row.sender       = light;
    row.signal       = entities::entity_signal::Color_Changed;
    row.target_kind  = connection_target_t::Uid;
    row.target       = crate;
    row.data.tag     = entities::entity_action::Kill;
    row.has_override = true;
    source.connections.push_back(row);
  }
  // 3. INBOUND: something outside the group switches the group's light off.
  {
    connection_t row;
    row.sender       = crate;
    row.signal       = entities::entity_signal::Died;
    row.target_kind  = connection_target_t::Uid;
    row.target       = light;
    row.data.tag     = entities::entity_action::Disable;
    row.has_override = true;
    source.connections.push_back(row);
  }
  // 4. INSIDE, through a payload: the toucher's respawn point becomes the
  //    group's own spawn marker. The uid lives in the OVERRIDE, which is the
  //    one a remap can only follow because the field is typed as a uid.
  {
    connection_t row;
    row.sender      = trigger;
    row.signal      = entities::entity_signal::Touched;
    row.target_kind = connection_target_t::Activator;
    entities::Set_Respawn_Point_Data payload;
    payload.location = spawn;
    row.data         = entities::erase(payload);
    row.has_override = true;
    source.connections.push_back(row);
  }

  const std::vector<entity_uid_t> members = {trigger, light, spawn, owned_weapon, stray_weapon, floor};

  // ------------------------------------------------------- outside-end detection
  const std::vector<connection_with_an_end_outside_t> outside_ends = find_connections_with_an_end_outside_the_selection(source, members);
  if (outside_ends.size() != 3)
    return fail(std::format("expected 3 outside-end rows, got {}", outside_ends.size()));
  if (!outside_ends[0].sender_is_inside || outside_ends[0].index != 1 ||
      outside_ends[0].outside_uid != crate || !outside_ends[0].kept_as_unbound)
    return fail("the outbound outside-end row was not reported as one kept as a slot");
  if (!outside_ends[1].sender_is_inside || outside_ends[1].index != 2 ||
      outside_ends[1].outside_uid != crate || !outside_ends[1].kept_as_unbound)
    return fail("the second outbound outside-end row was not reported as one kept as a slot");
  if (outside_ends[2].sender_is_inside || outside_ends[2].index != 3 ||
      outside_ends[2].outside_uid != crate || outside_ends[2].kept_as_unbound)
    return fail("the inbound outside-end row was not reported as one that is lost");

  // ------------------------------------------------------------------ extract
  const map_t piece = copy_map_piece(source, members);

  if (piece.entities.size() != 5 || piece.geometry.size() != 1)
    return fail(std::format("piece holds {} entities and {} geometry, expected 5 and 1",
                            piece.entities.size(), piece.geometry.size()));
  if (piece.has_object(crate))
    return fail("the piece took an object that was not selected");
  if (!piece.attached_cvars.empty())
    return fail("the piece carried the map's cvar lines");

  if (piece.connections.size() != 4)
    return fail(std::format("piece holds {} connections, expected 2 internal + 2 slots",
                            piece.connections.size()));
  if (find_row(piece, entities::entity_action::Disable) != nullptr)
    return fail("the inbound outside-end row survived the copy");
  // The two outbound rows are SLOTS: unbound, keyed by the uid they aimed at.
  {
    size_t slots = 0;
    for (const connection_t &row : piece.connections)
    {
      if (row.data.tag != entities::entity_action::Kill)
        continue;
      ++slots;
      if (row.target_kind != connection_target_t::Unbound || row.target != crate)
        return fail("an outbound outside-end row was not kept as an Unbound slot keyed by its old target");
    }
    if (slots != 2)
      return fail(std::format("expected 2 slots in the piece, found {}", slots));
  }

  // The anchor rule, measured rather than trusted: bottom-centre at the origin.
  if (!nearly(anchor_of(piece), {0, 0, 0}))
    return fail("the piece's anchor is not at its origin");

  // ...and the arrangement inside it is untouched, which is usually the reason
  // a group was selected in the first place.
  {
    const map_entity_t *source_trigger   = source.find_by_uid(trigger);
    const map_entity_t *source_light     = source.find_by_uid(light);
    const map_entity_t *piece_trigger = piece.find_by_uid(trigger);
    const map_entity_t *piece_light   = piece.find_by_uid(light);
    if (!source_trigger || !source_light || !piece_trigger || !piece_light)
      return fail("the piece did not keep the source uids");
    if (!nearly(source_light->entity->position - source_trigger->entity->position,
                piece_light->entity->position - piece_trigger->entity->position))
      return fail("the copy moved the members relative to each other");
  }

  // -------------------------------------------------------------- round trip
  const std::string text      = serialize_map_to_string(piece);
  const map_t       reparsed  = parse_map_from_string(text);
  if (serialize_map_to_string(reparsed) != text)
    return fail("a piece does not survive its own file format");
  if (reparsed.connections.size() != 4)
    return fail("the piece's wiring did not survive the file format");
  {
    const connection_t *row = find_row(reparsed, entities::entity_action::Set_Respawn_Point);
    if (row == nullptr || row->data.as_set_respawn_point().location != spawn)
      return fail("the override's uid did not survive the file format");
    const connection_t *slot = find_row(reparsed, entities::entity_action::Kill);
    if (slot == nullptr || slot->target_kind != connection_target_t::Unbound || slot->target != crate)
      return fail("a slot did not survive the file format with its kind and key");
  }

  // ------------------------------------------------------------------- paste
  map_t destination;
  destination.name = "destination.source";

  const linalg::vec3 first_at{0.f, 0.f, 0.f};
  const linalg::vec3 second_at{512.f, 0.f, 0.f};

  const paste_result_t first  = paste_map_piece(destination, piece, first_at);
  const paste_result_t second = paste_map_piece(destination, piece, second_at);

  if (first.uids.size() != 6 || second.uids.size() != 6)
    return fail("a paste did not place every member");
  if (first.cleared_reference_count != 1 || second.cleared_reference_count != 1)
    return fail("a paste did not clear exactly the one field naming outside the piece");
  if (first.dropped_connection_count != 0 || second.dropped_connection_count != 0)
    return fail("a paste dropped a row of a piece that had no outside-end ones");
  if (destination.object_count() != 12)
    return fail(std::format("destination holds {} objects, expected 12",
                            destination.object_count()));

  for (entity_uid_t placed : first.uids)
    for (entity_uid_t other : second.uids)
      if (placed == other)
        return fail("the two stamps share a uid");

  if (destination.connections.size() != 8)
    return fail(std::format("destination holds {} connections, expected 8",
                            destination.connections.size()));

  // Every uid a row names is a uid of the copy it belongs to -- never the
  // piece's, and never the other copy's. A slot's KEY is the exception: it
  // is not a uid of any map and rides through untouched.
  for (int which = 0; which < 2; ++which)
  {
    const paste_result_t &paste = which == 0 ? first : second;

    const connection_t &enable_row = destination.connections[(size_t)which * 4 + 0];
    if (enable_row.data.tag != entities::entity_action::Enable)
      return fail("the pasted rows are not in piece order");
    if (enable_row.sender != paste.remap.at(trigger) || enable_row.target != paste.remap.at(light))
      return fail("a pasted row still names the piece's uids");

    const connection_t &slot_a = destination.connections[(size_t)which * 4 + 1];
    const connection_t &slot_b = destination.connections[(size_t)which * 4 + 2];
    if (slot_a.data.tag != entities::entity_action::Kill || slot_b.data.tag != entities::entity_action::Kill)
      return fail("the pasted rows are not in piece order");
    if (slot_a.sender != paste.remap.at(trigger) || slot_b.sender != paste.remap.at(light))
      return fail("a pasted slot's sender was not remapped");
    if (slot_a.target_kind != connection_target_t::Unbound || slot_a.target != crate ||
        slot_b.target_kind != connection_target_t::Unbound || slot_b.target != crate)
      return fail("a pasted slot did not keep its kind and key");

    const connection_t &respawn_row = destination.connections[(size_t)which * 4 + 3];
    if (respawn_row.data.tag != entities::entity_action::Set_Respawn_Point)
      return fail("the pasted rows are not in piece order");
    if (respawn_row.data.as_set_respawn_point().location != paste.remap.at(spawn))
      return fail("a pasted override payload still names the piece's uid");

    // A FIELD naming a member follows the copy exactly as a row does; one naming
    // the outside is cleared rather than left pointing at a coincidence.
    const map_entity_t* owned_copy = destination.find_by_uid(paste.remap.at(owned_weapon));
    const map_entity_t* stray_copy = destination.find_by_uid(paste.remap.at(stray_weapon));
    if (!owned_copy || !stray_copy)
      return fail("a pasted weapon is missing");
    if (entities::entity_as<entities::Weapon_Entity>(owned_copy->entity.get())->owner_uid !=
        paste.remap.at(trigger))
      return fail("a pasted entity field still names the piece's uid");
    if (entities::entity_as<entities::Weapon_Entity>(stray_copy->entity.get())->owner_uid !=
        null_entity_uid)
      return fail("a pasted entity field naming outside the piece was not cleared");

    // Where the anchor was told to land is where the copy sits.
    const linalg::vec3        offset = which == 0 ? first_at : second_at;
    const map_entity_t       *from   = piece.find_by_uid(trigger);
    const map_entity_t       *to     = destination.find_by_uid(paste.remap.at(trigger));
    if (!from || !to || !nearly(to->entity->position, from->entity->position + offset))
      return fail("a pasted entity did not land at the paste position");
  }

  // The loader refuses exactly the slots, by row, so build_session drops them
  // and the editor draws them red; nothing else about the paste is refused.
  {
    const std::vector<connection_refusal_t> refusals = validate_map_connections(destination);
    if (refusals.size() != 4)
      return fail(std::format("expected the 4 slots refused, got {} refusal(s)", refusals.size()));
    for (const connection_refusal_t &refusal : refusals)
    {
      if (destination.connections[refusal.index].target_kind != connection_target_t::Unbound)
        return fail("a refusal named a row that is not a slot");
      if (refusal.reason.find("unbound") == std::string::npos)
        return fail("a slot's refusal does not say it is unbound");
    }
  }

  // The material is matched by PATH, so two stamps of one piece append one
  // entry -- and index 0 stays the destination's own default.
  if (destination.materials.size() != 2 || destination.materials[0] != std::string() ||
      destination.materials[1] != TEST_MATERIAL)
    return fail("the destination's material table is not the piece's material appended once");
  {
    const brush_geometry_t *pasted = first_brush(destination);
    if (pasted == nullptr || pasted->face_surfaces.empty())
      return fail("a pasted brush lost its faces");
    if (pasted->face_surfaces.front().material != 1)
      return fail("a pasted face does not name the destination's index for its material");
  }

  // ------------------------------------------------- a piece with settings
  {
    map_t carrying = piece;
    carrying.attached_cvars.push_back("sv_gravity 200");

    map_t                refuser;
    const paste_result_t refused = paste_map_piece(refuser, carrying, {0, 0, 0});
    if (!refused.uids.empty() || refuser.object_count() != 0 || !refuser.connections.empty())
      return fail("a piece carrying cvar lines was pasted anyway");
  }

  // ----------------------------------------------------- the tie, across a paste
  //
  // A brush names its owner (prediction_def.md §4.2), so a paste has to rewrite
  // that the way it rewrites a row and an entity field: the copy's brush must
  // name the COPY's Brush_Entity, not the piece's, and not the other paste's.
  // A brush tied outside the piece is the field-that-crosses case, and it is
  // cleared loudly rather than left naming whatever holds that number here.
  {
    map_t tie_source;
    tie_source.name = "tie.source";

    auto [inside_owner, owner_entity] =
        spawn_entity(tie_source, entities::entity_type::Brush_Entity);
    if (!owner_entity)
      return fail("a brush_entity would not spawn");

    const entity_uid_t tied_brush =
        tie_source.add_geometry(make_box_brush({0.f, 0.f, 0.f}, {16.f, 16.f, 16.f}));
    const entity_uid_t stray_brush =
        tie_source.add_geometry(make_box_brush({64.f, 0.f, 0.f}, {16.f, 16.f, 16.f}));
    set_owner_uid(tie_source.find_geometry_by_uid(tied_brush)->value, inside_owner);
    // A uid from another map entirely: nothing in the piece answers to it.
    set_owner_uid(tie_source.find_geometry_by_uid(stray_brush)->value, 9999);

    map_t tie_destination;
    const paste_result_t first_tie  = paste_map_piece(tie_destination, tie_source, {0.f, 0.f, 0.f});
    const paste_result_t second_tie = paste_map_piece(tie_destination, tie_source, {512.f, 0.f, 0.f});

    if (first_tie.cleared_reference_count != 1 || second_tie.cleared_reference_count != 1)
      return fail("a paste did not clear exactly the one tie naming outside the piece");

    for (const paste_result_t *paste : {&first_tie, &second_tie})
    {
      const entity_uid_t copied_owner = paste->remap.at(inside_owner);
      const entity_uid_t copied_tied  = paste->remap.at(tied_brush);
      const entity_uid_t copied_stray = paste->remap.at(stray_brush);

      if (get_owner_uid(tie_destination.find_geometry_by_uid(copied_tied)->value) != copied_owner)
        return fail("a pasted brush does not name the pasted copy of its owner");
      if (get_owner_uid(tie_destination.find_geometry_by_uid(copied_stray)->value) !=
          null_entity_uid)
        return fail("a brush tied outside the piece came through still tied");
    }

    if (first_tie.remap.at(inside_owner) == second_tie.remap.at(inside_owner))
      return fail("the two stamps share an owner");
  }

  printf("prefab_test: OK (6 objects copied, 2 field references remapped, 2 slots kept, 1 outside-end row dropped, pasted "
         "twice with %zu connections remapped, material appended once)\n",
         destination.connections.size());
  return 0;
}
