#pragma once

#include "array.hpp"
#include "entities/entity_reflection.hpp"
#include "log.hpp"
#include "map.hpp"
#include "span.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace shared
{

// What a removal did to the pool, which is the only thing Entity_System needs
// back in order to keep its uid index honest. remove_at swaps the last element
// into the freed slot, so exactly one OTHER entity may have changed address —
// this names it.
struct pool_removal_t
{
  bool         removed   = false;              // false means the slot was out of range
  entity_uid_t moved_uid = null_entity_uid;    // the entity now living in the freed slot
};

// One pool per entity type: a flat byte buffer addressed by stride.
//
// NOT a template, deliberately. It used to be `Entity_Pool<T>` behind a virtual
// `Entity_Pool_Base`, and that whole apparatus existed to answer one question —
// "runtime entity_type to compile-time T" — which the generated table now
// answers as data. `T` was used for exactly four things: sizeof, default
// construction, copy-assignment, and entity_as<T>'s tag compare. The first three
// are columns in entity_type_info_t (size_in_bytes, construct_at, and the
// triviality the static_asserts guarantee); the fourth is a compare against
// Entity::type, which every entity carries. So the type is a FIELD here rather
// than a parameter, and the hand-written switch that existed to supply the
// parameter (make_entity_pool) is gone with it. See entity_system_def.md §1.
//
// Two invariants this rests on (entity_system_def.md §4), both machine-checked:
//   * entities are trivially copyable and trivially destructible, so push_copy
//     and remove_at are memcpy and this buffer never runs a destructor. The
//     generator emits a static_assert per entity for both; the day a field
//     arrives that breaks it, the build stops rather than the pool leaking.
//   * reaching Entity* from std::byte* does NOT assume Entity sits at offset 0.
//     Entity has data members and so does every derived type, so they are not
//     pointer-interconvertible. entity_info(type).as_base is a generated thunk
//     emitted where the concrete type is complete, so the compiler applies
//     whatever adjustment the ABI wants. entity_layout_test checks it at
//     runtime, since no static_assert can express it.
//
// P7 decision (entity_storage_def.md §3): the pools stay DENSE. Removal is
// swap-and-pop, not a tombstone, so iteration has no holes to skip. Stable
// addressing is provided by the uid index on Entity_System instead of by stable
// slots — a monotonic uid is never reused, so a stale uid resolves to nothing
// rather than to a different entity, which is the guarantee a generation counter
// would have bought.
//
// A pointer into `storage` is still invalidated by the next spawn or removal in
// the SAME pool. That is deliberate and is the whole rule: resolve a uid at the
// point of use, never store the pointer across a call that can spawn or destroy.
struct Entity_Pool
{
  entities::entity_type  type   = entities::entity_type::Invalid;
  uint32_t               stride = 0;   // entity_info(type).size_in_bytes, cached
  uint32_t               count  = 0;
  std::vector<std::byte> storage;

  entities::Entity *at(uint32_t slot)
  {
    assert(slot < count && "Entity_Pool::at on a slot the pool does not hold");
    return entities::entity_info(type).as_base(storage.data() + (size_t)slot * stride);
  }

  const entities::Entity *at(uint32_t slot) const
  {
    return const_cast<Entity_Pool *>(this)->at(slot);
  }

  // Default-constructs one entity at the end and hands it back. The vector grows
  // geometrically, so this is amortized O(1) despite resizing by one stride.
  entities::Entity *push_default()
  {
    storage.resize((size_t)(count + 1) * stride);
    ++count;
    return entities::entity_info(type).construct_at(storage.data() + (size_t)(count - 1) * stride);
  }

  // Copies `source` in as the new last element and hands back the COPY. The uid
  // is not touched here — Entity_System stamps it, which is what makes map load
  // non-mutating (P7 step 1): this used to be written through the map's own
  // shared_ptr before the copy was taken, so initializing a session silently
  // renumbered the map the editor was holding.
  //
  // A type mismatch asserts rather than returning a sentinel. It is not a data
  // error a caller could recover from: Entity_System selects the pool BY
  // source->type, so a mismatch here means the table and the tag disagree.
  entities::Entity *push_copy(const entities::Entity *source)
  {
    assert(source != nullptr && "Entity_Pool::push_copy on a null entity");
    assert(source->type == type && "Entity_Pool::push_copy into the pool for a different type");

    storage.resize((size_t)(count + 1) * stride);
    ++count;

    std::byte *destination = storage.data() + (size_t)(count - 1) * stride;
    std::memcpy(destination, source, stride);
    return entities::entity_info(type).as_base(destination);
  }

  // Swap-and-pop the element at `slot`.
  pool_removal_t remove_at(uint32_t slot)
  {
    if (slot >= count)
    {
      log_error("Entity_Pool::remove_at: slot {} is out of range (the {} pool holds {})", slot,
                entities::entity_info(type).classname, count);
      return {};
    }

    pool_removal_t result;
    result.removed = true;

    const uint32_t last = count - 1;
    if (slot != last)
    {
      std::memcpy(storage.data() + (size_t)slot * stride,
                  storage.data() + (size_t)last * stride, stride);
      result.moved_uid = at(slot)->entity_id;
    }

    --count;
    storage.resize((size_t)count * stride);
    return result;
  }

  void reset()
  {
    count = 0;
    storage.clear();   // keeps the capacity, so a session reload reuses the allocation
  }
};

// Where the entity with a given uid lives right now. Slots move (swap-and-pop),
// so this is Entity_System's to maintain and nobody else's to cache.
struct entity_location_t
{
  entities::entity_type type = entities::entity_type::Invalid;
  uint32_t              slot = 0;
};

template <typename... Component_T> struct Component_View;

struct Entity_System
{
  // Sizes every pool from the generated table. There is no dispatch inside and
  // nothing to register: an array of values cannot be missing an entry, so "no
  // pool for type X" is not a state this code can be in.
  Entity_System();

  // Indexed by (uint32_t)entity_type. Index 0 is Invalid and stays empty.
  std::array<Entity_Pool, entities::ENTITY_TYPE_COUNT> pools;

  // uid -> where it lives. This is the P7 handle mechanism: `entity_uid_t` is
  // the handle the rest of the codebase already passes around (Rocket_Entity::
  // owner_id, physics_state_t's body maps, hit_result_t, snapshot_frame_t's
  // keys), and this table is what finally makes it resolvable in one step
  // instead of a linear scan per system.
  std::unordered_map<entity_uid_t, entity_location_t> locations;

  // Entity ID generation (simple incrementing counter)
  entity_uid_t next_entity_id = 1; // Start at 1 (0 is reserved for null)

  // The tag is no longer a parameter anywhere below: the generator puts
  // T::static_type on every entity struct, so passing it alongside T was a
  // second spelling of the same fact that could disagree with the first.

  // Every live entity of type T, in slot order. Empty rather than null when
  // there are none.
  //
  // A VIEW, not the container. This used to hand back the `std::vector<T>*`
  // itself, which let any caller `push_back` behind `locations`' back — the uid
  // index would then name slots holding something other than what it said, which
  // is the one P7 invariant no compiler checks. Spawning goes through spawn<T>()
  // and removal through destroy(uid); there is no third door now.
  //
  // Hiding the container is what made the storage swap a one-file change: a Span
  // over `std::vector<T>` and a Span over a strided byte buffer are the same
  // value to every caller here.
  //
  // Invalidated by the next spawn or destroy in T's pool — the same rule get<T>()
  // states, and for the same reason. Do not store it across one.
  //
  // The one caveat, stated so it is a decision rather than an accident: this
  // reinterprets the buffer as T and strides across elements that do not form an
  // array object. Formally UB, universally fine, and the same bet the codebase
  // already makes when it memcmp-diffs and memcpy-clones entities
  // (entity_system_def.md §4).
  template <typename T> Span<T> entities_of()
  {
    Entity_Pool &pool = pools[(uint32_t)T::static_type];

    // The one cross-file coupling of the whole byte pool, pinned where it is
    // used: the pool addresses elements by entity_info(type).size_in_bytes,
    // while a Span<T> walks them by sizeof(T). They agree by construction — the
    // generated column IS `(uint32_t)sizeof(T)` — but if they ever stopped
    // agreeing, element 0 would still read correctly and every element after it
    // would be garbage, which is the worst possible way to find out.
    assert(pool.stride == sizeof(T) &&
           "the generated stride for this entity disagrees with sizeof(T)");

    return Span<T>(reinterpret_cast<T *>(pool.storage.data()), pool.count);
  }

  // Every live entity carrying ALL of Component_T..., across every pool whose
  // type embeds them — in entity_type declaration order, then slot order.
  //
  //   for (auto [entity, render] : entity_system.entities_with<entities::Render>())
  //
  // Replaces `for (pool) for (slot) if (get_render(entity))`, which evaluated a
  // filter that is CONSTANT PER POOL once per entity — every element of the
  // Light_Entity pool paying a COMPONENT_OFFSETS lookup that could only return
  // -1. See Component_View below for what it costs instead.
  //
  // Same lifetime rule as entities_of<T>(): a view, not a container, invalidated
  // by the next spawn or destroy in any pool it covers.
  template <typename... Component_T> Component_View<Component_T...> entities_with();

  // Create an entity of type T and return its uid — the handle. Resolve it with
  // get<T>(uid) to write the new entity's fields. Cannot fail.
  //
  // This used to return the T* it had just constructed (P7 step 4 flipped it).
  // That pointer is invalidated by the very next spawn or destroy in the same
  // pool, so the old signature handed a dangling-in-waiting pointer to every
  // caller as its primary result. The rule the uid establishes: **never store a
  // T* across a call that can spawn or destroy in that pool** — which is
  // checkable by reading one function, where the old rule ("never store one, and
  // also hope") was not.
  template <typename T> entity_uid_t spawn()
  {
    Entity_Pool      &pool   = pools[(uint32_t)T::static_type];
    entities::Entity *entity = pool.push_default();

    entity->entity_id = next_entity_id++;
    locations[entity->entity_id] = {T::static_type, pool.count - 1};
    return entity->entity_id;
  }

  // Resolve a uid to the entity it names. Returns nullptr if no entity has that
  // uid (destroyed, or never existed) or if it has one of a different type —
  // both are ordinary answers, not errors, which is what makes this usable as a
  // liveness test.
  //
  // The returned pointer is valid until the next spawn or destroy touching T's
  // pool. Do not store it.
  template <typename T> T *get(entity_uid_t uid)
  {
    auto location_it = locations.find(uid);
    if (location_it == locations.end())
      return nullptr;

    const entity_location_t &location = location_it->second;
    if (location.type != T::static_type)
      return nullptr;

    Entity_Pool &pool = pools[(uint32_t)location.type];
    if (location.slot >= pool.count)
    {
      // The index disagrees with the pool. Never expected; means a mutation
      // bypassed the bookkeeping, and the next thing to happen would be a read
      // of a slot that isn't there.
      log_error("Entity_System::get: uid {} indexes slot {} of a pool holding {} — index is stale",
                uid, location.slot, pool.count);
      return nullptr;
    }

    // T is known here, so the element is reached as T directly rather than
    // through as_base — same reinterpretation entities_of<T>() makes, minus the
    // thunk call this warm path does not need.
    return reinterpret_cast<T *>(pool.storage.data() + (size_t)location.slot * pool.stride);
  }

  // Destroy whichever entity holds `uid`. False if none does — the caller can
  // treat that as "already gone" rather than an error.
  //
  // This is the ONE destruction funnel, and the reason P7 step 4 deleted the
  // `destroy(T*)` overload rather than keeping both: a pointer form has to
  // pointer-compare its way back to a slot, so "is this even my entity" was a
  // range check on the caller's behalf. A uid answers it by lookup. (It was
  // spelled `destroy_by_uid` while the overload existed; with the pointer form
  // gone there is nothing left to disambiguate from.)
  //
  // This is the STORAGE primitive: it removes the value from its pool and
  // repairs the uid index, and that is all it does. Anything an entity owns
  // outside the pool -- a Jolt body above all -- is torn down by the layer that
  // knows about it. On the server that layer is
  // `server::destroy_entity(context, uid)` (src/server/entity_lifecycle.hpp),
  // and server code calls THAT, not this. Entity_System lives in game_shared and
  // has no business knowing what physics_state_t is; see the header there for
  // why that stayed true instead of becoming an installed callback.
  bool destroy(entity_uid_t uid);

  void reset();
  void populate_from_map(const map_t &map);

  // Copy `entity` into the pool for its type and index it under `uid`. Takes a
  // raw pointer rather than the map's `shared_ptr` on purpose: what this needs
  // is bytes to copy from, and depending on how the caller owns them is what
  // tied session storage to an editor allocation decision (entity_system_def.md
  // §6 — whether map_entity_t keeps its shared_ptr is now free to change here).
  void add_entity(entity_uid_t uid, const entities::Entity *entity);

  // Cross-check `locations` against what the pools actually hold, in both
  // directions. Returns true if they agree; logs every disagreement it finds.
  // Called by session_test and cheap enough to call after a suspicious sequence
  // — this is the guard for the one invariant P7 introduces that the compiler
  // cannot check.
  //
  // It used to also check that a pool EXISTS for every indexed type. That half
  // is gone: `pools` is an array of values, so the question has no answer other
  // than yes.
  bool validate_locations() const;

private:
  // The one place a slot is removed and the index is repaired, so the
  // swap-and-pop fixup exists once.
  void remove_at_slot(entities::entity_type type, uint32_t slot);
};

// The result of Entity_System::entities_with<Component_T...>(). A lazy range:
// nothing is allocated, nothing is cached, and nothing has to be kept in sync at
// spawn or destroy — which is the whole reason this is not a stored aggregate.
//
// "Which entity types have Render" is not a fact anyone declares twice: it comes
// out of `render: Render` in entities.def as entity_type_info_t::component_mask.
// So the outer loop walks TYPES (ENTITY_TYPE_COUNT of them, one mask test each,
// once per query) rather than entities, and the inner loop walks the matching
// pools at their runtime stride. The component byte offsets are resolved ONCE
// PER POOL, in settle(), instead of once per entity.
//
// Intersections come free: required_mask is a fold over the pack, so
// entities_with<Render, Box_Volume>() is the same test against both bits.
template <typename... Component_T> struct Component_View
{
  static_assert(sizeof...(Component_T) > 0, "entities_with<> needs at least one component");

  static constexpr uint32_t REQUIRED_MASK =
      (... | (1u << (uint16_t)Component_T::static_component));

  // A pack cannot be expanded into struct members, so the row is a tuple —
  // which still binds as `auto [entity, render]` at the call site. The members
  // are references INTO THE POOL, so writing through them writes the entity.
  using row_t = std::tuple<entities::Entity &, Component_T &...>;

  Entity_System *system = nullptr;

  struct iterator
  {
    Entity_System *system = nullptr;

    // Indexes `system->pools` directly, so it is the entity_type tag. Starts at
    // 1: index 0 is Invalid, and entity_info() asserts on it.
    uint32_t type_index = 1;
    uint32_t slot       = 0;

    // Resolved once per pool by settle(), in pack order.
    Array<uint32_t, sizeof...(Component_T)> offsets = {};

    // Postcondition: either type_index == ENTITY_TYPE_COUNT (the end), or
    // pools[type_index] embeds every component, holds `slot`, and `offsets` is
    // resolved for it.
    void settle()
    {
      while (type_index < entities::ENTITY_TYPE_COUNT)
      {
        const entities::entity_type type = (entities::entity_type)type_index;

        if ((entities::entity_info(type).component_mask & REQUIRED_MASK) == REQUIRED_MASK &&
            slot < system->pools[type_index].count)
        {
          uint32_t next = 0;
          // Left-to-right by the fold's evaluation order, so `offsets` ends up
          // in pack order and make_row's index_sequence lines up with it.
          ((offsets[next++] = (uint32_t)entities::component_byte_offset(
                type, Component_T::static_component)),
           ...);
          return;
        }

        ++type_index;
        slot = 0;
      }
    }

    iterator &operator++()
    {
      ++slot;
      // Still inside the same pool: the offsets already hold, which is the
      // whole point. Only a pool change goes back through settle().
      if (slot < system->pools[type_index].count)
        return *this;

      ++type_index;
      slot = 0;
      settle();
      return *this;
    }

    row_t operator*() const
    {
      return make_row(system->pools[type_index].at(slot),
                      std::index_sequence_for<Component_T...>{});
    }

    bool operator!=(const iterator &other) const
    {
      return type_index != other.type_index || slot != other.slot;
    }

  private:
    template <size_t... Index>
    row_t make_row(entities::Entity *base, std::index_sequence<Index...>) const
    {
      // `base` comes from Entity_Pool::at, which goes through the generated
      // as_base thunk — an entity and its base are not pointer-interconvertible,
      // so the adjustment is not ours to guess (entity_system.hpp §Entity_Pool).
      return row_t(*base, *reinterpret_cast<Component_T *>(reinterpret_cast<uint8_t *>(base) +
                                                           offsets[Index])...);
    }
  };

  iterator begin() const
  {
    iterator result{system};
    result.settle();
    return result;
  }

  iterator end() const { return iterator{system, entities::ENTITY_TYPE_COUNT, 0}; }
};

template <typename... Component_T> Component_View<Component_T...> Entity_System::entities_with()
{
  return Component_View<Component_T...>{this};
}

} // namespace shared
