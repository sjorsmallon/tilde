#pragma once

#include "../../shared/entity.hpp"
#include "../../shared/map.hpp"
#include "../../shared/network/schema.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <stack>
#include <string>
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

struct entity_delta_t
{
  enum class type_t
  {
    Add,
    Remove,
    Modify
  };

  type_t type;
  shared::entity_uid_t entity_uid;

  // For Add/Remove: full entity snapshot
  entity_snapshot_t snapshot;

  // For Modify: changed properties with before/after values
  std::vector<property_change_t> changes;
};

struct transaction_t
{
  std::vector<entity_delta_t> deltas;
  bool empty() const { return deltas.empty(); }
};

// --- Transaction_System ---
// Passive undo/redo stack.

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
    for (const auto &delta : t.deltas)
      apply_delta(map, delta);
  }

  void revert_transaction(shared::map_t &map, const transaction_t &t)
  {
    for (auto it = t.deltas.rbegin(); it != t.deltas.rend(); ++it)
      revert_delta(map, *it);
  }

  void apply_delta(shared::map_t &map, const entity_delta_t &d)
  {
    switch (d.type)
    {
    case entity_delta_t::type_t::Add:
    {
      auto ent = shared::create_entity_by_classname(d.snapshot.classname);
      if (ent)
      {
        ent->init_from_map(d.snapshot.properties);
        map.add_entity_with_uid(d.entity_uid, ent);
      }
      break;
    }
    case entity_delta_t::type_t::Remove:
    {
      map.remove_entity(d.entity_uid);
      break;
    }
    case entity_delta_t::type_t::Modify:
    {
      auto *entry = map.find_by_uid(d.entity_uid);
      if (entry && entry->entity)
      {
        std::map<std::string, std::string> props;
        for (const auto &c : d.changes)
          props[c.field] = c.after;
        entry->entity->init_from_map(props);
      }
      break;
    }
    }
  }

  void revert_delta(shared::map_t &map, const entity_delta_t &d)
  {
    switch (d.type)
    {
    case entity_delta_t::type_t::Add:
    {
      map.remove_entity(d.entity_uid);
      break;
    }
    case entity_delta_t::type_t::Remove:
    {
      auto ent = shared::create_entity_by_classname(d.snapshot.classname);
      if (ent)
      {
        ent->init_from_map(d.snapshot.properties);
        map.add_entity_with_uid(d.entity_uid, ent);
      }
      break;
    }
    case entity_delta_t::type_t::Modify:
    {
      auto *entry = map.find_by_uid(d.entity_uid);
      if (entry && entry->entity)
      {
        std::map<std::string, std::string> props;
        for (const auto &c : d.changes)
          props[c.field] = c.before;
        entry->entity->init_from_map(props);
      }
      break;
    }
    }
  }
};

// --- Edit_Recorder ---
// Wraps map mutations and records them for undo/redo.

class Edit_Recorder
{
public:
  explicit Edit_Recorder(shared::map_t &map) : map_(map) {}

  // Add entity to map and record it
  shared::entity_uid_t add(std::shared_ptr<network::Entity> ent)
  {
    shared::entity_uid_t uid = map_.add_entity(ent);

    entity_delta_t d;
    d.type = entity_delta_t::type_t::Add;
    d.entity_uid = uid;
    d.snapshot.classname = shared::get_classname_for_entity(ent.get());
    d.snapshot.properties = ent->get_all_properties();
    txn_.deltas.push_back(std::move(d));

    return uid;
  }

  // Record removal and remove entity from map
  void remove(shared::entity_uid_t uid)
  {
    auto *entry = map_.find_by_uid(uid);
    if (!entry || !entry->entity)
      return;

    entity_delta_t d;
    d.type = entity_delta_t::type_t::Remove;
    d.entity_uid = uid;
    d.snapshot.classname =
        shared::get_classname_for_entity(entry->entity.get());
    d.snapshot.properties = entry->entity->get_all_properties();
    txn_.deltas.push_back(std::move(d));

    map_.remove_entity(uid);
  }

  // Snapshot entity before modification
  void track(shared::entity_uid_t uid)
  {
    auto *entry = map_.find_by_uid(uid);
    if (!entry || !entry->entity)
      return;
    tracked_[uid] = entry->entity->get_all_properties();
  }

  // Diff entity after modification, record changes
  void finish(shared::entity_uid_t uid)
  {
    auto it = tracked_.find(uid);
    if (it == tracked_.end())
      return;

    auto *entry = map_.find_by_uid(uid);
    if (!entry || !entry->entity)
      return;

    auto new_props = entry->entity->get_all_properties();
    const auto &old_props = it->second;

    entity_delta_t d;
    d.type = entity_delta_t::type_t::Modify;
    d.entity_uid = uid;

    for (const auto &[key, old_val] : old_props)
    {
      auto nit = new_props.find(key);
      if (nit != new_props.end() && nit->second != old_val)
      {
        d.changes.push_back({key, old_val, nit->second});
      }
    }

    if (!d.changes.empty())
      txn_.deltas.push_back(std::move(d));

    tracked_.erase(it);
  }

  // Extract the transaction (empty if nothing changed)
  std::optional<transaction_t> take()
  {
    if (txn_.empty())
      return std::nullopt;
    return std::move(txn_);
  }

private:
  shared::map_t &map_;
  transaction_t txn_;
  std::map<shared::entity_uid_t, std::map<std::string, std::string>> tracked_;
};

} // namespace client
