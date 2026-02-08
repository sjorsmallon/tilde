#pragma once

#include "../../shared/entity.hpp"
#include "../../shared/map.hpp"
#include "../../shared/network/schema.hpp"
#include "editor_types.hpp"
#include <cstdint>
#include <cstring>
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
  GEOMETRY_ADD,
  GEOMETRY_REMOVE,
  GEOMETRY_MODIFY
};

struct field_delta_t
{
  uint16_t id;
  std::vector<uint8_t> old_val;
  std::vector<uint8_t> new_val;
};

struct delta_t
{
  transaction_type_t type;
  uint32_t target_index = 0;

  // For ADD/REMOVE/MODIFY (depending on type)
  // For ADD/REMOVE: Snapshot of the object (to restore on undo/redo)
  std::variant<std::monostate, shared::static_geometry_t,
               shared::entity_spawn_t>
      object_snapshot;

  // For MODIFY: Field changes
  std::vector<network::field_change_t> changes;
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
  template <typename T>
  void commit_modification(uint32_t index, const T &old_val, const T &new_val)
  {
    transaction_t t;
    delta_t d;
    d.type = transaction_type_t::GEOMETRY_MODIFY;
    d.target_index = index;

    // Compute reversible field deltas
    const auto *schema = old_val.get_schema();
    if (schema)
    {
      d.changes = network::diff_reversible(&old_val, &new_val, schema);
    }

    if (!d.changes.empty())
    {
      t.add(std::move(d));
      push_transaction(std::move(t));
    }
  }

  void commit_add(uint32_t index, const shared::static_geometry_t &obj)
  {
    transaction_t t;
    delta_t d;
    d.type = transaction_type_t::GEOMETRY_ADD;
    d.target_index = index;
    d.object_snapshot = obj;
    t.add(std::move(d));
    push_transaction(std::move(t));
  }

  void commit_remove(uint32_t index, const shared::static_geometry_t &obj)
  {
    transaction_t t;
    delta_t d;
    d.type = transaction_type_t::GEOMETRY_REMOVE;
    d.target_index = index;
    d.object_snapshot = obj; // Needed for Undo (which adds it back)
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
    // transaction to match stack behavior? Usually transactions are atomic
    // batch, but reverse order is safer.
    for (auto it = t.mutations.rbegin(); it != t.mutations.rend(); ++it)
    {
      revert_delta(map, *it);
    }
  }

  void apply_delta(shared::map_t &map, const delta_t &delta)
  {
    switch (delta.type)
    {
    case transaction_type_t::GEOMETRY_ADD:
    {
      if (std::holds_alternative<shared::static_geometry_t>(
              delta.object_snapshot))
      {
        auto obj = std::get<shared::static_geometry_t>(delta.object_snapshot);
        if (delta.target_index <= map.static_geometry.size())
          map.static_geometry.insert(
              map.static_geometry.begin() + delta.target_index, obj);
        else
          map.static_geometry.push_back(obj);
      }
      break;
    }
    case transaction_type_t::GEOMETRY_REMOVE:
    {
      // Apply REMOVE = erase
      if (delta.target_index < map.static_geometry.size())
      {
        map.static_geometry.erase(map.static_geometry.begin() +
                                  delta.target_index);
      }
      break;
    }
    case transaction_type_t::GEOMETRY_MODIFY:
    {
      if (delta.target_index < map.static_geometry.size())
      {
        auto &obj = map.static_geometry[delta.target_index];
        apply_fields(obj, delta.changes, true); // true = use new_val
      }
      break;
    }
    default:
      break;
    }
  }

