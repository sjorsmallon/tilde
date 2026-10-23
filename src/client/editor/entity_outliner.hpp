#pragma once

#include "../../shared/array.hpp"
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
  std::vector<shared::entity_uid_t> hidden_this_frame;

  [[nodiscard]] bool is_hidden(shared::entity_uid_t uid) const { return hidden_entities.count(uid) != 0; }

  [[nodiscard]] bool anything_hidden() const { return !hidden_this_frame.empty(); }

  void show_all() { hidden_entities.clear(); }

  // Rebuilds `hidden_this_frame` from the map. Call once per frame before the
  // context is handed to a tool.
  void refresh(const shared::map_t& map);
};

// The row being renamed in place, if any: a group's or an entity's.
struct row_rename_t
{
  shared::entity_uid_t uid      = shared::null_entity_uid;
  bool                 is_group = false;
  Array<char, 96>      name;
  int                  frames_open = 0;
};

struct outliner_state_t
{
  row_rename_t                      rename;
  // F2: rename the one group or entity the selection is, on the next draw.
  bool                              rename_requested = false;
  std::vector<shared::entity_uid_t> previous_selection;
  // A selection the list itself asked for lands a frame later and is already
  // on screen, so it is not revealed.
  int frames_ignoring_selection_change = 0;
  // "Go to" puts the camera this far above the object's top.
  float go_to_height = 256.0f;
};

// what the panel asked for this frame.
struct outliner_result_t
{
  // A row: the object ALONE, group or not -- the outliner is where picking
  // inside a group is free.
  std::optional<shared::entity_uid_t> clicked_object;
  std::optional<shared::entity_uid_t> clicked_group;
  // Ctrl or shift was held: the click toggles into the selection.
  bool                                toggles = false;
  bool                                group_selection = false;
  std::optional<shared::entity_uid_t> ungroup;
  std::optional<shared::entity_uid_t> renamed_group;
  std::optional<shared::entity_uid_t> renamed_entity;
  std::string                         new_name;
  bool                                nothing_to_rename = false;
  // An object or a group uid: fly the camera to it.
  std::optional<shared::entity_uid_t> go_to;
};

// One line naming any map object -- classname and name for an entity, the
// kind for geometry -- shared by the outliner's rows and the sidebar header,
// so a uid reads the same in both.
[[nodiscard]] std::string object_label(const shared::map_t& map, shared::entity_uid_t uid);

// Groups and loose entities at the top level, loose geometry under one node.
// A selection made elsewhere opens its group and scrolls its row into view.
[[nodiscard]] outliner_result_t draw_entity_outliner(const shared::map_t&              map,
                                                     entity_visibility_t&              visibility,
                                                     outliner_state_t&                 state,
                                                     Span<const shared::entity_uid_t> selection);

} // namespace client
