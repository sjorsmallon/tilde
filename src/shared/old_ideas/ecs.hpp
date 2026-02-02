#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace ecs {

using Entity = uint64_t;
using ComponentMask = uint64_t;

// --- Component ID Generation ---
// Assigns a unique ID (0..63) to each component type T.
inline size_t get_next_component_id() {
  static std::atomic<size_t> counter{0};
  return counter++;
}

template <typename T> size_t get_component_id() {
  static size_t id = get_next_component_id();
  return id;
}

// --- Base Pool Interface ---
// Type-erased base class for component pools so we can store them in a list.
class BasePool {
public:
  virtual ~BasePool() = default;
  virtual void remove(Entity e) = 0;
};

// --- Component Pool ---
// Stores components of type T contiguously.
// Maps Entity ID -> Index in vector.
template <typename T> class ComponentPool : public BasePool {
public:
  std::vector<T> data;
  std::unordered_map<Entity, size_t> entity_to_index;
  std::unordered_map<size_t, Entity> index_to_entity;

  T &add(Entity e, T component) {
    if (entity_to_index.find(e) != entity_to_index.end()) {
      // Overwrite existing
      data[entity_to_index[e]] = component;
      return data[entity_to_index[e]];
    }

    size_t index = data.size();
    data.push_back(component);
    entity_to_index[e] = index;
    index_to_entity[index] = e;
    return data.back();
  }

  template <typename... Args> T &emplace(Entity e, Args &&...args) {
    if (entity_to_index.find(e) != entity_to_index.end()) {
      // Overwrite existing with new construction
      size_t index = entity_to_index[e];
      data[index] = T(std::forward<Args>(args)...); // Assignment
      return data[index];
    }

    size_t index = data.size();
    data.emplace_back(std::forward<Args>(args)...);
    entity_to_index[e] = index;
    index_to_entity[index] = e;
    return data.back();
  }

  void remove(Entity e) override {
    if (entity_to_index.find(e) == entity_to_index.end()) {
      return;
    }

    size_t index_to_remove = entity_to_index[e];
    size_t last_index = data.size() - 1;
    Entity last_entity = index_to_entity[last_index];

    // Swap with last element to keep vector packed
    if (index_to_remove != last_index) {
      data[index_to_remove] = data[last_index];

      // Update maps
      entity_to_index[last_entity] = index_to_remove;
      index_to_entity[index_to_remove] = last_entity;
    }

    // Pop back
    data.pop_back();
    entity_to_index.erase(e);
    index_to_entity.erase(last_index);
  }

  T &get(Entity e) {
    assert(entity_to_index.find(e) != entity_to_index.end());
    return data[entity_to_index[e]];
  }

  bool has(Entity e) {
    return entity_to_index.find(e) != entity_to_index.end();
  }
};

// --- Registry / EntityManager ---
class Registry {
public:
  Entity create_entity() {
    static std::atomic<Entity> entity_counter{
        1}; // 0 reserved? or just start at 1
    Entity e = entity_counter++;
    entity_masks[e] = 0;
    return e;
  }

  template <typename T> T &add_component(Entity e, T component) {
    size_t component_id = get_component_id<T>();
    assert(component_id < 64 && "Max component types exceeded (64)");

    // Get or Create Pool
    auto pool_it = pools.find(std::type_index(typeid(T)));
    if (pool_it == pools.end()) {
      pools[std::type_index(typeid(T))] = std::make_unique<ComponentPool<T>>();
    }

    auto *pool = static_cast<ComponentPool<T> *>(
        pools[std::type_index(typeid(T))].get());
    T &comp = pool->add(e, component);

    // Update bitmask
    entity_masks[e] |= (1ULL << component_id);

    return comp;
  }

  // Overload for in-place construction
  template <typename T, typename... Args>
  T &add_component(Entity e, Args &&...args) {
    size_t component_id = get_component_id<T>();
    assert(component_id < 64 && "Max component types exceeded (64)");

    // Get or Create Pool
    auto pool_it = pools.find(std::type_index(typeid(T)));
    if (pool_it == pools.end()) {
      pools[std::type_index(typeid(T))] = std::make_unique<ComponentPool<T>>();
    }

    auto *pool = static_cast<ComponentPool<T> *>(
        pools[std::type_index(typeid(T))].get());
    T &comp = pool->emplace(e, std::forward<Args>(args)...);

    // Update Bitmask
    entity_masks[e] |= (1ULL << component_id);

    return comp;
  }

  template <typename T> void remove_component(Entity e) {
    size_t component_id = get_component_id<T>();
    auto pool_it = pools.find(std::type_index(typeid(T)));
    if (pool_it != pools.end()) {
      pool_it->second->remove(e);
    }

    entity_masks[e] &= ~(1ULL << component_id);
  }

  template <typename T> T &get_component(Entity e) {
    auto pool_it = pools.find(std::type_index(typeid(T)));
    assert(pool_it != pools.end());
    auto *pool = static_cast<ComponentPool<T> *>(pool_it->second.get());
    return pool->get(e);
  }

  template <typename T> bool has_component(Entity e) {
    size_t component_id = get_component_id<T>();
    if (entity_masks.find(e) == entity_masks.end())
      return false;
    return (entity_masks[e] & (1ULL << component_id)) != 0;
  }

  // Helper to get raw pool reference - useful for iterating
  template <typename T> ComponentPool<T> &get_pool() {
    auto pool_it = pools.find(std::type_index(typeid(T)));
    if (pool_it == pools.end()) {
      pools[std::type_index(typeid(T))] = std::make_unique<ComponentPool<T>>();
    }
    return *static_cast<ComponentPool<T> *>(
        pools[std::type_index(typeid(T))].get());
  }

private:
  std::unordered_map<Entity, ComponentMask> entity_masks;
  std::unordered_map<std::type_index, std::unique_ptr<BasePool>> pools;
};

} // namespace ecs