  void revert_delta(shared::map_t &map, const delta_t &delta)
  {
    switch (delta.type)
    {
    case transaction_type_t::GEOMETRY_ADD:
    {
      // Revert ADD = REMOVE
      if (delta.target_index < map.static_geometry.size())
      {
        map.static_geometry.erase(map.static_geometry.begin() +
                                  delta.target_index);
      }
      break;
    }
    case transaction_type_t::GEOMETRY_REMOVE:
    {
      // Revert REMOVE = ADD back
      if (std::holds_alternative<shared::static_geometry_t>(
              delta.object_snapshot))
      {
        auto obj = std::get<shared::static_geometry_t>(delta.object_snapshot);
        if (delta.target_index <= map.static_geometry.size())
          map.static_geometry.insert(
              map.static_geometry.begin() + delta.target_index, obj);
        else
          map.static_geometry.push_back(obj);
      }
      break;
    }
    case transaction_type_t::GEOMETRY_MODIFY:
    {
      if (delta.target_index < map.static_geometry.size())
      {
        auto &obj = map.static_geometry[delta.target_index];
        apply_fields(obj, delta.changes, false); // false = use old_val
      }
      break;
    }
    default:
      break;
    }
  }

  void apply_fields(shared::static_geometry_t &obj,
                    const std::vector<network::field_change_t> &changes,
                    bool use_new)
  {
    std::visit(
        [&](auto &shape)
        {
          using T = std::decay_t<decltype(shape)>;
          if constexpr (!std::is_same_v<T, shared::mesh_t>)
          {
            const auto *schema = shape.get_schema();
            if (schema)
            {
              uint8_t *target_ptr = reinterpret_cast<uint8_t *>(&shape);
              for (const auto &change : changes)
              {
                for (const auto &field : schema->fields)
                {
                  if (field.index == change.id)
                  {
                    const auto &data =
                        use_new ? change.new_val : change.old_val;
                    if (data.size() == field.size)
                    {
                      std::memcpy(target_ptr + field.offset, data.data(),
                                  field.size);
                    }
                    break;
                  }
                }
              }
            }
          }
        },
        obj.data);
  }
};

// RAII Transaction Wrapper
class Editor_Transaction
{
public:
  // For modifying existing geometry
  Editor_Transaction(Transaction_System *sys, shared::map_t *map,
                     uint32_t geometry_index)
      : system(sys), map(map), type(transaction_type_t::GEOMETRY_MODIFY),
        target_index(geometry_index)
  {
    if (target_index < map->static_geometry.size())
    {
      auto &geo = map->static_geometry[target_index];
      before_snapshot = geo;
    }
  }

  // For creating new geometry
  Editor_Transaction(Transaction_System *sys, shared::map_t *map,
                     const std::string &name)
      : system(sys), map(map), type(transaction_type_t::GEOMETRY_ADD),
        transaction_name(name)
  {
    target_index = (uint32_t)map->static_geometry.size();
  }

  ~Editor_Transaction()
  {
    if (!system || !map)
      return;

    if (type == transaction_type_t::GEOMETRY_MODIFY)
    {
      if (target_index < map->static_geometry.size())
      {
        auto &current_geo = map->static_geometry[target_index];
        // Compare inner data
        std::visit(
            [&](auto &&before_shape)
            {
              using T = std::decay_t<decltype(before_shape)>;
              if constexpr (!std::is_same_v<T, shared::mesh_t>)
              {
                if (std::holds_alternative<T>(current_geo.data))
                {
                  const auto &current_shape = std::get<T>(current_geo.data);
                  // Use system helper to commit
                  system->commit_modification(target_index, before_shape,
                                              current_shape);
                  // Note: commit_modification now internally computes diff
                  // logic, simplifying this class
                }
              }
            },
            before_snapshot.data);
      }
    }
    else if (type == transaction_type_t::GEOMETRY_ADD)
    {
      if (map->static_geometry.size() > target_index)
      {
        const auto &new_geo = map->static_geometry[target_index];
        system->commit_add(target_index, new_geo);
      }
    }
  }

private:
  Transaction_System *system;
  shared::map_t *map;
  transaction_type_t type;
  uint32_t target_index;
  std::string transaction_name;
  shared::static_geometry_t before_snapshot;
};

} // namespace client
