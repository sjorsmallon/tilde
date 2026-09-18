#pragma once

#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/shapes.hpp"
#include "editor_types.hpp"

#include <optional>
#include <variant>
#include <vector>

// How the editor treats an entity, in two halves that are different KINDS of
// fact and are kept apart on purpose:
//
//   PER TYPE      editor_data_per_entity_type_t -- colour, icon, stand-in shape
//                 and the three draw functions. Constants, one row per
//                 entity_type, in a table pinned by rows_in_enum_order.
//   PER INSTANCE  editor_shape_at() -- the shape THIS entity is drawn and
//                 picked as, derived from its components and, failing those,
//                 from its type's stand-in. Nothing else answers that question,
//                 which is what makes the box you click the box you see.
//
// The `.def` cannot say any of this: it is about how the editor DRAWS a type,
// and the editor is the only party to that decision.

namespace client
{

// The type's screen-space icon, or no `texture` for a type that has none.
//
// The colour is the TYPE's, used only where the instance has nothing better to
// say; a light's icon is tinted by its own colour instead.
struct entity_icon_t
{
  std::optional<assets::texture_asset> texture;
  color_t                              fallback_color = colors::white;
};

entity_icon_t get_entity_icon(const entities::Entity* e);

// The shape the editor draws and picks an entity as, at `position` -- a
// parameter because the placement ghost is drawn where the cursor is, not
// where the entity is. ONE ordered rule, and no type arm but the stand-in:
//
//   1. a Box_Volume component      -> that box (a trigger, a pad, a hitbox)
//   2. else a Render mesh          -> its bounds under render.scale
//   3. else the type's stand-in    -> the player hull, the spectate frustum,
//                                     the pyramid marker
//   4. else a point, padded so it can be clicked at all
//
// A frustum is a shape of its own rather than its bound because its corner is
// empty space, and a click there should fall through to what is behind it.
using editor_shape_t = std::variant<shared::aabb_bounds_t, shared::spectate_frustum_t>;

editor_shape_t editor_shape_at(const entities::Entity* e, const linalg::vec3& position);

// The two readings of editor_shape_at(e, e->position) the picking BVH wants.
shared::aabb_bounds_t editor_bounds_of(const entities::Entity* e);
std::vector<Plane>    editor_collision_planes_of(const entities::Entity* e);

// Every context draws the same three layers: ART (the render component, else
// the type's stand-in, else the shape as a wire box), the type's DIAGRAM on top
// of it, and the REACH on top of that for the selected and the placed entity
// only. A diagram never substitutes for art: a pad that grows a mesh keeps its
// arrow.

// Placement preview at `origin` -- the entity's position, NOT necessarily the
// center of the drawn shape.
void draw_entity_ghost(const entities::Entity* e, pass_builder_t& draws,
                       const linalg::vec3& origin, const entity_draw_settings_t& settings);

void draw_entity_in_editor(const entities::Entity* e, pass_builder_t& draws,
                           const entity_draw_settings_t& settings);

// Surface point under the cursor -> where entity->position goes, lifted so the
// entity's shape RESTS on the surface: zero for a type whose origin is at its
// feet (the hull rises from position), half a box for a centred one, whatever
// the mesh says for a prop. The ORIGIN, not the center of the drawn shape.
linalg::vec3 compute_placement_origin(const entities::Entity* e,
                                      const linalg::vec3& ghost_position);

// The selection highlight's pink <-> white pulse at time `time`. Shared so the
// geometry highlight pulses in lockstep with the entity one.
color_t compute_selection_pulse_color(float time);

// The three layers in the pulse colour; the art is skipped when the outline already traces it.
void draw_selection_highlight(const entities::Entity* e, pass_builder_t& draws, float time,
                              float grid_step, const entity_draw_settings_t& settings,
                              bool art_is_outlined);

} // namespace client
