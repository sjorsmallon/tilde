#include "entity_system.hpp"

#include "log.hpp"

namespace shared
{

Entity_System::Entity_System()
{
  // Every tag in the generated enum gets a pool, sized from the table. The enum
  // IS the list — there is nothing to keep in sync and no per-type switch, which
  // is the whole point of entity_system_def.md §1. Index 0 is Invalid and is
  // left at its default (stride 0, count 0); nothing ever indexes it, because
  // `locations` only ever names a type an entity actually carries.
  for (uint32_t index = 1; index < entities::ENTITY_TYPE_COUNT; ++index)
  {
    const entities::entity_type      type = (entities::entity_type)index;
    const entities::entity_type_info_t &info = entities::entity_info(type);

    // The buffer comes from operator new, which guarantees only fundamental
    // alignment. Every entity is scalars, vec3f, enums and inline strings today,
    // so this is slack rather than a constraint — but a future over-aligned
    // field would silently hand construct_at misaligned storage, and this says
    // so instead.
    assert(info.alignment <= alignof(std::max_align_t) &&
           "an entity is over-aligned for a std::vector<std::byte> pool");

    pools[index].type   = type;
    pools[index].stride = info.size_in_bytes;
  }
}

void Entity_System::reset()
{
  for (Entity_Pool &pool : pools)
    pool.reset();

  locations.clear();
  next_entity_id = 1; // Reset ID counter when clearing entities
}

void Entity_System::remove_at_slot(entities::entity_type type, uint32_t slot)
{
  Entity_Pool &pool = pools[(uint32_t)type];
  if (slot >= pool.count)
  {
    log_error("Entity_System::remove_at_slot: slot {} is out of range (the {} pool holds {})", slot,
              entities::entity_info(type).classname, pool.count);
    return;
  }

  // Read the uid BEFORE the removal — afterwards the slot holds a different
  // entity (or nothing), so there would be no way back to the one being freed.
  const entity_uid_t removed_uid = pool.at(slot)->entity_id;

  const pool_removal_t removal = pool.remove_at(slot);
  if (!removal.removed)
    return; // remove_at already reported the out-of-range slot

  locations.erase(removed_uid);

  // The swap-and-pop moved the pool's last entity into the freed slot, so that
  // entity's location is now stale. This one line is the entire cost of keeping
  // the pools dense.
  if (removal.moved_uid != null_entity_uid)
    locations[removal.moved_uid] = {type, slot};
}

bool Entity_System::destroy(entity_uid_t uid)
{
  auto location_it = locations.find(uid);
  if (location_it == locations.end())
    return false;

  const entity_location_t location = location_it->second;
  remove_at_slot(location.type, location.slot);
  return true;
}

void Entity_System::add_entity(entity_uid_t uid, const entities::Entity *entity)
{
  if (entity == nullptr)
    return;

  if (uid == null_entity_uid)
  {
    log_error("Entity_System::add_entity: a {} was handed uid 0, which is the null "
              "sentinel — not added",
              entities::classname_of(entity));
    return;
  }

  auto existing = locations.find(uid);
  if (existing != locations.end())
  {
    log_error("Entity_System::add_entity: uid {} is already held by a {} — not added. "
              "The map's uid space is supposed to be unique.",
              uid, entities::entity_info(existing->second.type).classname);
    return;
  }

  // The pool is selected BY the entity's own tag, so the type mismatch that
  // add_existing used to report back through a sentinel slot cannot happen from
  // here at all — push_copy asserts on it as a backstop, not as a path.
  Entity_Pool &pool = pools[(uint32_t)entity->type];

  // The uid goes in as a PARAMETER and is stamped on the pool's copy. It used to
  // be written through the caller's shared_ptr first — which is the map the
  // caller owns, not the session — so `const map_t&` was a lie and initializing
  // a session renumbered the map (P7 step 1, entity_storage_def.md §4).
  //
  // Map-loaded entities carry their canonical uid here. Runtime-spawned entities
  // go through Entity_System::spawn() instead, which assigns from next_entity_id.
  // Both ID spaces are unified by populate_from_map below.
  entities::Entity *copy = pool.push_copy(entity);
  copy->entity_id        = uid;

  locations[uid] = {entity->type, pool.count - 1};
}

void Entity_System::populate_from_map(const map_t &map)
{
  reset();
  for (const map_entity_t &entry : map.entities)
  {
    if (!entry.entity)
    {
      log_error("map '{}' holds uid {} with no entity behind it — dropping it, "
                "but the map is malformed and whatever wrote it is the bug",
                map.name, entry.uid);
      continue;
    }
    add_entity(entry.uid, entry.entity.get());
  }

  // Continue runtime IDs from where the map left off so map-loaded and
  // runtime-spawned IDs share one monotonic space.
  if (map.next_uid > next_entity_id)
    next_entity_id = map.next_uid;
}

bool Entity_System::validate_locations() const
{
  bool valid = true;

  // Every indexed uid must name a live slot holding that same uid.
  for (const auto &[uid, location] : locations)
  {
    const Entity_Pool &pool = pools[(uint32_t)location.type];
    if (location.slot >= pool.count)
    {
      log_error("validate_locations: uid {} indexes slot {} of a {} pool holding {}", uid,
                location.slot, entities::entity_info(location.type).classname, pool.count);
      valid = false;
      continue;
    }

    const entity_uid_t actual = pool.at(location.slot)->entity_id;
    if (actual != uid)
    {
      log_error("validate_locations: uid {} indexes slot {} of the {} pool, but that slot "
                "holds uid {}",
                uid, location.slot, entities::entity_info(location.type).classname, actual);
      valid = false;
    }
  }

  // ...and every live entity must be indexed. The direction that catches a
  // spawn path which forgot to register.
  for (const Entity_Pool &pool : pools)
  {
    for (uint32_t slot = 0; slot < pool.count; ++slot)
    {
      const entity_uid_t uid = pool.at(slot)->entity_id;
      auto location_it = locations.find(uid);
      if (location_it == locations.end())
      {
        log_error("validate_locations: the {} pool holds uid {} in slot {}, which is not "
                  "in the index",
                  entities::entity_info(pool.type).classname, uid, slot);
        valid = false;
        continue;
      }

      if (location_it->second.type != pool.type || location_it->second.slot != slot)
      {
        log_error("validate_locations: uid {} lives in slot {} of the {} pool but is "
                  "indexed at slot {} of the {} pool",
                  uid, slot, entities::entity_info(pool.type).classname, location_it->second.slot,
                  entities::entity_info(location_it->second.type).classname);
        valid = false;
      }
    }
  }

  return valid;
}

} // namespace shared
