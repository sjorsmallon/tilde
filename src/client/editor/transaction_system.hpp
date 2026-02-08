#pragma once

#include "../../shared/entity.hpp"
#include "../../shared/map.hpp"
#include "../../shared/network/schema.hpp"
#include "editor_types.hpp"
#include <stack>
#include <variant>
#include <vector>

namespace client
{

enum class Transaction_Type
{
  ENTITY_ADD,
  ENTITY_REMOVE,
  ENTITY_MODIFY,
  GEOMETRY_ADD,
  GEOMETRY_REMOVE,
  GEOMETRY_MODIFY
};

struct Delta
{
  Transaction_Type type;

  // For Geometry: index in map.static_geometry
  // For Entity: entity_id (though we need to map it to index or handle via ID)
  // Let's use index for simplicity in this first pass, as urged by user.
  uint32_t target_index = 0;

  // For ADD/REMOVE/MODIFY
  // We can store the full object states for ADD/REMOVE to be safe
  // For MODIFY, we use field updates.

  // Holding copy of data for ADD/REMOVE (and rollback)
  std::variant<std::monostate, shared::static_geometry_t,
               shared::entity_spawn_t>
      object_snapshot;

  // For MODIFY
  std::vector<network::Field_Update> changes;
};

class Transaction
{
public:
  std::vector<Delta> deltas;

  void add_delta(Delta d) { deltas.push_back(std::move(d)); }

  bool is_empty() const { return deltas.empty(); }
};

class Transaction_System
{
public:
  // Commits a transaction that represents the FORWARD change (A -> B)
  // We need to simultaneously compute the BACKWARD change (B -> A) to push onto
  // the Undo Stack. HOWEVER, `diff` only gives us the change. It is easier if
  // the caller provides BOTH or we compute it. Caller usage:
  //   auto old_snapshot = capture();
  //   modify();
  //   auto new_snap = capture();
  //   ts.commit(old_snap, new_snap);

  // Let's implement that helper.

  // Generic commit for single object modification
  template <typename T>
  void commit_modification(uint32_t index, const T &old_val, const T &new_val)
  {
    Transaction undo_t;
    Delta undo_d;
    undo_d.type = Transaction_Type::GEOMETRY_MODIFY;
    undo_d.target_index = index;
    // Undo: New -> Old
    undo_d.changes = network::diff(&new_val, &old_val, old_val.get_schema());
    undo_t.add_delta(undo_d);

    Transaction redo_t;
    Delta redo_d;
    redo_d.type = Transaction_Type::GEOMETRY_MODIFY;
    redo_d.target_index = index;
    // Redo: Old -> New
    redo_d.changes = network::diff(&old_val, &new_val, old_val.get_schema());
    redo_t.add_delta(redo_d);

    push_transaction(std::move(undo_t), std::move(redo_t));
  }

  void commit_add(uint32_t index, const shared::static_geometry_t &obj)
  {
    Transaction undo_t;
    Delta undo_d;
    undo_d.type = Transaction_Type::GEOMETRY_REMOVE;
    undo_d.target_index = index;
    undo_d.object_snapshot = obj; // Need obj to restore it if we Undo the
                                  // Remove (wait, this is Undo ADD)
    // Undo ADD = REMOVE.
    // When we Redo (ADD), we need the object.
    // When we Undo (REMOVE), we don't strictly need the object if we just erase
    // index. BUT `apply` needs to be able to generate the reverse.
    undo_t.add_delta(undo_d);

    Transaction redo_t;
    Delta redo_d;
    redo_d.type = Transaction_Type::GEOMETRY_ADD;
    redo_d.target_index = index;
    redo_d.object_snapshot = obj;
    redo_t.add_delta(redo_d);

    push_transaction(std::move(undo_t), std::move(redo_t));
  }

  void commit_remove(uint32_t index, const shared::static_geometry_t &obj)
  {
    // Undo REMOVE = ADD back
    Transaction undo_t;
    Delta undo_d;
    undo_d.type = Transaction_Type::GEOMETRY_ADD;
    undo_d.target_index = index;
    undo_d.object_snapshot = obj;
    undo_t.add_delta(undo_d);

    // Redo REMOVE = REMOVE again
    Transaction redo_t;
    Delta redo_d;
    redo_d.type = Transaction_Type::GEOMETRY_REMOVE;
    redo_d.target_index = index;
    // Snapshot needed? Not for execution, but for consistency if we want
    // strictly symmetric But erase by index is fine.
    redo_t.add_delta(redo_d);

    push_transaction(std::move(undo_t), std::move(redo_t));
  }

  void undo(shared::map_t &map)
  {
    if (undo_stack.empty())
      return;

    auto pair = std::move(undo_stack.top());
    undo_stack.pop();

    apply_transaction(map, pair.undo_ops);

    redo_stack.push(std::move(pair));
  }

  void redo(shared::map_t &map)
  {
    if (redo_stack.empty())
      return;

    auto pair = std::move(redo_stack.top());
    redo_stack.pop();

    apply_transaction(map, pair.redo_ops);

    undo_stack.push(std::move(pair));
  }

