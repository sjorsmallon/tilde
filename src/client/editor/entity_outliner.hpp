#pragma once

#include "../../shared/array.hpp"
#include "../../shared/entities/generated/entities_generated.hpp"
#include "../../shared/entity_uid.hpp"
#include "../../shared/span.hpp"

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace shared { struct map_t; }

namespace client
{

struct entity_visibility_t
{
  std::unordered_set<shared::entity_uid_t> hidden_entities;
  Enum_Array<entities::entity_type, bool> hidden_types;
  std::vector<shared::entity_uid_t> hidden_this_frame;


  [[nodiscard]] bool is_hidden(entities::entity_type type, shared::entity_uid_t uid) const
  {
    return hidden_types[type] || hidden_entities.count(uid) != 0;
  }

  [[nodiscard]] bool anything_hidden() const;

  void show_all();

  // Rebuilds `hidden_this_frame` from the map. Call once per frame before the
  // context is handed to a tool.
  void refresh(const shared::map_t& map);
};

// what the panel asked for this frame.
struct outliner_result_t
{
  // A row: the object ALONE, group or not -- the outliner is where picking
  // inside a group is free.
  std::optional<shared::entity_uid_t> clicked_object;
  std::optional<shared::entity_uid_t> clicked_group;
  bool                                group_selection = false;
  std::optional<shared::entity_uid_t> ungroup;
};

// One line naming any map object -- classname and name for an entity, the
// kind for geometry -- shared by the outliner's group rows and the inspector's
// member list, so a uid reads the same in both.
[[nodiscard]] std::string object_label(const shared::map_t& map, shared::entity_uid_t uid);

[[nodiscard]] outliner_result_t draw_entity_outliner(const shared::map_t&              map,
                                                     entity_visibility_t&              visibility,
                                                     Span<const shared::entity_uid_t> selection);

} // namespace client
