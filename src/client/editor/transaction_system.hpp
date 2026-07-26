#pragma once

#include "../../shared/entity.hpp"
#include "../../shared/entity_system.hpp" // type_to_classname, for error messages
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
//
// Both flavors below are BINARY. Nothing here round-trips through text: an
// entity snapshot is an exact clone and an entity modification is a list of
// changed field bytes, exactly as geometry is a whole value. Text appears only
// at the disk save/load boundary, which is map.cpp's business, not undo's.

// One entity's captured state: an exact clone, produced by shared::clone_entity.
// Held by shared_ptr because that's what map_t stores and what the factory
// hands back; the transaction never mutates it, hence const.
using entity_snapshot_t = std::shared_ptr<const network::Entity>;

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
  // Carried so apply/revert can verify the entity still at this uid is the same
  // type the change was captured against. Field indices are per-class, so
  // writing them into a different class would silently corrupt it.
  ::entity_type type = ::entity_type::UNKNOWN;
  std::vector<network::field_change_t> changes;
};

// --- The second entry flavor: geometry value-swap ---------------------------
//
// Geometry doesn't get diffed field-by-field. It's a plain value, so undo IS a
// whole-value copy — before and after, swap on undo/redo. It needs no schema and
// no reflection at all, where the entity flavor needs both to know where the
// fields are.
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

// Capture an entity's whole state for a created/removed entry, or as the
// "before" side of a modification.
inline entity_snapshot_t snapshot_entity(const network::Entity *ent)
{
  return shared::clone_entity(ent);
}

// Rebuild an entity from a snapshot. The snapshot is already a real entity of
// the right concrete type, so this is just another exact clone.
inline std::shared_ptr<network::Entity>
restore_entity(const entity_snapshot_t &snapshot)
{
  return shared::clone_entity(snapshot.get());
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

  void add_modified(shared::entity_uid_t uid, ::entity_type type,
                    std::vector<network::field_change_t> changes)
  {
    if (!changes.empty())
      diffs.push_back(diff_entity_modified_t{uid, type, std::move(changes)});
  }

  // The entity counterpart of add_geometry_modified: hand it the pre-edit
  // snapshot and the live entity, and it captures the fields that actually
  // differ. No-op when nothing changed, so a click without a drag doesn't push
  // an empty transaction.
  void add_modified_from_diff(shared::entity_uid_t uid,
                              const entity_snapshot_t &before,
                              const network::Entity *after)
  {
    if (!before || !after)
      return;

    if (before->get_type() != after->get_type())
    {
      log_error("transaction: uid {} was captured as {} but is now {} — the edit "
                "is not undoable",
                uid, shared::get_classname_for_entity(before.get()),
                shared::get_classname_for_entity(after));
      return;
    }

    const network::Class_Schema *schema = after->get_schema();
    if (!schema)
    {
      log_error("transaction: entity {} has no registered schema — the edit is "
                "not undoable",
                shared::get_classname_for_entity(after));
      return;
    }

    add_modified(uid, after->get_type(),
                 network::capture_field_changes(before.get(), after, schema));
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
            { restore_entity_into_map(map, d.uid, d.snapshot); },
            [&](const diff_entity_removed_t &d)
            { map.remove_entity(d.uid); },
            [&](const diff_entity_modified_t &d)
            { write_entity_changes(map, d, /*write_new_value=*/true); },
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
            { restore_entity_into_map(map, d.uid, d.snapshot); },
            [&](const diff_entity_modified_t &d)
            { write_entity_changes(map, d, /*write_new_value=*/false); },
            [&](const diff_geometry_created_t &d)
            { map.remove_geometry(d.uid); },
            [&](const diff_geometry_removed_t &d)
            { map.add_geometry_with_uid(d.uid, d.value); },
            [&](const diff_geometry_modified_t &d)
            { set_geometry_value(map, d.uid, d.before); }},
        diff);
  }

  // Put a snapshotted entity back in the map under its original uid. A fresh
  // clone each time, so redoing a delete twice can't hand the map an object the
  // undo stack still owns.
  static void restore_entity_into_map(shared::map_t &map, shared::entity_uid_t uid,
                                      const entity_snapshot_t &snapshot)
  {
    std::shared_ptr<network::Entity> entity = restore_entity(snapshot);
    if (!entity)
    {
      log_error("transaction: could not rebuild the entity for uid {} — it stays "
                "missing",
                uid);
      return;
    }
    map.add_entity_with_uid(uid, entity);
  }

  // Apply or revert one entity modification: memcpy the captured field bytes
  // back over the live entity. Undo and redo differ only in which side is
  // written, which is why there's one function instead of two mirrored ones.
  static void write_entity_changes(shared::map_t &map,
                                   const diff_entity_modified_t &diff,
                                   bool write_new_value)
  {
    shared::map_entity_t *entry = map.find_by_uid(diff.uid);
    if (!entry || !entry->entity)
    {
      log_error("transaction: entity uid {} is gone — cannot restore its fields",
                diff.uid);
      return;
    }

    network::Entity *entity = entry->entity.get();
    if (entity->get_type() != diff.type)
    {
      log_error("transaction: uid {} now holds a {} but the change was captured "
                "against a {} — refusing to write field bytes into it",
                diff.uid, shared::get_classname_for_entity(entity),
                shared::type_to_classname(diff.type));
      return;
    }

    const network::Class_Schema *schema = entity->get_schema();
    if (!schema)
    {
      log_error("transaction: entity {} has no registered schema — cannot "
                "restore its fields",
                shared::get_classname_for_entity(entity));
      return;
    }

    network::write_field_changes(entity, diff.changes, schema, write_new_value);
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