  bool can_undo() const { return !undo_stack.empty(); }
  bool can_redo() const { return !redo_stack.empty(); }

private:
  struct Transaction_Pair
  {
    Transaction undo_ops;
    Transaction redo_ops;
  };

  std::stack<Transaction_Pair> undo_stack;
  std::stack<Transaction_Pair> redo_stack;

  friend class Editor_Transaction;

  // Internal commit helpers - now private or used by Editor_Transaction
  void push_transaction(Transaction undo_t, Transaction redo_t)
  {
    undo_stack.push({std::move(undo_t), std::move(redo_t)});
    while (!redo_stack.empty())
      redo_stack.pop();
  }

  void apply_transaction(shared::map_t &map, const Transaction &t)
  {
    for (const auto &delta : t.deltas)
    {
      apply_delta(map, delta);
    }
  }

  void apply_delta(shared::map_t &map, const Delta &delta)
  {
    switch (delta.type)
    {
    case Transaction_Type::GEOMETRY_ADD:
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
    case Transaction_Type::GEOMETRY_REMOVE:
    {
      if (delta.target_index < map.static_geometry.size())
      {
        map.static_geometry.erase(map.static_geometry.begin() +
                                  delta.target_index);
      }
      break;
    }
    case Transaction_Type::GEOMETRY_MODIFY:
    {
      if (delta.target_index < map.static_geometry.size())
      {
        auto &obj = map.static_geometry[delta.target_index];
        std::visit(
            [&](auto &shape)
            {
              using T = std::decay_t<decltype(shape)>;
              if constexpr (!std::is_same_v<T, shared::mesh_t>)
              {
                const auto *schema = shape.get_schema();
                if (schema)
                {
                  network::apply_diff(&shape, delta.changes, schema);
                }
              }
            },
            obj.data);
      }
      break;
    }
    default:
      break;
    }
  }
};

// RAII Transaction Wrapper
class Editor_Transaction
{
public:
  // For modifying existing geometry
  Editor_Transaction(Transaction_System *sys, shared::map_t *map,
                     uint32_t geometry_index)
      : system(sys), map(map), type(Transaction_Type::GEOMETRY_MODIFY),
        target_index(geometry_index)
  {
    if (target_index < map->static_geometry.size())
    {
      // Snapshot BEFORE state
      auto &geo = map->static_geometry[target_index];
      before_snapshot = geo;
    }
  }

  // For creating new geometry (or entity)
  Editor_Transaction(Transaction_System *sys, shared::map_t *map,
                     const std::string &name)
      : system(sys), map(map), type(Transaction_Type::GEOMETRY_ADD),
        transaction_name(name)
  {
    // Snapshot size to identify new ID
    target_index = (uint32_t)map->static_geometry.size();
  }

  // Destructor commits the transaction
  ~Editor_Transaction()
  {
    if (!system || !map)
      return;

    if (type == Transaction_Type::GEOMETRY_MODIFY)
    {
      if (target_index < map->static_geometry.size())
      {
        auto &current_geo = map->static_geometry[target_index];
        // We have before_snapshot (static_geometry_t) and current_geo.
        // We need to diff the INNER data using visit?
        // Actually, static_geometry_t wraps a variant.
        // If types match, we diff properties. If types changed? (Unlikely for
        // modification, usually separate op).

        // For now assume same type.
        std::visit(
            [&](auto &&before_shape)
            {
              using T = std::decay_t<decltype(before_shape)>;
              if constexpr (!std::is_same_v<T, shared::mesh_t>)
              {
                // Assuming current is same type
                if (std::holds_alternative<T>(current_geo.data))
                {
                  const auto &current_shape = std::get<T>(current_geo.data);

                  // Use Transaction_System helper or implement logic here?
                  // Let's implement diff logic here or call a helper.
                  // The commit_modification logic from before:

                  Transaction undo_t;
                  Delta undo_d;
                  undo_d.type = Transaction_Type::GEOMETRY_MODIFY;
                  undo_d.target_index = target_index;
                  const auto *schema = before_shape.get_schema();
                  if (!schema)
                    return;

                  undo_d.changes =
                      network::diff(&current_shape, &before_shape, schema);

                  // Optimization: if no changes, don't push?
                  if (undo_d.changes.empty())
                    return;

                  undo_t.add_delta(std::move(undo_d));

                  Transaction redo_t;
                  Delta redo_d;
                  redo_d.type = Transaction_Type::GEOMETRY_MODIFY;
                  redo_d.target_index = target_index;
                  redo_d.changes =
                      network::diff(&before_shape, &current_shape, schema);
                  redo_t.add_delta(std::move(redo_d));

                  system->push_transaction(std::move(undo_t),
                                           std::move(redo_t));
                }
              }
            },
            before_snapshot.data);
      }
    }
    else if (type == Transaction_Type::GEOMETRY_ADD)
    {
      // Check if item was added at target_index
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
  Transaction_Type type;
  uint32_t target_index;
  std::string transaction_name;
  shared::static_geometry_t before_snapshot;
};

} // namespace client
