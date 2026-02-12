#pragma once

#include "../../shared/entity.hpp"
#include "../../shared/map.hpp"
#include "../../shared/network/schema.hpp"
#include "editor_types.hpp"
#include <cstdint>
#include <cstring>
#include <map>
#include <stack>
#include <string>
#include <variant>
#include <vector>

namespace client
{

enum class transaction_type_t
{
  ENTITY_ADD,
  ENTITY_REMOVE,
  ENTITY_MODIFY,
  // Geometry types removed/merged into ENTITY types
};

struct Entity_Snapshot
{
  std::string classname;
  std::map<std::string, std::string> properties;
};

struct Property_Change
{
  std::string name;
  std::string old_value;
  std::string new_value;
};

struct delta_t
{
  transaction_type_t type;
  uint32_t target_index = 0;

  // For ADD/REMOVE: Snapshot of the entity
  Entity_Snapshot snapshot;

  // For MODIFY: Property changes
  std::vector<Property_Change> changes;
};

struct transaction_t
{
  std::vector<delta_t> mutations;

  void add(delta_t d) { mutations.push_back(std::move(d)); }

  bool is_empty() const { return mutations.empty(); }
};

class Transaction_System
{
public:
  // Commit modification of an existing entity
  void commit_modification(uint32_t index, const network::Entity *entity,
                           const std::map<std::string, std::string> &old_props)
  {
    transaction_t t;
    delta_t d;
    d.type = transaction_type_t::ENTITY_MODIFY;
    d.target_index = index;

    std::map<std::string, std::string> new_props = entity->get_all_properties();

    // Compute diff
    for (const auto &[key, old_val] : old_props)
    {
      auto it = new_props.find(key);
      if (it != new_props.end())
      {
        if (it->second != old_val)
        {
          d.changes.push_back({key, old_val, it->second});
        }
      }
    }
    // Check for new properties (unlikely with fixed schema but possible if we
    // had dynamic props) Our schema is fixed so checking old_props keys is
    // sufficient if both are from get_all_properties().

    if (!d.changes.empty())
    {
      t.add(std::move(d));
      push_transaction(std::move(t));
    }
  }

  void commit_add(uint32_t index, const network::Entity *entity,
                  const std::string &classname)
  {
    transaction_t t;
    delta_t d;
    d.type = transaction_type_t::ENTITY_ADD;
    d.target_index = index;
    d.snapshot.classname = classname;
    d.snapshot.properties = entity->get_all_properties();

    t.add(std::move(d));
    push_transaction(std::move(t));
  }

  void commit_remove(uint32_t index, const network::Entity *entity,
                     const std::string &classname)
  {
    transaction_t t;
    delta_t d;
    d.type = transaction_type_t::ENTITY_REMOVE;
    d.target_index = index;
    d.snapshot.classname = classname;
    d.snapshot.properties = entity->get_all_properties();

    t.add(std::move(d));
    push_transaction(std::move(t));
  }

  void undo(shared::map_t &map)
  {
    if (undo_stack.empty())
      return;

    auto t = std::move(undo_stack.top());
    undo_stack.pop();

    revert_transaction(map, t);

    redo_stack.push(std::move(t));
  }

  void redo(shared::map_t &map)
  {
    if (redo_stack.empty())
      return;

    auto t = std::move(redo_stack.top());
    redo_stack.pop();

    apply_transaction(map, t);

    undo_stack.push(std::move(t));
  }

  bool can_undo() const { return !undo_stack.empty(); }
  bool can_redo() const { return !redo_stack.empty(); }

  // Helper to commit a constructed transaction
  void push_transaction(transaction_t t)
  {
    undo_stack.push(std::move(t));
    while (!redo_stack.empty())
    {
      redo_stack.pop();
    }
  }

private:
  std::stack<transaction_t> undo_stack;
  std::stack<transaction_t> redo_stack;

  friend class Editor_Transaction;

  void apply_transaction(shared::map_t &map, const transaction_t &t)
  {
    for (const auto &delta : t.mutations)
    {
      apply_delta(map, delta);
    }
  }

  void revert_transaction(shared::map_t &map, const transaction_t &t)
  {
    // Revert needs to happen in reverse order of modifications within
    // transaction
    for (auto it = t.mutations.rbegin(); it != t.mutations.rend(); ++it)
    {
      revert_delta(map, *it);
    }
  }

