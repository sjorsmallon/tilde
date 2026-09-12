#include "map_group.hpp"

#include "log.hpp"
#include "map.hpp"

#include <algorithm>
#include <unordered_set>

namespace shared
{

const map_group_t* find_group_of(const map_t& map, entity_uid_t member)
{
  for (const map_group_t& group : map.groups)
    if (std::find(group.members.begin(), group.members.end(), member) != group.members.end())
      return &group;
  return nullptr;
}

map_group_t* find_group_of(map_t& map, entity_uid_t member)
{
  return const_cast<map_group_t*>(find_group_of(static_cast<const map_t&>(map), member));
}

const map_group_t* find_group_by_uid(const map_t& map, entity_uid_t group_uid)
{
  for (const map_group_t& group : map.groups)
    if (group.uid == group_uid)
      return &group;
  return nullptr;
}

void expand_to_group(const map_t& map, entity_uid_t uid, std::vector<entity_uid_t>& out)
{
  const auto append = [&out](entity_uid_t candidate)
  {
    if (std::find(out.begin(), out.end(), candidate) == out.end())
      out.push_back(candidate);
  };

  const map_group_t* group = find_group_of(map, uid);
  if (group == nullptr)
  {
    append(uid);
    return;
  }

  // Only what the map still has: a member deleted in this session is inert
  // rather than pruned, and a selection must not name it.
  for (entity_uid_t member : group->members)
    if (map.has_object(member))
      append(member);
}

bool uid_sets_equal(Span<const entity_uid_t> left, Span<const entity_uid_t> right)
{
  if (left.size() != right.size())
    return false;
  for (entity_uid_t uid : right)
    if (std::find(left.begin(), left.end(), uid) == left.end())
      return false;
  return true;
}

namespace
{

void dissolve_groups_smaller_than_two(map_t& map)
{
  std::erase_if(map.groups, [](const map_group_t& group) { return group.members.size() < 2; });
}

} // namespace

entity_uid_t group_objects(map_t& map, Span<const entity_uid_t> uids, std::string name)
{
  map_group_t group;
  for (entity_uid_t uid : uids)
  {
    if (!map.has_object(uid))
      continue;
    if (std::find(group.members.begin(), group.members.end(), uid) != group.members.end())
      continue;
    group.members.push_back(uid);
  }

  if (group.members.size() < 2)
  {
    log_warning("group_objects: a group needs two objects; {} of the {} uids named are in the map",
                group.members.size(), uids.size());
    return null_entity_uid;
  }

  // Exclusive: out of the old group first, and a group that shrank below two is
  // no longer a group.
  for (map_group_t& existing : map.groups)
    std::erase_if(existing.members, [&group](entity_uid_t member) {
      return std::find(group.members.begin(), group.members.end(), member) != group.members.end();
    });
  dissolve_groups_smaller_than_two(map);

  group.uid  = map.next_uid++;
  group.name = std::move(name);
  const entity_uid_t uid = group.uid;
  map.groups.push_back(std::move(group));
  return uid;
}

void add_group_with_uid(map_t& map, map_group_t group)
{
  if (group.uid >= map.next_uid)
    map.next_uid = group.uid + 1;
  map.groups.push_back(std::move(group));
}

bool ungroup(map_t& map, entity_uid_t group_uid)
{
  const size_t before = map.groups.size();
  std::erase_if(map.groups, [group_uid](const map_group_t& group) { return group.uid == group_uid; });
  return map.groups.size() != before;
}

size_t remap_group_members(map_group_t& group, const uid_remap_t& remap)
{
  std::vector<entity_uid_t> remapped;
  remapped.reserve(group.members.size());
  for (entity_uid_t member : group.members)
  {
    auto it = remap.find(member);
    if (it != remap.end())
      remapped.push_back(it->second);
  }
  group.members = std::move(remapped);
  return group.members.size();
}

size_t prune_map_groups(map_t& map)
{
  size_t                           lines = 0;
  std::unordered_set<entity_uid_t> claimed;

  for (map_group_t& group : map.groups)
  {
    std::vector<entity_uid_t> kept;
    kept.reserve(group.members.size());
    for (entity_uid_t member : group.members)
    {
      if (!map.has_object(member))
      {
        log_warning("map groups: \"{}\" (uid {}) names uid {}, which the map does not have -- dropped",
                    group.name, group.uid, member);
        ++lines;
        continue;
      }
      if (!claimed.insert(member).second)
      {
        log_warning("map groups: uid {} is in \"{}\" (uid {}) and in an earlier group -- an object "
                    "belongs to one group, so it stays in the first",
                    member, group.name, group.uid);
        ++lines;
        continue;
      }
      kept.push_back(member);
    }
    group.members = std::move(kept);
  }

  for (const map_group_t& group : map.groups)
  {
    if (group.members.size() < 2)
    {
      log_warning("map groups: \"{}\" (uid {}) has {} member(s) left -- dissolved",
                  group.name, group.uid, group.members.size());
      ++lines;
    }
  }
  dissolve_groups_smaller_than_two(map);

  return lines;
}

} // namespace shared
