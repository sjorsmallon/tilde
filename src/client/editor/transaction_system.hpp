#pragma once

#include "../../shared/entity.hpp"
#include "../../shared/log.hpp"
#include "../../shared/map.hpp"
#include "../../shared/map_geometry.hpp"
#include "../../shared/network/schema.hpp"
#include <cstdint>
#include <map>
#include <stack>
#include <string>
#include <variant>
#include <vector>

namespace client
{

// --- Data types ---

struct property_change_t
{
  std::string field;
  std::string before;
  std::string after;
};

struct entity_snapshot_t
{
  std::string classname;
  std::map<std::string, std::string> properties;
};

struct diff_entity_created_t
{
  shared::entity_uid_t uid;
  entity_snapshot_t snapshot;
};

struct diff_entity_removed_t
{
  shared::entity_uid_t uid;
  entity_snapshot_t snapshot;
};

struct diff_entity_modified_t
{
  shared::entity_uid_t uid;
  std::vector<property_change_t> changes;
};

// --- The second entry flavor: geometry value-swap ---------------------------
//
// Geometry doesn't get diffed field-by-field. It's a plain value, so undo IS a
// whole-value copy — before and after, swap on undo/redo. That is both simpler
// and strictly more correct than the entity flavor above, which compares
// FORMATTED FLOAT TEXT and so silently drops a change too small to show at 6
// decimals. It also needs no schema, no reflection, and no string round-trip.
//
// The cost is memory: a sculpted displacement's snapshot is its whole vertex
// grid. That's the right trade for an editor — the tools already captured
// whole-grid start states by hand for exactly this reason.

struct diff_geometry_created_t
{
  shared::entity_uid_t uid;
  shared::geometry_value_t value;
};

struct diff_geometry_removed_t
{
  shared::entity_uid_t uid;
  shared::geometry_value_t value;
};

struct diff_geometry_modified_t
{
  shared::entity_uid_t uid;
  shared::geometry_value_t before;
  shared::geometry_value_t after;
};

using edit_diff_t =
    std::variant<diff_entity_created_t, diff_entity_removed_t, diff_entity_modified_t,
                 diff_geometry_created_t, diff_geometry_removed_t,
                 diff_geometry_modified_t>;

struct transaction_t
{
  std::vector<edit_diff_t> diffs;
  bool empty() const { return diffs.empty(); }
};

// --- Free helpers ---

template <class... Ts> struct overloaded : Ts...
{
  using Ts::operator()...;
};

inline std::vector<property_change_t>
diff_properties(const std::map<std::string, std::string> &before,
                const std::map<std::string, std::string> &after)
{
  std::vector<property_change_t> changes;
  for (const auto &[key, old_val] : before)
  {
    auto it = after.find(key);
    if (it != after.end() && it->second != old_val)
      changes.push_back({key, old_val, it->second});
  }
  return changes;
}

inline entity_snapshot_t snapshot_entity(const network::Entity *ent)
{
  return {shared::get_classname_for_entity(ent), ent->get_all_properties()};
}

// --- transaction_builder_t ---

struct transaction_builder_t
{
  std::vector<edit_diff_t> diffs;

  void add_created(shared::entity_uid_t uid, entity_snapshot_t snapshot)
  {
    diffs.push_back(diff_entity_created_t{uid, std::move(snapshot)});
  }

  void add_removed(shared::entity_uid_t uid, entity_snapshot_t snapshot)
  {
    diffs.push_back(diff_entity_removed_t{uid, std::move(snapshot)});
  }

  void add_modified(shared::entity_uid_t uid,
                    std::vector<property_change_t> changes)
  {
    if (!changes.empty())
      diffs.push_back(
          diff_entity_modified_t{uid, std::move(changes)});
  }

  void add_modified_from_diff(
      shared::entity_uid_t uid,
      const std::map<std::string, std::string> &before,
      const std::map<std::string, std::string> &after)
  {
    add_modified(uid, diff_properties(before, after));
  }

  // --- geometry ---

  void add_geometry_created(shared::entity_uid_t uid, shared::geometry_value_t value)
  {
    diffs.push_back(diff_geometry_created_t{uid, std::move(value)});
  }

