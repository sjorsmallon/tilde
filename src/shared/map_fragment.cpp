#include "map_fragment.hpp"

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

// Every uid a row names apart from its sender: the target when it is one, and
// every entity-typed member of an override payload. A payload uid of
// null_entity_uid names nobody and is not an end.
void collect_row_targets(const connection_t& connection, std::vector<entity_uid_t>& out)
{
  out.clear();

  if (connection.target_kind == connection_target_t::Uid)
    out.push_back(connection.target);

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

// Bottom-centre of the union of the members' bounds. The cursor drives THAT, so
// a stamped group sits on the surface under it the way one placed object does.
linalg::vec3 compute_subset_anchor(const map_t& map, const uid_set_t& members)
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

std::vector<crossing_connection_t> find_crossing_connections(const map_t&              map,
                                                             Span<const entity_uid_t> uids)
{
  const uid_set_t members = make_uid_set(uids);

  std::vector<crossing_connection_t> crossings;
  std::vector<entity_uid_t>          targets;

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
        crossings.push_back({index, true, target});
        break;
      }
      continue;
    }

    for (entity_uid_t target : targets)
    {
      if (members.count(target) == 0)
        continue;
      crossings.push_back({index, false, connection.sender});
      break;
    }
  }

  return crossings;
}

map_t extract_map_subset(const map_t& map, Span<const entity_uid_t> uids)
{
  const uid_set_t    members = make_uid_set(uids);
  const linalg::vec3 anchor  = compute_subset_anchor(map, members);

  map_t subset;

  // The whole table, uncompacted. save_map's build_material_remap drops what no
  // face names any more, which is the ONE place a material table is compacted;
  // doing it here as well would be a second rule free to disagree with it.
  subset.materials = map.materials;

  // Uids are kept, so the identity is the remap a row is tested against: a row
  // survives exactly when every uid it names is a member. That is the same
  // question find_crossing_connections answers, asked through the one walk of a
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
      log_error("extract_map_subset: uid {} would not clone and was left out", entry.uid);
      continue;
    }

    copy->position = copy->position - anchor;
    subset.add_entity_with_uid(entry.uid, std::move(copy));
    identity[entry.uid] = entry.uid;
  }

  for (const map_geometry_t& entry : map.geometry)
  {
    if (members.count(entry.uid) == 0)
      continue;

    geometry_value_t value = entry.value;
    translate_geometry(value, linalg::vec3{0.f, 0.f, 0.f} - anchor);
    subset.add_geometry_with_uid(entry.uid, std::move(value));
    identity[entry.uid] = entry.uid;
  }

  for (const connection_t& connection : map.connections)
  {
    connection_t                    row    = connection;
    const connection_remap_result_t result = remap_connection_uids(row, identity);
    if (result.ok)
    {
      subset.connections.push_back(row);
      continue;
    }

    // Only worth a line when the row TOUCHED the selection. A map's other
    // wiring fails this test too, and saying so once per row would bury the
    // ones the author might actually have meant to take along.
    if (members.count(connection.sender) > 0)
      log_warning("extract_map_subset: a {} row is not included -- its {} (uid {}) is outside "
                  "the selection",
                  entities::to_string(connection.signal), result.end, result.unmapped);
  }

  return subset;
}

stamp_result_t stamp_map(map_t& destination, const map_t& source, const linalg::vec3& position)
{
  stamp_result_t result;

  if (!source.attached_cvars.empty())
  {
    log_error("stamp_map: refusing a fragment carrying {} cvar line(s) -- those are the MAP's "
              "game settings and are not a prefab's to bring along. Nothing was stamped.",
              source.attached_cvars.size());
    return result;
  }

  result.uids.reserve(source.object_count());
  result.remap.reserve(source.object_count());

  // A source material index resolved against the DESTINATION's table, by path.
  // Index 0 is the map DEFAULT and belongs to whichever map is being drawn, so
  // it is carried across as 0 rather than as the source's entry-0 path.
  const auto adopt_material = [&destination, &source](uint16_t index) -> uint16_t
  {
    if (index == 0)
      return 0;

    if (index >= source.materials.size())
    {
      log_error("stamp_map: a face names material {}, which the fragment's table does not have "
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
      log_error("stamp_map: uid {} would not clone and was not stamped", entry.uid);
      continue;
    }

    copy->position = copy->position + position;

    const entity_uid_t stamped = destination.add_entity(std::move(copy));
    result.remap[entry.uid]    = stamped;
    result.uids.push_back(stamped);
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

    const entity_uid_t stamped = destination.add_geometry(std::move(value));
    result.remap[entry.uid]    = stamped;
    result.uids.push_back(stamped);
  }

  for (const connection_t& connection : source.connections)
  {
    connection_t                    row      = connection;
    const connection_remap_result_t remapped = remap_connection_uids(row, result.remap);
    if (!remapped.ok)
    {
      log_error("stamp_map: a {} row was dropped -- its {} (uid {}) is not in the fragment",
                entities::to_string(connection.signal), remapped.end, remapped.unmapped);
      ++result.dropped_connection_count;
      continue;
    }

    destination.connections.push_back(row);
  }

  return result;
}

} // namespace shared
