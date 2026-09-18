#include "path_editing.hpp"

#include "../../shared/map_connection.hpp"
#include "../../shared/map_geometry.hpp"
#include "../../shared/movers.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace client
{

namespace
{

const entities::Path_Node_Entity* node_in_map(const shared::map_t& map, shared::entity_uid_t uid)
{
  const shared::map_entity_t* entry = map.find_by_uid(uid);
  if (entry == nullptr)
    return nullptr;
  return entities::entity_as<entities::Path_Node_Entity>(entry->entity.get());
}

void modify_entity(shared::map_t& map, shared::entity_uid_t uid, transaction_t& transaction,
                   auto&& edit)
{
  shared::map_entity_t* entry = map.find_by_uid(uid);
  if (entry == nullptr || !entry->entity)
    return;
  const entity_snapshot_t before = snapshot_entity(entry->entity.get());
  edit(*entry->entity);
  transaction.add_modified_from_diff(uid, before, entry->entity.get());
}

} // namespace

void rebuild_path_scratch(const shared::map_t& map, path_scratch_t& scratch)
{
  scratch.system.populate_from_map(map);
  scratch.links = shared::derive_path_links(scratch.system);
}

std::vector<shared::entity_uid_t> chain_through(const path_scratch_t& scratch,
                                                shared::entity_uid_t node)
{
  std::vector<shared::entity_uid_t> chain;
  if (scratch.system.get<entities::Path_Node_Entity>(node) == nullptr)
    return chain;

  const size_t limit = scratch.system.entities_of<entities::Path_Node_Entity>().size();

  shared::entity_uid_t head = node;
  for (size_t step = 0; step < limit; ++step)
  {
    const shared::entity_uid_t previous = shared::previous_node_of(scratch.links, head);
    if (previous == shared::null_entity_uid || previous == node)
      break;
    head = previous;
  }

  std::unordered_set<shared::entity_uid_t> visited;
  for (shared::entity_uid_t walk = head; walk != shared::null_entity_uid && !visited.contains(walk);)
  {
    const entities::Path_Node_Entity* current = scratch.system.get<entities::Path_Node_Entity>(walk);
    if (current == nullptr)
      break;
    visited.insert(walk);
    chain.push_back(walk);
    walk = current->next;
  }
  return chain;
}

bool chain_is_closed(const path_scratch_t& scratch, const std::vector<shared::entity_uid_t>& chain)
{
  if (chain.empty())
    return false;
  const entities::Path_Node_Entity* tail = scratch.system.get<entities::Path_Node_Entity>(chain.back());
  return tail != nullptr && tail->next == chain.front();
}

std::vector<shared::entity_uid_t> geometry_owned_by(const shared::map_t& map,
                                                    shared::entity_uid_t owner)
{
  std::vector<shared::entity_uid_t> owned;
  for (const shared::map_geometry_t& entry : map.geometry)
    if (shared::get_owner_uid(entry.value) == owner)
      owned.push_back(entry.uid);
  return owned;
}

void relink_paths_around_removed_nodes(shared::map_t& map, Span<const shared::entity_uid_t> removed,
                                       transaction_t& transaction)
{
  std::unordered_set<shared::entity_uid_t> removed_nodes;
  for (shared::entity_uid_t uid : removed)
    if (node_in_map(map, uid) != nullptr)
      removed_nodes.insert(uid);
  if (removed_nodes.empty())
    return;

  std::unordered_map<shared::entity_uid_t, shared::entity_uid_t> previous_of;
  std::unordered_map<shared::entity_uid_t, uint32_t>             predecessor_count;
  for (const auto [uid, node] : map.entities_of_type<entities::Path_Node_Entity>())
  {
    if (node->next == shared::null_entity_uid)
      continue;
    previous_of[node->next] = ++predecessor_count[node->next] == 1 ? uid : shared::null_entity_uid;
  }

  const size_t limit = map.entities.size();
  const auto survivor_after = [&](shared::entity_uid_t uid)
  {
    for (size_t step = 0; step < limit && removed_nodes.contains(uid); ++step)
    {
      const entities::Path_Node_Entity* node = node_in_map(map, uid);
      uid = node != nullptr ? node->next : shared::null_entity_uid;
    }
    return removed_nodes.contains(uid) ? shared::null_entity_uid : uid;
  };
  const auto survivor_before = [&](shared::entity_uid_t uid)
  {
    for (size_t step = 0; step < limit && removed_nodes.contains(uid); ++step)
    {
      const auto found = previous_of.find(uid);
      uid = found != previous_of.end() ? found->second : shared::null_entity_uid;
    }
    return removed_nodes.contains(uid) ? shared::null_entity_uid : uid;
  };

  std::vector<std::pair<shared::entity_uid_t, shared::entity_uid_t>> node_relinks;
  for (const auto [uid, node] : map.entities_of_type<entities::Path_Node_Entity>())
  {
    if (removed_nodes.contains(uid) || !removed_nodes.contains(node->next))
      continue;
    const shared::entity_uid_t past = survivor_after(node->next);
    node_relinks.push_back({uid, past == uid ? shared::null_entity_uid : past});
  }

  std::vector<std::pair<shared::entity_uid_t, shared::entity_uid_t>> mover_relinks;
  for (const auto [uid, mover] : map.entities_of_type<entities::Mover_Entity>())
  {
    if (!removed_nodes.contains(mover->follow.from))
      continue;
    shared::entity_uid_t start = survivor_after(mover->follow.from);
    if (start == shared::null_entity_uid)
      start = survivor_before(mover->follow.from);
    mover_relinks.push_back({uid, start});
  }

  for (const auto& [uid, next] : node_relinks)
    modify_entity(map, uid, transaction, [&](entities::Entity& entity)
                  { static_cast<entities::Path_Node_Entity&>(entity).next = next; });
  for (const auto& [uid, start] : mover_relinks)
    modify_entity(map, uid, transaction, [&](entities::Entity& entity)
                  { static_cast<entities::Mover_Entity&>(entity).follow.from = start; });
}

void draw_path_links(const shared::map_t& map, const editor_context_t& ctx, pass_builder_t& draws)
{
  for (const auto [uid, node] : map.entities_of_type<entities::Path_Node_Entity>())
  {
    if (!ctx.object_is_visible(uid) || node->next == shared::null_entity_uid)
      continue;
    if (const entities::Path_Node_Entity* next = node_in_map(map, node->next))
      draws.debug.arrow(node->position, next->position, colors::green);
    else
      draws.debug.backed_text(node->position, "next names no path node", colors::red);
  }

  for (const auto [uid, mover] : map.entities_of_type<entities::Mover_Entity>())
  {
    if (!ctx.object_is_visible(uid))
      continue;
    const entities::Path_Node_Entity* start = node_in_map(map, mover->follow.from);
    if (start == nullptr)
    {
      draws.debug.backed_text(mover->position, "mover starts from no path node", colors::red);
      continue;
    }
    draws.debug.line(mover->position, start->position, colors::magenta);
  }
}

} // namespace client