  void apply_delta(shared::map_t &map, const delta_t &delta)
  {
    switch (delta.type)
    {
    case transaction_type_t::ENTITY_ADD:
    {
      // Create new entity
      auto new_ent =
          shared::create_entity_by_classname(delta.snapshot.classname);
      if (new_ent)
      {
        new_ent->init_from_map(delta.snapshot.properties);

        shared::entity_placement_t placement;
        placement.entity = new_ent;
        placement.position = new_ent->position;
        placement.scale = {1, 1, 1};
        placement.rotation = {0, 0, 0};

        if (delta.target_index <= map.entities.size())
          map.entities.insert(map.entities.begin() + delta.target_index,
                              placement);
        else
          map.entities.push_back(placement);
      }
      break;
    }
    case transaction_type_t::ENTITY_REMOVE:
    {
      if (delta.target_index < map.entities.size())
      {
        map.entities.erase(map.entities.begin() + delta.target_index);
      }
      break;
    }
    case transaction_type_t::ENTITY_MODIFY:
    {
      if (delta.target_index < map.entities.size())
      {
        auto &placement = map.entities[delta.target_index];
        if (placement.entity)
        {
          std::map<std::string, std::string> props;
          for (const auto &change : delta.changes)
          {
            props[change.name] = change.new_value;
          }
          placement.entity->init_from_map(props);
        }
      }
      break;
    }
    }
  }

  void revert_delta(shared::map_t &map, const delta_t &delta)
  {
    switch (delta.type)
    {
    case transaction_type_t::ENTITY_ADD:
    {
      // Revert ADD = REMOVE
      if (delta.target_index < map.entities.size())
      {
        map.entities.erase(map.entities.begin() + delta.target_index);
      }
      break;
    }
    case transaction_type_t::ENTITY_REMOVE:
    {
      // Revert REMOVE = ADD back
      auto new_ent =
          shared::create_entity_by_classname(delta.snapshot.classname);
      if (new_ent)
      {
        new_ent->init_from_map(delta.snapshot.properties);

        shared::entity_placement_t placement;
        placement.entity = new_ent;
        placement.position = new_ent->position;
        placement.scale = {1, 1, 1};
        placement.rotation = {0, 0, 0};

        if (delta.target_index <= map.entities.size())
          map.entities.insert(map.entities.begin() + delta.target_index,
                              placement);
        else
          map.entities.push_back(placement);
      }
      break;
    }
    case transaction_type_t::ENTITY_MODIFY:
    {
      if (delta.target_index < map.entities.size())
      {
        auto &placement = map.entities[delta.target_index];
        if (placement.entity)
        {
          std::map<std::string, std::string> props;
          for (const auto &change : delta.changes)
          {
            props[change.name] = change.old_value;
          }
          placement.entity->init_from_map(props);
        }
      }
      break;
    }
    }
  }
};

// RAII Transaction Wrapper
class Editor_Transaction
{
public:
  // For modifying existing entity
  Editor_Transaction(Transaction_System *sys, shared::map_t *map,
                     uint32_t ent_index)
      : system(sys), map(map), type(transaction_type_t::ENTITY_MODIFY),
        target_index(ent_index)
  {
    if (target_index < map->entities.size())
    {
      auto &placement = map->entities[target_index];
      if (placement.entity)
      {
        before_props = placement.entity->get_all_properties();
      }
    }
  }

  // For creating new entity
  Editor_Transaction(Transaction_System *sys, shared::map_t *map,
                     const std::string &name)
      : system(sys), map(map), type(transaction_type_t::ENTITY_ADD),
        transaction_name(name)
  {
    target_index = (uint32_t)map->entities.size();
  }

  ~Editor_Transaction()
  {
    if (!system || !map)
      return;

    if (type == transaction_type_t::ENTITY_MODIFY)
    {
      if (target_index < map->entities.size())
      {
        auto &placement = map->entities[target_index];
        if (placement.entity)
        {
          system->commit_modification(target_index, placement.entity.get(),
                                      before_props);
        }
      }
    }
    else if (type == transaction_type_t::ENTITY_ADD)
    {
      if (map->entities.size() > target_index)
      {
        const auto &placement = map->entities[target_index];
        if (placement.entity)
        {
          std::string classname =
              shared::get_classname_for_entity(placement.entity.get());
          system->commit_add(target_index, placement.entity.get(), classname);
        }
      }
    }
  }

private:
  Transaction_System *system;
  shared::map_t *map;
  transaction_type_t type;
  uint32_t target_index;
  std::string transaction_name;
  std::map<std::string, std::string> before_props;
};

} // namespace client