  void add_geometry_removed(shared::entity_uid_t uid, shared::geometry_value_t value)
  {
    diffs.push_back(diff_geometry_removed_t{uid, std::move(value)});
  }

  // No-op if the value didn't actually change, so a click-without-drag doesn't
  // push an empty transaction the user then has to undo twice.
  void add_geometry_modified(shared::entity_uid_t uid, shared::geometry_value_t before,
                             shared::geometry_value_t after)
  {
    if (geometry_values_equal(before, after))
      return;
    diffs.push_back(diff_geometry_modified_t{uid, std::move(before), std::move(after)});
  }

  transaction_t take()
  {
    transaction_t txn;
    txn.diffs = std::move(diffs);
    return txn;
  }
};

// --- Transaction_System ---

class Transaction_System
{
public:
  void push(transaction_t txn)
  {
    if (txn.empty())
      return;
    undo_stack.push(std::move(txn));
    while (!redo_stack.empty())
      redo_stack.pop();
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

private:
  std::stack<transaction_t> undo_stack;
  std::stack<transaction_t> redo_stack;

  void apply_transaction(shared::map_t &map, const transaction_t &t)
  {
    for (const auto &diff : t.diffs)
      apply_diff(map, diff);
  }

  void revert_transaction(shared::map_t &map, const transaction_t &t)
  {
    for (auto it = t.diffs.rbegin(); it != t.diffs.rend(); ++it)
      revert_diff(map, *it);
  }

  void apply_diff(shared::map_t &map, const edit_diff_t &diff)
  {
    std::visit(
        overloaded{
            [&](const diff_entity_created_t &d)
            {
              auto ent =
                  shared::create_entity_by_classname(d.snapshot.classname);
              if (ent)
              {
                ent->init_from_map(d.snapshot.properties);
                map.add_entity_with_uid(d.uid, ent);
              }
            },
            [&](const diff_entity_removed_t &d)
            { map.remove_entity(d.uid); },
            [&](const diff_entity_modified_t &d)
            {
              auto *entry = map.find_by_uid(d.uid);
              if (entry && entry->entity)
              {
                std::map<std::string, std::string> props;
                for (const auto &c : d.changes)
                  props[c.field] = c.after;
                entry->entity->init_from_map(props);
              }
            },
            [&](const diff_geometry_created_t &d)
            { map.add_geometry_with_uid(d.uid, d.value); },
            [&](const diff_geometry_removed_t &d)
            { map.remove_geometry(d.uid); },
            [&](const diff_geometry_modified_t &d)
            { set_geometry_value(map, d.uid, d.after); }},
        diff);
  }

  void revert_diff(shared::map_t &map, const edit_diff_t &diff)
  {
    std::visit(
        overloaded{
            [&](const diff_entity_created_t &d)
            { map.remove_entity(d.uid); },
            [&](const diff_entity_removed_t &d)
            {
              auto ent =
                  shared::create_entity_by_classname(d.snapshot.classname);
              if (ent)
              {
                ent->init_from_map(d.snapshot.properties);
                map.add_entity_with_uid(d.uid, ent);
              }
            },
            [&](const diff_entity_modified_t &d)
            {
              auto *entry = map.find_by_uid(d.uid);
              if (entry && entry->entity)
              {
                std::map<std::string, std::string> props;
                for (const auto &c : d.changes)
                  props[c.field] = c.before;
                entry->entity->init_from_map(props);
              }
            },
            [&](const diff_geometry_created_t &d)
            { map.remove_geometry(d.uid); },
            [&](const diff_geometry_removed_t &d)
            { map.add_geometry_with_uid(d.uid, d.value); },
            [&](const diff_geometry_modified_t &d)
            { set_geometry_value(map, d.uid, d.before); }},
        diff);
  }

  // Write a whole geometry value back over the object with this uid. The entire
  // apply/revert asymmetry of the entity flavor collapses into this one line.
  static void set_geometry_value(shared::map_t &map, shared::entity_uid_t uid,
                                 const shared::geometry_value_t &value)
  {
    shared::map_geometry_t *entry = map.find_geometry_by_uid(uid);
    if (!entry)
    {
      log_error("transaction: geometry uid {} is gone — cannot restore its value",
                uid);
      return;
    }
    entry->value = value;
  }
};

} // namespace client
