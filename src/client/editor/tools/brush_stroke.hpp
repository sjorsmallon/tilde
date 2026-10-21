#pragma once

#include "../../../shared/map/map.hpp"
#include "../editor_types.hpp"

#include <vector>

namespace client
{

// A sculpt stroke covers every brush whose bounds meet its sphere and writes
// the same offset at the same world position in each: the weld across brushes
// is by construction, never a read-side step. geometry_def.md Track F.
struct stroke_brush_before_t
{
  shared::entity_uid_t uid = shared::null_entity_uid;
  shared::geometry_value_t before;
};

// Remember `uid`'s value before the stroke first writes it. A later call for
// the same uid changes nothing, so a brush the sphere leaves and re-enters
// keeps the value it had when the stroke began.
void remember_brush_before_stroke(std::vector<stroke_brush_before_t> &befores,
                                  const shared::map_t &map, shared::entity_uid_t uid);

// Sculpt every brush of the map whose bounds meet the sphere, remembering each
// one before its first write and refreshing the mesh of each one touched.
// Answers how many grid vertices moved over all of them.
size_t sculpt_brushes_under_stroke(editor_context_t &ctx,
                                   std::vector<stroke_brush_before_t> &befores,
                                   const linalg::vec3 &center, float radius,
                                   const linalg::vec3 &delta);

// The same sweep over meshes: every mesh of the map with a grid whose bounds
// meet the sphere, remembered before its first write and refreshed when
// touched. geometry_def.md §11.8 increment 11.
size_t sculpt_meshes_under_stroke(editor_context_t &ctx,
                                  std::vector<stroke_brush_before_t> &befores,
                                  const linalg::vec3 &center, float radius,
                                  const linalg::vec3 &delta);

// One undo entry for the whole stroke, a modified diff per remembered brush
// that changed. Answers whether anything changed; a brush that vanished
// mid-stroke is logged and left out.
bool push_stroke_transaction(editor_context_t &ctx, const char *name,
                             std::vector<stroke_brush_before_t> befores);

} // namespace client
