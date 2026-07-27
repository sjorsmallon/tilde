#pragma once

#include "entities/entity_reflection.hpp"
#include "log.hpp"
#include "map.hpp"
#include <map>
#include <memory>
#include <vector>

namespace shared
{

// One pool per entity type, each a flat vector of concrete values.
//
// UNCHANGED BY THE CUTOVER, deliberately: spawn() still hands back a raw T*
// into a vector that reallocates, so an outstanding pointer is still
// invalidated by the next spawn. That is P7's problem (see the storage refactor
// in todo.md), and bundling a storage change with a reflection change is how
// you end up unable to tell which half broke the game.
//
// What DID change is how a pool is created for a runtime tag: it was a template
// instantiated per X-macro entry, and is now one switch over the closed enum
// (make_entity_pool, in the .cpp). A forgotten type is a -Wswitch warning there.
struct Entity_Pool_Base
{
  virtual ~Entity_Pool_Base() = default;
  virtual void reset() = 0;
  virtual void add_existing(const entities::Entity *entity) = 0;

  // How many entities this pool holds. On the base so a caller that only has a
  // tag (the entity debug overlay) can ask without knowing T -- which is what
  // the per-type X-macro expansion it replaced was doing the long way.
  virtual size_t size() const = 0;
};

template <typename T> struct Entity_Pool : Entity_Pool_Base
{
  std::vector<T> entities;

  void reset() override { entities.clear(); }

  size_t size() const override { return entities.size(); }

  void add_existing(const entities::Entity *entity) override
  {
    const T *typed = entities::entity_as<T>(entity);
    if (typed == nullptr)
    {
      log_error("Entity_Pool: a {} was handed to the pool for a different type — not added",
                entities::classname_of(entity));
      return;
    }
    entities.push_back(*typed);
  }

  void remove(T *pointer)
  {
    for (size_t index = 0; index < entities.size(); ++index)
    {
      if (&entities[index] != pointer)
        continue;

      if (index != entities.size() - 1)
        entities[index] = std::move(entities.back());
      entities.pop_back();
      return;
    }
  }
};

// Creates the pool that holds `type`. Exhaustive over entity_type, so adding an
// entity to entities.def and forgetting this is a compile-time warning rather
// than a runtime "has no matching pool" at map load.
std::unique_ptr<Entity_Pool_Base> make_entity_pool(entities::entity_type type);

//@NOTE(SJM): while I am mostly opposed to constructors and not default
// parameters,
// we need to be sure to register all entity types before we can use them, and
// it does not make sense to live in a world where you can forget that.
struct Entity_System
{
  Entity_System() { register_all_known_entity_types(); }

  std::map<entities::entity_type, std::unique_ptr<Entity_Pool_Base>> pools;

  // Entity ID generation (simple incrementing counter)
  entity_uid_t next_entity_id = 1; // Start at 1 (0 is reserved for null)

  // The tag is no longer a parameter anywhere below: the generator puts
  // T::static_type on every entity struct, so passing it alongside T was a
  // second spelling of the same fact that could disagree with the first.
  template <typename T> std::vector<T> *get_entities()
  {
    auto it = pools.find(T::static_type);
    if (it == pools.end())
      return nullptr;
    return &static_cast<Entity_Pool<T> *>(it->second.get())->entities;
  }

  //@FIXME(SJM): I don't think this should actually return T* ? and that delete
  // should not actually delete but just free up a slot. (P7.)
  template <typename T> T *spawn()
  {
    auto it = pools.find(T::static_type);
    if (it == pools.end())
      return nullptr;

    auto *pool = static_cast<Entity_Pool<T> *>(it->second.get());
    pool->entities.emplace_back();

    T *entity = &pool->entities.back();
    entity->entity_id = next_entity_id++;
    return entity;
  }

  template <typename T> void destroy(T *pointer)
  {
    auto it = pools.find(T::static_type);
    if (it == pools.end())
      return;
    static_cast<Entity_Pool<T> *>(it->second.get())->remove(pointer);
  }

  void reset();
  void populate_from_map(const map_t &map);
  void add_entity(entity_uid_t uid, const std::shared_ptr<entities::Entity> &entity);

  // this is called in the constructor, no need for you to call it.
  void register_all_known_entity_types();
};

} // namespace shared
