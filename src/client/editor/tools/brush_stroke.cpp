#include "brush_stroke.hpp"

#include "../../../shared/aabb.hpp"
#include "../../../shared/log.hpp"
#include "../../../shared/map/map_geometry.hpp"
#include "../../geometry_renderer.hpp"
#include "../transaction_system.hpp"

#include <variant>

namespace client
{

void remember_brush_before_stroke(std::vector<stroke_brush_before_t> &befores,
                                  const shared::map_t &map, shared::entity_uid_t uid)
{
  for (const stroke_brush_before_t &remembered : befores)
  {
    if (remembered.uid == uid)
      return;
  }

  const shared::map_geometry_t *entry = map.find_geometry_by_uid(uid);
  if (!entry)
  {
    log_error("brush stroke: brush {} is not in the map - the stroke will not touch it", uid);
    return;
  }
  befores.push_back({uid, entry->value});
}

size_t sculpt_brushes_under_stroke(editor_context_t &ctx,
                                   std::vector<stroke_brush_before_t> &befores,
                                   const linalg::vec3 &center, float radius,
                                   const linalg::vec3 &delta)
{
  size_t moved = 0;
  for (shared::map_geometry_t &entry : ctx.map->geometry)
  {
    shared::brush_geometry_t *brush = std::get_if<shared::brush_geometry_t>(&entry.value);
    if (!brush || !shared::aabb_intersects_sphere(shared::get_bounds(entry.value), center, radius))
      continue;

    remember_brush_before_stroke(befores, *ctx.map, entry.uid);
    const size_t touched = shared::sculpt_brush_grid_vertices(*brush, center, radius, delta,
                                                              {ctx.map->geometry, entry.uid});
    if (touched == 0)
      continue;

    moved += touched;
    refresh_generated_geometry_mesh(entry.value, entry.uid, ctx.map->materials,
                                    ctx.map->lightmap, ctx.map->geometry);
  }
  return moved;
}

bool push_stroke_transaction(editor_context_t &ctx, const char *name,
                             std::vector<stroke_brush_before_t> befores)
{
  transaction_t transaction;
  for (stroke_brush_before_t &remembered : befores)
  {
    const shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(remembered.uid);
    if (!entry)
    {
      log_error("brush stroke: brush {} vanished mid-stroke - its edit is not undoable",
                remembered.uid);
      continue;
    }
    transaction.add_geometry_modified(remembered.uid, std::move(remembered.before),
                                      entry->value);
  }

  if (transaction.empty())
    return false;

  ctx.transaction_system.push(name, std::move(transaction));
  return true;
}

} // namespace client
