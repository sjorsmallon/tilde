#include "selection_tool.hpp"
#include "../../../shared/log.hpp"

#include <algorithm>

// A brush holds world-space points, so writing one rebuilds and re-uploads its
// mesh. A live drag therefore writes entities as it goes and holds geometry as
// an offset the editor draws, written once when the drag settles.

namespace client
{

void Selection_Tool::place_dragged_object(editor_context_t& ctx, const drag_origin_t& origin,
                                          const linalg::vec3& position)
{
  if (ctx.map->find_geometry_by_uid(origin.uid) == nullptr)
  {
    if (!shared::try_set_object_position(*ctx.map, origin.uid, position))
      log_error("selection tool: object {} vanished mid-drag", origin.uid);
    return;
  }

  const std::vector<live_drag_offset_t>::iterator slot = std::lower_bound(
      live_drag_offsets_sorted.begin(), live_drag_offsets_sorted.end(), origin.uid,
      [](const live_drag_offset_t& entry, shared::entity_uid_t uid) { return entry.uid < uid; });
  if (slot != live_drag_offsets_sorted.end() && slot->uid == origin.uid)
    slot->translation = position - origin.position;
  else
    live_drag_offsets_sorted.insert(slot, {origin.uid, position - origin.position});
}

void Selection_Tool::settle_live_drag(editor_context_t& ctx)
{
  if (ctx.map)
  {
    for (const live_drag_offset_t& offset : live_drag_offsets_sorted)
    {
      const std::optional<linalg::vec3> position =
          shared::try_get_object_position(*ctx.map, offset.uid);
      if (!position ||
          !shared::try_set_object_position(*ctx.map, offset.uid, *position + offset.translation))
        log_error("selection tool: object {} vanished before its drag settled", offset.uid);
    }
  }
  live_drag_offsets_sorted.clear();
}

} // namespace client
