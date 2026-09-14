#include "map_piece.hpp"

#include "entities/entity_reflection.hpp"
#include "log.hpp"

#include <cstring>
#include <unordered_set>

namespace shared
{

namespace
{

using uid_set_t = std::unordered_set<entity_uid_t>;

uid_set_t make_uid_set(Span<const entity_uid_t> uids)
{
  uid_set_t set;
  set.reserve(uids.size());
  for (entity_uid_t uid : uids)
    set.insert(uid);
  return set;
}

// create_entity() gives back the CONCRETE type's raw pointer erased to Entity*,
// so the deleter has to be attached explicitly -- entities have no virtual
// destructor and never will. Same reason make_entity spells it out.
std::shared_ptr<entities::Entity> clone_into_shared(const entities::Entity* entity)
{
  entities::Entity* copy = entities::clone_entity(entity);
  if (copy == nullptr)
    return nullptr;
  return std::shared_ptr<entities::Entity>(copy, &entities::destroy_entity);
}

void collect_payload_uids(const connection_t& connection, std::vector<entity_uid_t>& out)
{
  if (!connection.has_override)
    return;

  const uint8_t* payload = entities::action_payload_bytes(connection.data);
  for (const field_info_t& field : entities::action_payload_fields(connection.data.tag))
  {
    if (field.type != FIELD_TYPE_ENTITY_UID)
      continue;

    entity_uid_t named = null_entity_uid;
    std::memcpy(&named, payload + field.offset, sizeof(named));
    if (named != null_entity_uid)
      out.push_back(named);
  }
}

// Every uid a row names apart from its sender: the target when it is one, and
// every entity-typed member of an override payload. A payload uid of
// null_entity_uid names nobody and is not an end.
void collect_row_targets(const connection_t& connection, std::vector<entity_uid_t>& out)
{
  out.clear();

  if (connection.target_kind == connection_target_t::Uid)
    out.push_back(connection.target);

  collect_payload_uids(connection, out);
}

// Whether an outbound row becomes an `Unbound` slot: its `Uid` target is the
// outside end, and nothing in its override payload is outside -- a payload uid
// has no kind to mark it unbound with, so a row like that is still dropped.
bool row_is_kept_as_unbound(const connection_t& connection, const uid_set_t& members)
{
  if (members.count(connection.sender) == 0)
    return false;
  if (connection.target_kind != connection_target_t::Uid || members.count(connection.target) > 0)
    return false;

  std::vector<entity_uid_t> payload_uids;
  collect_payload_uids(connection, payload_uids);
  for (entity_uid_t named : payload_uids)
    if (members.count(named) == 0)
      return false;
  return true;
}

// Bottom-centre of the union of the members' bounds. The cursor drives THAT, so
// a pasted group sits on the surface under it the way one placed object does.
linalg::vec3 compute_piece_anchor(const map_t& map, const uid_set_t& members)
{
  bool          any = false;
  aabb_bounds_t bounds{{0, 0, 0}, {0, 0, 0}};

  for (entity_uid_t uid : members)
  {
    if (!map.has_object(uid))
      continue;

    const aabb_bounds_t object = compute_object_bounds(map, uid);
    if (!any)
    {
      bounds = object;
      any    = true;
      continue;
    }

    bounds = union_aabb(bounds, object);
  }

  if (!any)
    return {0, 0, 0};

  return {(bounds.min.x + bounds.max.x) * 0.5f, bounds.min.y,
          (bounds.min.z + bounds.max.z) * 0.5f};
}

} // namespace

std::vector<connection_with_an_end_outside_t>
find_connections_with_an_end_outside_the_selection(const map_t&             map,
                                                   Span<const entity_uid_t> selection)
{
  const uid_set_t members = make_uid_set(selection);

  std::vector<connection_with_an_end_outside_t> outside_ends;
  std::vector<entity_uid_t>                     targets;

  for (size_t index = 0; index < map.connections.size(); ++index)
  {
    const connection_t& connection = map.connections[index];

    const bool sender_is_inside = members.count(connection.sender) > 0;
    collect_row_targets(connection, targets);

    // At most ONE entry per row, so a count of these is a count of connections
    // -- which is the sentence the editor has to write ("3 connections leave
    // this selection"). The first outside end is enough to name the row.
    if (sender_is_inside)
    {
      for (entity_uid_t target : targets)
      {
        if (members.count(target) > 0)
          continue;
        outside_ends.push_back({index, true, target, row_is_kept_as_unbound(connection, members)});
        break;
      }
      continue;
    }

    for (entity_uid_t target : targets)
    {
      if (members.count(target) == 0)
        continue;
      outside_ends.push_back({index, false, connection.sender});
      break;
    }
  }

  return outside_ends;
}

map_t copy_map_piece(const map_t& map, Span<const entity_uid_t> uids)
{
  const uid_set_t    members = make_uid_set(uids);
  const linalg::vec3 anchor  = compute_piece_anchor(map, members);

  map_t piece;

  // The whole table, uncompacted. save_map's build_material_remap drops what no
  // face names any more, which is the ONE place a material table is compacted;
  // doing it here as well would be a second rule free to disagree with it.
  piece.materials = map.materials;

  // Uids are kept, so the identity is the remap a row is tested against: a row
  // survives exactly when every uid it names is a member. That is the same
  // question find_connections_with_an_end_outside_the_selection answers, asked through the one walk of a
  // row's uids rather than through a second copy of the rule.
  uid_remap_t identity;
  identity.reserve(members.size());

  // Map order, not selection order: what comes out is diffable against what
  // went in, and a selection is a set with no order worth preserving.
  for (const map_entity_t& entry : map.entities)
  {
    if (members.count(entry.uid) == 0 || !entry.entity)
      continue;

    std::shared_ptr<entities::Entity> copy = clone_into_shared(entry.entity.get());
    if (!copy)
    {
      log_error("copy_map_piece: uid {} would not clone and was left out", entry.uid);
      continue;
    }

    copy->position = copy->position - anchor;
    piece.add_entity_with_uid(entry.uid, std::move(copy));
    identity[entry.uid] = entry.uid;
  }

  for (const map_geometry_t& entry : map.geometry)
  {
    if (members.count(entry.uid) == 0)
      continue;

    geometry_value_t value = entry.value;
    translate_geometry(value, linalg::vec3{0.f, 0.f, 0.f} - anchor);
    piece.add_geometry_with_uid(entry.uid, std::move(value));
    identity[entry.uid] = entry.uid;
  }

  // A group of part of the selection is a group of that part; one member or
  // none is no group. Uids are kept, like everything else in a piece.
  for (const map_group_t& group : map.groups)
  {
    map_group_t copy = group;
    if (remap_group_members(copy, identity) >= 2)
      add_group_with_uid(piece, std::move(copy));
  }

  for (const connection_t& connection : map.connections)
  {
    connection_t row = connection;

    // The slot. The target stays as the grouping key; remap passes an Unbound
    // target through, so the row survives on its sender alone.
    if (row_is_kept_as_unbound(row, members))
      row.target_kind = connection_target_t::Unbound;

    const connection_remap_result_t result = remap_connection_uids(row, identity);
    if (result.ok)
    {
      piece.connections.push_back(row);
      continue;
    }

    // Only worth a line when the row TOUCHED the selection. A map's other
    // wiring fails this test too, and saying so once per row would bury the
    // ones the author might actually have meant to take along.
    if (members.count(connection.sender) > 0)
      log_warning("copy_map_piece: a {} row is not included -- its {} (uid {}) is outside "
                  "the selection",
                  entities::to_string(connection.signal), result.end, result.unmapped);
  }

  return piece;
}

paste_result_t paste_map_piece(map_t& destination, const map_t& source, const linalg::vec3& position)
{
  paste_result_t result;

  if (!source.attached_cvars.empty())
  {
    log_error("paste_map_piece: refusing a piece carrying {} cvar line(s) -- those are the MAP's "
              "game settings and are not a prefab's to bring along. Nothing was pasted.",
              source.attached_cvars.size());
    return result;
  }

  result.uids.reserve(source.object_count());
  result.remap.reserve(source.object_count());

  struct pasted_entity_t
  {
    entity_uid_t       uid;
    entities::Entity*  entity;
  };
  std::vector<pasted_entity_t> pasted_entities;
  pasted_entities.reserve(source.entities.size());

  // A source material index resolved against the DESTINATION's table, by path.
  // Index 0 is the map DEFAULT and belongs to whichever map is being drawn, so
  // it is carried across as 0 rather than as the source's entry-0 path.
  const auto adopt_material = [&destination, &source](uint16_t index) -> uint16_t
  {
    if (index == 0)
      return 0;

    if (index >= source.materials.size())
    {
      log_error("paste_map_piece: a face names material {}, which the piece's table does not have "
                "({} entries) -- it falls back to the map default",
                index, source.materials.size());
      return 0;
    }

    return destination.material_index_for(source.materials[index]);
  };

  for (const map_entity_t& entry : source.entities)
  {
    if (!entry.entity)
      continue;

    std::shared_ptr<entities::Entity> copy = clone_into_shared(entry.entity.get());
    if (!copy)
    {
      log_error("paste_map_piece: uid {} would not clone and was not pasted", entry.uid);
      continue;
    }

    copy->position = copy->position + position;

    entities::Entity* placed  = copy.get();
    const entity_uid_t pasted = destination.add_entity(std::move(copy));
    result.remap[entry.uid]    = pasted;
    result.uids.push_back(pasted);
    pasted_entities.push_back({pasted, placed});
  }

  for (const map_geometry_t& entry : source.geometry)
  {
    geometry_value_t value = entry.value;
    translate_geometry(value, position);

    if (brush_geometry_t* brush = std::get_if<brush_geometry_t>(&value))
    {
      for (face_surface_t& face : brush->face_surfaces)
      {
        face.material       = adopt_material(face.material);
        face.blend_material = adopt_material(face.blend_material);
      }
    }

    // The TIE, through the same table everything else goes through. Done here
    // rather than in the field pass below because an owner is always an ENTITY
    // and every entity is already placed -- and because by the time that pass
    // runs this value has been moved into the destination.
    //
    // A brush tied to a Brush_Entity OUTSIDE the piece names an end outside the
    // selection too, and is the one kind the connection walk still does not
    // report -- so the author hears about it here: cleared loudly, never left
    // naming whatever happens to hold that uid in the destination.
    const entity_uid_t owner = get_owner_uid(value);
    if (owner != null_entity_uid)
    {
      const auto found = result.remap.find(owner);
      if (found == result.remap.end())
      {
        log_warning("paste_map_piece: geometry {} is tied to uid {}, which is not in the "
                    "piece -- untied",
                    entry.uid, owner);
        ++result.cleared_reference_count;
        set_owner_uid(value, null_entity_uid);
      }
      else
      {
        set_owner_uid(value, found->second);
      }
    }

    const entity_uid_t pasted = destination.add_geometry(std::move(value));
    result.remap[entry.uid]    = pasted;
    result.uids.push_back(pasted);
  }

  // The copies' own references, through the same table the rows go through. A
  // second pass because a copy can name a member placed after it, and a
  // geometry uid is a legal value for an entity field (one uid space). A
  // field naming nothing passes through; one naming something outside the
  // piece is cleared, since that uid means nothing in this map.
  for (const pasted_entity_t& pasted : pasted_entities)
  {
    uint8_t* base = reinterpret_cast<uint8_t*>(pasted.entity);
    for (const entities::leaf_field_t& leaf : entities::collect_leaf_fields(pasted.entity->type))
    {
      if (leaf.info->type != FIELD_TYPE_ENTITY_UID)
        continue;

      entity_uid_t named = null_entity_uid;
      std::memcpy(&named, base + leaf.offset, sizeof(named));
      if (named == null_entity_uid)
        continue;

      const auto found = result.remap.find(named);
      if (found == result.remap.end())
      {
        log_warning("paste_map_piece: {} (uid {}) field '{}' named uid {}, which is not in the "
                    "piece -- cleared",
                    entities::entity_info(pasted.entity->type).classname, pasted.uid,
                    leaf.name, named);
        ++result.cleared_reference_count;
        named = null_entity_uid;
      }
      else
      {
        named = found->second;
      }
      std::memcpy(base + leaf.offset, &named, sizeof(named));
    }
  }

  for (const connection_t& connection : source.connections)
  {
    connection_t                    row      = connection;
    const connection_remap_result_t remapped = remap_connection_uids(row, result.remap);
    if (!remapped.ok)
    {
      log_error("paste_map_piece: a {} row was dropped -- its {} (uid {}) is not in the piece",
                entities::to_string(connection.signal), remapped.end, remapped.unmapped);
      ++result.dropped_connection_count;
      continue;
    }

    destination.connections.push_back(row);
  }

  // The piece's groups, on the pasted copies. A fresh uid each, from the
  // destination's space: the piece's group uids mean nothing here.
  for (const map_group_t& group : source.groups)
  {
    map_group_t copy = group;
    if (remap_group_members(copy, result.remap) < 2)
      continue;
    copy.uid = destination.next_uid++;
    destination.groups.push_back(std::move(copy));
  }

  return result;
}

} // namespace shared
